import { readFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { dirname, resolve } from 'node:path';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../tests/fixtures/web-m75');
const manifest = JSON.parse(await readFile(resolve(root, 'manifest.json'), 'utf8'));
if (manifest.schema !== 'pandoeditor-m75-web-quality' || manifest.version !== 1 ||
    manifest.worldMapCommit !== 'c0bd31d13dc8495593d78cf51f7cc195de7c9469')
  throw new Error('M7.5 web contract identity mismatch');
for (const [name, expected] of Object.entries(manifest.sources)) {
  const bytes = await readFile(resolve(root, 'source', name));
  const blob = createHash('sha1').update(`blob ${bytes.length}\0`).update(bytes).digest('hex');
  if (blob !== expected) throw new Error(`Pinned web source mismatch: ${name}`);
}
const source = name => pathToFileURL(resolve(root, 'source', name)).href;
const lod = await import(source('render-lod.js'));
const quality = await import(source('adaptive-render-quality.js'));
const countries = await import(source('gpu-country-ranges.js'));
const line = { type: 'LineString', coordinates: [[0, 0], [.01, .001], [10, 0]] };
const dateline = { type: 'LineString', coordinates: [[179, 0], [-179, 0]] };
const polygon = { type: 'Polygon', coordinates: [
  [[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]],
  [[2, 2], [4, 2], [4, 4], [2, 4], [2, 2]],
] };
const mesh = {
  triangleIndices: new Uint32Array([0, 1, 2, 3, 4, 5]),
  lineIndices: new Uint32Array([0, 1, 3, 4]),
  countryTriangleRanges: new Uint32Array([0, 3, 3, 3]),
  countryBoundaryRanges: new Uint32Array([0, 2, 2, 2]),
  countryBounds: new Int32Array([0, 0, 5000000, 5000000, 100000000, 0, 105000000, 5000000]),
  countryBoundsFlags: new Uint32Array([0, 0]), metadataCountryIds: ['A', 'B'],
};
const frame = { mode: 1, cssViewport: [800, 600], cssTranslate: [400, 300],
  cssScale: 400, flatCenter: [0, 0], worldOffsets: [0] };
let now = 0;
const controller = quality.createAdaptiveRenderQualityController({ now: () => now });
for (let i = 0; i < 40; i++) { now += 20; controller.recordFrame(40); }
const degraded = controller.profile();
controller.beginInteraction();
const interaction = controller.profile();
controller.endInteraction();
for (let i = 0; i < 240; i++) { now += 20; controller.recordFrame(5); }
const settled = controller.profile();
const result = {
  schema: 'pandoeditor-m75-web-quality-oracle', version: 1,
  worldMapCommit: manifest.worldMapCommit,
  lod: {
    exact: lod.resolveRenderLod({ requested: 'coarse', policy: 'exact' }),
    protected: lod.resolveRenderLod({ requested: 'coarse', policy: 'independent', protected: true }),
    independent: lod.resolveRenderLod({ requested: 'coarse', policy: 'independent' }),
    coarseLine: lod.simplifyRenderGeometry(line, { lod: 'coarse', policy: 'independent' }),
    dateline: lod.simplifyRenderGeometry(dateline, { lod: 'coarse', policy: 'independent' }),
    globeLine: lod.simplifyRenderGeometry(line, { lod: 'coarse', policy: 'independent', projection: 'globe' }),
    polygon: lod.simplifyRenderGeometry(polygon, { lod: 'coarse', policy: 'independent' }),
  },
  culling: countries.countryDrawRangesForFrame(mesh, frame),
  quality: {
    desktop: quality.resolveInitialRenderQuality(),
    mobile: quality.resolveInitialRenderQuality({ mobile: true }),
    coarse: quality.renderQualityProfile('coarse'),
    medium: quality.renderQualityProfile('medium', { mobile: true, phase: 'interaction' }),
    degradedTier: degraded.tier, interactionRevision: interaction.revision,
    settledTier: settled.tier,
  },
};
process.stdout.write(`${JSON.stringify(result, null, 2)}\n`);
