import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync, mkdirSync, mkdtempSync, existsSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname, resolve } from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { gunzipSync, gzipSync } from 'node:zlib';
import {
  WORLD_ROLES, validateWorldDataset, validateWorldAssetBytes, verifyWorldFiles,
  safeRelativePath, sha256,
} from './lib/world-dataset-contract.mjs';
import {
  WEB_ASSET_ROLES, buildWorldSyncPlan, validateWebWorldBundle, applyWorldSyncPlan, sourceReader, planWorldSync,
} from './sync-world-data.mjs';

const root = fileURLToPath(new URL('../assets/world/', import.meta.url));
const currentBytes = readFileSync(join(root, 'manifest.json'));
const initial = JSON.parse(currentBytes.toString('utf8'));
const commit = 'f'.repeat(40);

function previewBundle() {
  const assets = {};
  const buffers = new Map();
  for (const [role, [webRole, name, ext]] of Object.entries(WEB_ASSET_ROLES)) {
    const bytes = readFileSync(join(root, initial[role].path));
    buffers.set(role, bytes);
    const decoded = role === 'labelAnchors' ? bytes : gunzipSync(bytes);
    const hash = sha256(bytes);
    const headerWords = role === 'countryCanonical' ? 20
      : role === 'canonicalMesh' || role === 'previewMesh' ? 12 : 0;
    const header = headerWords ? Array.from({ length: headerWords },
      (_, i) => decoded.readUInt32LE(i * 4)) : null;
    assets[webRole] = {
      url: `world/objects/${name}-sha256-${hash}${ext}`,
      encoding: role === 'labelAnchors' ? 'identity' : 'gzip',
      compressedBytes: bytes.length, decodedBytes: decoded.length, sha256: hash,
      ...(header ? { header } : {}),
    };
  }
  const bundle = {
    schema: 'pandolab-world-bundle', schemaVersion: 1,
    source: { countryCount: 258, sha256: 'a'.repeat(64) },
    assets,
  };
  const raw = Buffer.from(JSON.stringify(bundle));
  return { bundle, raw, buffers };
}

test('v2 manifest is the only source of native file paths, and every blob is unchanged', () => {
  assert.equal(validateWorldDataset(initial), initial);
  const verified = verifyWorldFiles(root, initial);
  assert.equal(verified.length, WORLD_ROLES.length);
  assert.equal(initial.origin.mode, 'mixed-pinned');
  assert.equal(initial.origin.webCommit, null);
  assert.equal(initial.labelAnchors.bytes, 13826);
});

test('corrupt source hashes, required policy and path traversal all fail closed', () => {
  for (const path of ['../hidden', '/absolute', 'a/../b', 'a\\b', 'a//b', 'a%2fb', 'a?query']) {
    assert.equal(safeRelativePath(path), false);
  }
  const invalid = structuredClone(initial);
  invalid.countryCanonical.sha256 = '0'.repeat(64);
  assert.throws(() => validateWorldAssetBytes(invalid, 'countryCanonical',
    readFileSync(join(root, initial.countryCanonical.path))), /SHA-256 mismatch/);
  const unsafe = structuredClone(initial);
  unsafe.previewMesh.path = '../escape';
  assert.throws(() => validateWorldDataset(unsafe), /Unsafe/);
  const mixed = structuredClone(initial);
  mixed.origin.mode = 'web-bundle';
  assert.throws(() => validateWorldDataset(mixed), /Approved source/);
  const policy = structuredClone(initial);
  policy.countryCanonical.required = false;
  assert.throws(() => validateWorldDataset(policy), /required-asset/);
});

test('an approved upstream bundle is independent of the native legacy versioned filenames', () => {
  const { bundle, raw, buffers } = previewBundle();
  validateWebWorldBundle(bundle);
  const plan = buildWorldSyncPlan({
    current: initial, webRef: commit, webBundleBytes: raw, webBundle: bundle,
    downloaded: buffers,
  });
  assert.equal(plan.changed.length, 5);
  assert.equal(plan.candidate.origin.mode, 'web-bundle');
  assert.equal(plan.candidate.origin.worldBundleSha256, sha256(raw));
  assert.equal(plan.candidate.countryCanonical.source.ref, commit);
  assert.equal(plan.candidate.countryCanonical.source.path.startsWith('assets/data/world/objects/'), true);
  assert.equal(plan.candidate.hydro.source.ref, initial.hydro.source.ref);
  assert.equal(plan.candidate.canonicalPositionCount, 548454);
});

test('source changes and altered compressed assets are not accepted without validation', () => {
  const { bundle, raw, buffers } = previewBundle();
  const corrupted = new Map(buffers);
  corrupted.set('previewMesh', Buffer.from([1, 2, 3]));
  assert.throws(() => buildWorldSyncPlan({
    current: initial, webRef: commit, webBundleBytes: raw, webBundle: bundle,
    downloaded: corrupted,
  }), /stored byte mismatch/);
  const traversal = structuredClone(bundle);
  traversal.assets.previewCountries.url = 'world/objects/../bad';
  assert.throws(() => validateWebWorldBundle(traversal), /content address/);
  assert.throws(() => sourceReader({ webRef: 'not-a-commit' }), /full 40-character/);
});

test('sync downloads only changed upstream bytes and reuses verified local objects', async () => {
  const { bundle, buffers } = previewBundle();
  const rawPreview = gunzipSync(buffers.get('countryPreview'));
  const differentCompression = gzipSync(rawPreview, { level: 1 });
  assert.notEqual(sha256(differentCompression), sha256(buffers.get('countryPreview')));
  const previewSpec = bundle.assets.previewCountries;
  previewSpec.sha256 = sha256(differentCompression);
  previewSpec.compressedBytes = differentCompression.length;
  previewSpec.url = `world/objects/countries-preview-sha256-${previewSpec.sha256}.geojson.gz`;
  const raw = Buffer.from(JSON.stringify(bundle));
  const upstreamPath = `assets/data/${previewSpec.url}`;
  const requests = [];
  const reader = async path => {
    requests.push(path);
    if (path === 'assets/data/world/current.json') return raw;
    if (path === upstreamPath) return differentCompression;
    for (const role of ['terrain', 'hydro']) {
      if (path === initial[role].source.path) {
        return readFileSync(join(root, initial[role].path));
      }
    }
    throw new Error(`Unexpected extra upstream fetch: ${path}`);
  };
  const appRoot = fileURLToPath(new URL('../', import.meta.url));
  const plan = await planWorldSync({ appRoot, webRef: commit, reader });
  assert.deepEqual(plan.downloadedRoles, ['countryPreview']);
  assert.deepEqual(new Set(plan.reusedRoles),
    new Set(['previewMesh', 'countryCanonical', 'canonicalMesh', 'labelAnchors']));
  assert.equal(plan.physicalDrift.length, 0);
  assert.equal(plan.changed.length, 5);
  assert.deepEqual(requests, [
    'assets/data/world/current.json', upstreamPath,
    initial.terrain.source.path, initial.hydro.source.path,
  ]);
});

test('approval is required; physical dataset drift is blocked; approved bundle is atomic and reusable', () => {
  const { bundle, raw, buffers } = previewBundle();
  const plan = buildWorldSyncPlan({
    current: initial, webRef: commit, webBundleBytes: raw, webBundle: bundle,
    downloaded: buffers,
  });
  const appRoot = mkdtempSync(join(tmpdir(), 'pando-sync-v2-'));
  const worldRoot = join(appRoot, 'assets/world');
  try {
    mkdirSync(worldRoot, { recursive: true });
    writeFileSync(join(worldRoot, 'manifest.json'), currentBytes);
    for (const role of WORLD_ROLES) {
      const target = join(worldRoot, initial[role].path);
      mkdirSync(dirname(target), { recursive: true });
      writeFileSync(target, readFileSync(join(root, initial[role].path)));
    }
    assert.throws(() => applyWorldSyncPlan({
      appRoot, plan, approveBundle: '0'.repeat(64),
    }), /Explicit --approve-bundle/);
    assert.equal(readFileSync(join(worldRoot, 'manifest.json'), 'utf8'),
      currentBytes.toString('utf8'));
    assert.throws(() => applyWorldSyncPlan({
      appRoot, plan: { ...plan, physicalDrift: ['hydro'] },
      approveBundle: plan.webBundleSha256,
    }), /Physical inventory is pinned/);
    const outcome = applyWorldSyncPlan({
      appRoot, plan, approveBundle: plan.webBundleSha256,
    });
    assert.equal(outcome.assetsAdded, 5);
    assert.equal(verifyWorldFiles(worldRoot, plan.candidate).length, 7);
    assert.equal(existsSync(join(worldRoot, initial.countryCanonical.path)), true);
    const repeat = applyWorldSyncPlan({
      appRoot, plan, approveBundle: plan.webBundleSha256,
    });
    assert.equal(repeat.assetsAdded, 0);
    const otherApproved = structuredClone(plan.candidate);
    otherApproved.origin.worldBundleSha256 = 'b'.repeat(64);
    writeFileSync(join(worldRoot, 'manifest.json'), JSON.stringify(otherApproved, null, 2));
    assert.throws(() => applyWorldSyncPlan({
      appRoot, plan, approveBundle: plan.webBundleSha256,
    }), /Native world manifest changed/);
  } finally {
    rmSync(appRoot, { recursive: true, force: true });
  }
});
