import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

export const SOURCE_REPOSITORY = 'kimjeon-il/Pando';
export const DATASET_SCHEMA = 'pandoeditor-world-dataset';
export const DATASET_VERSION = 2;
export const WORLD_ROLES = Object.freeze([
  'countryPreview', 'previewMesh', 'countryCanonical', 'canonicalMesh',
  'terrain', 'hydro', 'labelAnchors',
]);
export const OPTIONAL_ROLES = Object.freeze(['countryPreview', 'terrain', 'hydro']);
const COMMIT_RE = /^[a-f0-9]{40}$/;
const SHA_RE = /^[a-f0-9]{64}$/;
const SEMVER_RE = /^\d+\.\d+\.\d+$/;
const REQUIRED = new Set(WORLD_ROLES.filter(name => !OPTIONAL_ROLES.includes(name)));

function ensure(value, message) {
  if (!value) throw new Error(message);
}

export function sha256(bytes) {
  return createHash('sha256').update(bytes).digest('hex');
}
export function gitBlobSha(bytes) {
  return createHash('sha1').update(Buffer.concat([
    Buffer.from(`blob ${bytes.length}\0`), bytes,
  ])).digest('hex');
}

export function safeRelativePath(value) {
  return typeof value === 'string' && value.length > 0 && value.length <= 512
    && !value.startsWith('/') && !value.includes('\\')
    && !/[?#%:\0]/.test(value)
    && value.split('/').every(part => part && part !== '.' && part !== '..');
}

export function validateWorldDataset(manifest) {
  ensure(manifest && typeof manifest === 'object' && !Array.isArray(manifest)
    && manifest.schema === DATASET_SCHEMA && manifest.version === DATASET_VERSION,
  'Unexpected native world manifest schema/version');
  ensure(manifest.countryCount === 258 && manifest.canonicalPositionCount === 548454,
    'Unsupported canonical country envelope');
  const origin = manifest.origin;
  ensure(origin?.repository === SOURCE_REPOSITORY
    && ['mixed-pinned', 'web-bundle'].includes(origin.mode), 'Unknown data origin');
  if (origin.mode === 'mixed-pinned') {
    ensure(origin.webCommit === null && origin.worldBundleSha256 === null,
      'Mixed source cannot pretend to pin an upstream bundle');
  } else {
    ensure(COMMIT_RE.test(origin.webCommit || '') && SHA_RE.test(origin.worldBundleSha256 || ''),
      'Approved source must name exact commit and bundle SHA-256');
  }
  const seenPaths = new Set();
  for (const role of WORLD_ROLES) {
    const entry = manifest[role];
    ensure(entry && typeof entry === 'object' && !Array.isArray(entry),
      `Missing native world asset ${role}`);
    ensure(safeRelativePath(entry.path) && !seenPaths.has(entry.path),
      `Unsafe or duplicate asset path: ${role}`);
    seenPaths.add(entry.path);
    ensure(SEMVER_RE.test(entry.version || '') && COMMIT_RE.test(entry.gitBlobSha || '')
      && SHA_RE.test(entry.sha256 || ''), `Invalid native asset identity: ${role}`);
    ensure(Number.isSafeInteger(entry.bytes) && entry.bytes > 0 && entry.bytes < 100_000_000,
      `Invalid native asset byte length: ${role}`);
    ensure(entry.required === REQUIRED.has(role),
      `Invalid native required-asset contract: ${role}`);
    ensure(entry.source && COMMIT_RE.test(entry.source.ref || '')
      && safeRelativePath(entry.source.path)
      && entry.source.path.startsWith('assets/data/'),
    `Invalid source provenance for ${role}`);
    if (origin.mode === 'web-bundle' && !['hydro', 'terrain'].includes(role)) {
      ensure(entry.source.ref === origin.webCommit, `Mixed commit in approved world bundle: ${role}`);
    }
    if (role === 'countryPreview' || role === 'countryCanonical'
      || role === 'previewMesh' || role === 'canonicalMesh') {
      ensure(entry.path.endsWith(role === 'countryPreview' ? '.geojson.gz'
        : role === 'countryCanonical' ? '.pcg.gz' : '.bin.gz'),
      `Unexpected world geometry asset format: ${role}`);
    }
  }
  ensure(Object.keys(manifest).every(key => [
    'schema', 'version', 'countryCount', 'canonicalPositionCount', 'origin',
    ...WORLD_ROLES,
  ].includes(key)), 'Unexpected native world manifest fields');
  return manifest;
}

export function validateWorldAssetBytes(manifest, role, bytes) {
  validateWorldDataset(manifest);
  ensure(WORLD_ROLES.includes(role), `Unknown native asset: ${role}`);
  const asset = manifest[role];
  ensure(Buffer.isBuffer(bytes) && bytes.length === asset.bytes,
    `Wrong byte count: ${role}`);
  ensure(sha256(bytes) === asset.sha256, `SHA-256 mismatch: ${role}`);
  ensure(gitBlobSha(bytes) === asset.gitBlobSha, `Git blob mismatch: ${role}`);
  return true;
}

export function verifyWorldFiles(root, manifest) {
  validateWorldDataset(manifest);
  const files = [];
  for (const role of WORLD_ROLES) {
    const entry = manifest[role];
    const bytes = readFileSync(resolve(root, entry.path));
    validateWorldAssetBytes(manifest, role, bytes);
    files.push({ role, path: entry.path, bytes: entry.bytes, sha256: entry.sha256 });
  }
  return files;
}
