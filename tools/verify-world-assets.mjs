import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { dirname, join, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(process.argv[2] || join(here, '../assets/world'));
const expected = Object.freeze({
  countryPreview: ['countries-preview-v0.33.0.geojson.gz', '0.33.0', 'ce5eecd8f4e1a611131f2117780c34ca3cf555b2'],
  previewMesh: ['world-mesh-preview-v0.33.0.bin.gz', '0.33.0', 'caff1ab319706f6bf90635ed5a992ffbf4f2a84a'],
  countryCanonical: ['countries-canonical-v0.33.0.pcg.gz', '0.33.0', '54146d9eeb28e4af08e094f5061bf64689e6bdf3'],
  canonicalMesh: ['world-mesh-v0.12.6.bin.gz', '0.12.6', '8c73420b92e89ab64cbe75dcc5016efe2c0a22b6'],
  terrain: ['terrain/v0.12.6/manifest.json', '0.12.6', '6821c49315ffd381758f81dfe4d83b6b574f0ad4'],
  hydro: ['hydro/v0.13.1/manifest.json', '0.13.1', '9a9cdb719351e51d2ff509c410cae8741ac36a0a'],
});
function digest(algorithm, bytes) { return createHash(algorithm).update(bytes).digest('hex'); }
function gitBlob(bytes) {
  return digest('sha1', Buffer.concat([Buffer.from(`blob ${bytes.length}\0`), bytes]));
}
function requireValue(condition, message) { if (!condition) throw Error(message); }

try {
  const manifest = JSON.parse(readFileSync(join(root, 'manifest.json'), 'utf8'));
  requireValue(manifest.schema === 'pandoeditor-world-dataset' && manifest.version === 1,
    'Unexpected world manifest schema/version');
  requireValue(manifest.worldMapCommit === 'c0bd31d13dc8495593d78cf51f7cc195de7c9469',
    'Wrong world-map source commit');
  requireValue(manifest.countryCount === 258 && manifest.canonicalPositionCount === 548454,
    'Wrong canonical country envelope');
  for (const [key, [path, version, sha]] of Object.entries(expected)) {
    const asset = manifest[key];
    requireValue(asset && asset.path === path && asset.version === version && asset.gitBlobSha === sha,
      `Wrong ${key} identity`);
    requireValue(asset.required === !['countryPreview', 'terrain', 'hydro'].includes(key),
      `Wrong ${key} required policy`);
    requireValue(!path.split('/').includes('..') && !path.startsWith(sep), 'Unsafe asset path');
    const bytes = readFileSync(join(root, path));
    requireValue(gitBlob(bytes) === sha, `Git blob mismatch: ${key}`);
    requireValue(digest('sha256', bytes) === asset.sha256, `SHA-256 mismatch: ${key}`);
  }
  console.log(`World assets: 258 countries, pinned world-map ${manifest.worldMapCommit}, all six identities match`);
} catch (error) {
  console.error(`World asset verification failed: ${error.message}`);
  process.exitCode = 1;
}
