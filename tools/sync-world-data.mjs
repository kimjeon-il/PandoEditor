import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, renameSync, unlinkSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { gunzipSync } from 'node:zlib';
import {
  gitBlobSha, safeRelativePath, sha256, validateWorldDataset,
  validateWorldAssetBytes, verifyWorldFiles, WORLD_ROLES,
} from './lib/world-dataset-contract.mjs';

export const WEB_ASSET_ROLES = Object.freeze({
  countryPreview: ['previewCountries', 'countries-preview', '.geojson.gz'],
  previewMesh: ['previewMesh', 'world-mesh-preview', '.bin.gz'],
  countryCanonical: ['canonicalCountryPacket', 'countries-canonical', '.pcg.gz'],
  canonicalMesh: ['canonicalMesh', 'world-mesh', '.bin.gz'],
  labelAnchors: ['labelAnchors', 'country-label-anchors', '.json'],
});
const HEX40 = /^[a-f0-9]{40}$/;
const HEX64 = /^[a-f0-9]{64}$/;
const SOURCE = 'https://raw.githubusercontent.com/kimjeon-il/Pando';
function requireValue(value, error) { if (!value) throw new Error(error); }
function unpack(spec, bytes, role) {
  requireValue(bytes.length === spec.compressedBytes && sha256(bytes) === spec.sha256,
    `Upstream stored byte mismatch: ${role}`);
  let raw;
  try { raw = spec.encoding === 'gzip' ? gunzipSync(bytes) : bytes; }
  catch { throw new Error(`Upstream gzip failed: ${role}`); }
  requireValue(raw.length === spec.decodedBytes && raw.length < 80_000_000,
    `Upstream decoded byte count mismatch: ${role}`);
  if (spec.header) {
    requireValue(Array.isArray(spec.header) && raw.length >= spec.header.length * 4,
      `Missing header: ${role}`);
    for (let i = 0; i < spec.header.length; i++)
      requireValue(raw.readUInt32LE(i * 4) === spec.header[i],
        `Upstream packet header mismatch: ${role}/${i}`);
  }
  if (role === 'countryPreview') {
    const collection = JSON.parse(raw.toString('utf8'));
    const ids = collection?.features?.map(x => x?.id);
    requireValue(collection.type === 'FeatureCollection' && ids.length === 258
      && new Set(ids).size === 258 && ids.every(Boolean),
    'Upstream country preview count/IDs changed');
  }
  if (role === 'labelAnchors') {
    const anchors = JSON.parse(raw.toString('utf8'));
    requireValue(anchors.version && Object.keys(anchors.anchors || {}).length === 258,
      'Upstream label anchors country count changed');
  }
  if (role === 'countryCanonical')
    requireValue(raw.readUInt32LE(8) === 258 && raw.readUInt32LE(20) === 548454,
      'Unsupported canonical country packet geometry count');
  if (role === 'canonicalMesh' || role === 'previewMesh')
    requireValue(raw.readUInt32LE(0) === 0x434d4731 && raw.readUInt32LE(8) === 258,
      'Unsupported GPU country mesh format');
}

export function validateWebWorldBundle(bundle) {
  requireValue(bundle?.schema === 'pandolab-world-bundle' && bundle.schemaVersion === 1
    && bundle.source?.countryCount === 258 && HEX64.test(bundle.source?.sha256 || ''),
  'Unexpected upstream world bundle schema/source');
  for (const [role, [webKey, prefix, ext]] of Object.entries(WEB_ASSET_ROLES)) {
    const spec = bundle.assets?.[webKey];
    requireValue(spec && HEX64.test(spec.sha256 || '')
      && spec.url === `world/objects/${prefix}-sha256-${spec.sha256}${ext}`
      && Number.isSafeInteger(spec.compressedBytes) && spec.compressedBytes > 0
      && spec.compressedBytes < 100_000_000
      && Number.isSafeInteger(spec.decodedBytes) && spec.decodedBytes > 0
      && safeRelativePath(spec.url)
      && spec.encoding === (role === 'labelAnchors' ? 'identity' : 'gzip'),
    `Invalid upstream world content address: ${role}`);
  }
  return bundle;
}

export function buildWorldSyncPlan({
  current, webRef, webBundleBytes, webBundle, downloaded, physicalDrift = [],
}) {
  validateWorldDataset(current);
  requireValue(HEX40.test(webRef), 'An exact upstream commit SHA is required');
  validateWebWorldBundle(webBundle);
  const digest = sha256(webBundleBytes);
  const candidate = structuredClone(current);
  candidate.origin = {
    repository: 'kimjeon-il/Pando', mode: 'web-bundle',
    webCommit: webRef, worldBundleSha256: digest,
  };
  const changed = [];
  for (const [role, [webKey]] of Object.entries(WEB_ASSET_ROLES)) {
    const spec = webBundle.assets[webKey];
    const bytes = downloaded.get(role);
    requireValue(Buffer.isBuffer(bytes), `Missing downloaded upstream asset ${role}`);
    unpack(spec, bytes, role);
    const next = {
      ...current[role],
      path: spec.url,
      gitBlobSha: gitBlobSha(bytes),
      sha256: spec.sha256,
      bytes: bytes.length,
      source: { ref: webRef, path: `assets/data/${spec.url}` },
    };
    candidate[role] = next;
    if (JSON.stringify(next) !== JSON.stringify(current[role]))
      changed.push({ role, before: current[role].sha256, after: next.sha256,
        path: next.path, bytes: bytes.length });
  }
  validateWorldDataset(candidate);
  return { candidate, changed, physicalDrift, webBundleSha256: digest, webRef, downloaded };
}

async function verifiedFetch(url, maxBytes = 100_000_000) {
  const response = await fetch(url, {
    signal: AbortSignal.timeout(120_000),
    redirect: 'error',
    headers: { 'Accept-Encoding': 'identity' },
  });
  if (!response.ok) throw new Error(`Upstream fetch failed (${response.status}): ${url}`);
  const declared = Number(response.headers.get('content-length') || 0);
  requireValue(!declared || declared <= maxBytes, 'Remote source exceeds size limit');
  const bytes = Buffer.from(await response.arrayBuffer());
  requireValue(bytes.length <= maxBytes, 'Remote payload exceeds size limit');
  return bytes;
}
export function sourceReader({ webRef, webRoot }) {
  requireValue(HEX40.test(webRef), 'Provide full 40-character --web-ref SHA');
  if (webRoot) {
    const local = resolve(webRoot);
    const head = execFileSync('git', ['-C', local, 'rev-parse', 'HEAD'],
      { encoding: 'utf8' }).trim();
    requireValue(head === webRef, 'Local web checkout does not match pinned --web-ref');
    return async path => {
      requireValue(safeRelativePath(path) && path.startsWith('assets/data/'),
        'Unapproved upstream data path');
      return readFileSync(resolve(local, path));
    };
  }
  return async path => {
    requireValue(safeRelativePath(path) && path.startsWith('assets/data/'),
      'Unapproved upstream data path');
    return verifiedFetch(`${SOURCE}/${webRef}/${path}`);
  };
}
export async function planWorldSync({ appRoot, webRef, webRoot = null, reader = null }) {
  const local = resolve(appRoot, 'assets/world');
  const current = validateWorldDataset(JSON.parse(readFileSync(join(local, 'manifest.json'), 'utf8')));
  verifyWorldFiles(local, current);
  const get = reader || sourceReader({ webRef, webRoot });
  const raw = await get('assets/data/world/current.json');
  requireValue(raw.length < 1_000_000, 'World bundle manifest too large');
  const bundle = validateWebWorldBundle(JSON.parse(raw.toString('utf8')));
  const downloaded = new Map();
  for (const [role, [webKey]] of Object.entries(WEB_ASSET_ROLES)) {
    downloaded.set(role, await get(`assets/data/${bundle.assets[webKey].url}`));
  }
  const physicalDrift = [];
  for (const role of ['terrain', 'hydro']) {
    const bytes = await get(current[role].source.path);
    if (sha256(bytes) !== current[role].sha256 || bytes.length !== current[role].bytes)
      physicalDrift.push(role);
  }
  const plan = buildWorldSyncPlan({
    current, webRef, webBundleBytes: raw, webBundle: bundle, downloaded, physicalDrift,
  });
  return { ...plan, downloaded };
}

function atomicWrite(target, bytes) {
  const temporary = `${target}.sync-${process.pid}-${Date.now()}`;
  try {
    writeFileSync(temporary, bytes, { flag: 'wx' });
    renameSync(temporary, target);
  } finally {
    if (existsSync(temporary)) unlinkSync(temporary);
  }
}
export function applyWorldSyncPlan({ appRoot, plan, approveBundle }) {
  requireValue(HEX64.test(approveBundle || '') && approveBundle === plan.webBundleSha256,
    'Explicit --approve-bundle SHA-256 must match exact upstream world/current.json bytes');
  requireValue(plan.physicalDrift.length === 0,
    `Physical inventory is pinned and requires separate review: ${plan.physicalDrift.join(', ')}`);
  const root = resolve(appRoot, 'assets/world');
  const oldRaw = readFileSync(join(root, 'manifest.json'), 'utf8');
  const existing = validateWorldDataset(JSON.parse(oldRaw));
  verifyWorldFiles(root, existing);
  const newFiles = [];
  try {
    for (const role of Object.keys(WEB_ASSET_ROLES)) {
      const asset = plan.candidate[role], bytes = plan.downloaded.get(role);
      validateWorldAssetBytes(plan.candidate, role, bytes);
      const target = resolve(root, asset.path);
      if (existsSync(target)) {
        requireValue(sha256(readFileSync(target)) === asset.sha256,
          `Existing immutable object has different bytes: ${role}`);
        continue;
      }
      mkdirSync(dirname(target), { recursive: true });
      writeFileSync(target, bytes, { flag: 'wx' });
      newFiles.push(target);
    }
    // The manifest becomes active only after every new immutable object is verified.
    atomicWrite(join(root, 'manifest.json'), `${JSON.stringify(plan.candidate, null, 2)}\n`);
    return { updated: true, assetsAdded: newFiles.length, changedRoles: plan.changed.length };
  } catch (error) {
    for (const path of newFiles) unlinkSync(path);
    throw error;
  }
}

function options(argv) {
  const parsed = {};
  for (let i = 0; i < argv.length; i++) {
    const name = argv[i];
    if (name === '--apply') { parsed.apply = true; continue; }
    if (!['--web-ref', '--web-root', '--app-root', '--approve-bundle'].includes(name)
      || !argv[i + 1]) throw new Error(`Unknown or incomplete option: ${name}`);
    parsed[name.slice(2).replaceAll('-', '')] = argv[++i];
  }
  if (!parsed.webref) throw new Error('Usage: --web-ref <full-Git-SHA> [--web-root <checkout>] [--apply --approve-bundle <SHA-256>]');
  if (!!parsed.approvebundle !== !!parsed.apply)
    throw new Error('Approval digest and --apply must be specified together');
  return parsed;
}
const invoked = process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url);
if (invoked) {
  try {
    const argv = options(process.argv.slice(2));
    const appRoot = resolve(argv.approot || fileURLToPath(new URL('../', import.meta.url)));
    const plan = await planWorldSync({ appRoot, webRef: argv.webref, webRoot: argv.webroot });
    const report = {
      sourceCommit: plan.webRef, bundleSha256: plan.webBundleSha256,
      changed: plan.changed, physicalDrift: plan.physicalDrift,
      mode: argv.apply ? 'approved-apply' : 'read-only',
    };
    if (argv.apply) report.result = applyWorldSyncPlan({
      appRoot, plan, approveBundle: argv.approvebundle,
    });
    console.log(JSON.stringify(report, null, 2));
  } catch (error) {
    console.error(`World data sync failed: ${error.message}`);
    process.exitCode = 1;
  }
}
