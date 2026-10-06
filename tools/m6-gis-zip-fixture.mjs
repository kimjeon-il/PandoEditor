import { readFileSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { runInNewContext } from 'node:vm';

const root = new URL('../tests/fixtures/web-m6/source/', import.meta.url);
const pin = JSON.parse(readFileSync(new URL('../tests/fixtures/web-m6/manifest.json', import.meta.url)));
function storeFixture(name, bytes) {
  const path = new URL('../tests/fixtures/web-m6/' + name, import.meta.url);
  if (process.argv.includes('--verify')) {
    // ZIP timestamps and deflate streams can differ across Node runtimes.
    // The web contract is the archive members and their exact decoded bytes.
    const expected = context.fflate.unzipSync(readFileSync(path));
    const actual = context.fflate.unzipSync(bytes);
    const paths = Object.keys(expected).sort();
    if (JSON.stringify(paths) !== JSON.stringify(Object.keys(actual).sort()) ||
        paths.some(key => !Buffer.from(expected[key]).equals(Buffer.from(actual[key])))) {
      throw new Error('Web GIS ZIP fixture contents changed: ' + name);
    }
  } else writeFileSync(path, bytes);
}
for (const name of ['gis-io.js', 'fflate.min.js', 'gis-adapters.js']) {
  const body = readFileSync(new URL(name, root));
  const sha = createHash('sha1').update('blob ' + body.length + '\0').update(body).digest('hex');
  if (sha !== pin.sourceBlobs[name]) throw new Error('Web GIS source SHA changed: ' + name);
}
const context = {
  URL, Blob, TextDecoder, TextEncoder, structuredClone, console,
  Date: class extends Date {
    constructor(...args) { super(...(args.length ? args : ['2026-01-01T00:00:00.000Z'])); }
    static now() { return 1767225600000; }
  },
  location: { href: 'https://example.invalid/' },
  document: { currentScript: { src: 'https://example.invalid/assets/js/gis-io.js' } },
};
context.window = context;
context.self = context;
for (const name of ['fflate.min.js', 'gis-adapters.js', 'gis-io.js']) {
  runInNewContext(readFileSync(new URL(name, root), 'utf8'), context, { filename: name });
}

const square = x => ({ type: 'Polygon', coordinates: [[[x, 0], [x + 2, 0], [x + 2, 2], [x, 2], [x, 0]]] });
const state = {
  countriesData: { features: [{ type: 'Feature', id: 'AAA', properties: { name: 'Alpha' }, geometry: square(0) }] },
  countryOverrides: {},
  territorialUnits: [{
    id: 'sub:1', geometry: square(0),
    properties: { unitType: 'subunit', name: 'Subdivision', sovereignId: 'AAA', parentId: 'AAA',
      sourceEntityId: 'history:1', style: { color: '#123456' } },
  }],
  genericFeatures: [{ type: 'Feature', id: 'generic:1', properties: { name: 'Generic' },
    geometry: { type: 'Point', coordinates: [1, 1] } }],
  distributionLayers: [{ id: 'lang:1', type: 'language', name: 'Language', color: '#0088ff' }],
  distributionEntries: [{ id: 'entry:1', layerId: 'lang:1', mode: 'territorial',
    territorialUnitId: 'AAA', share: 60, certainty: 'known' }],
  labels: [{ id: 'label:1', name: 'Capital', kind: 'custom', coordinates: [1, 1] }],
};
const categories = ['countries', 'subunits', 'genericFeatures', 'distributions', 'labels'];
const { blob, manifest } = await context.PandoLabGIS.exportGeoJsonBundle(state, categories);
const bytes = Buffer.from(await blob.arrayBuffer());
storeFixture('web-gis-geojson.zip', bytes);
const files = context.fflate.unzipSync(bytes);
const variants = [
  ['bad-schema.zip', { ...manifest, schemaVersion: 4 }],
  ['bad-crs.zip', { ...manifest, crs: 'EPSG:3857' }],
  ['missing-layer.zip', { ...manifest, layers: [{ ...manifest.layers[0], file: 'missing.geojson' }] }],
  ['wrong-count.zip', { ...manifest, layers: [{ ...manifest.layers[0], featureCount: 2 }] }],
];
for (const [name, value] of variants) {
  storeFixture(name, Buffer.from(context.fflate.zipSync({
    ...files, 'manifest.json': context.fflate.strToU8(JSON.stringify(value)),
  }, { level: 6 })));
}
storeFixture('plain-geojson.zip',
  Buffer.from(context.fflate.zipSync({ 'plain.geojson': files['countries.geojson'] }, { level: 6 })));
console.log(JSON.stringify({ size: bytes.length, manifest }));
