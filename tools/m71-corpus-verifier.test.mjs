import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {linkSync, mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {test} from 'node:test';
import {gitBlobSha, readManifest, verifyCommittedCorpus} from './verify-m71-world-corpus.mjs';

const fixture = new URL('../tests/fixtures/world-rendering/manifest.json', import.meta.url);
const original = JSON.parse(readFileSync(fixture, 'utf8'));
function sandbox(change = () => {}) {
  const root = mkdtempSync(join(tmpdir(), 'm71-verifier-'));
  const manifest = structuredClone(original);
  change(manifest);
  writeFileSync(join(root, 'manifest.json'), JSON.stringify(manifest));
  return {root, manifest, close: () => rmSync(root, {recursive: true, force: true})};
}

test('Git blob hashing uses the raw byte length and blob header', () => {
  assert.equal(gitBlobSha(Buffer.from('hello\n')), 'ce013625030ba8dba906f756967f9e9ca394464a');
});

test('pinned manifest reads but missing generated fixtures fail', () => {
  const s = sandbox();
  try {
    assert.equal(readManifest(s.root).countryIds.length, 13);
    assert.throws(() => verifyCommittedCorpus(s.root), /missing fixture: countries.geojson/);
  } finally { s.close(); }
});

for (const [label, edit] of [
  ['schema', m => { m.schema = 'other'; }],
  ['version', m => { m.version = 2; }],
  ['world-map commit', m => { m.worldMapCommit = '0'.repeat(40); }],
  ['country order', m => { m.countryIds.reverse(); }],
  ['country source', m => { delete m.sources.countries.gitBlobSha; }],
  ['canonical packet source', m => { delete m.sources.canonicalPacket; }],
  ['both synthetic fixture identities', m => { delete m.fixtureHashes['sentinels.geojson']; delete m.fixtureHashes['composition.json']; }],
  ['country metadata', m => { delete m.countries; }],
  ['hydro metadata', m => { delete m.hydro; }],
  ['aggregate metadata', m => { delete m.aggregate; }],
]) {
  test(`rejects changed ${label}`, () => {
    const s = sandbox(edit);
    try { assert.throws(() => readManifest(s.root), /manifest|source|country|hydro|aggregate/i); }
    finally { s.close(); }
  });
}

test('rejects a fixture whose bytes differ from its registered SHA-256', () => {
  const s = sandbox(m => {
    m.fixtureHashes['countries.geojson'] = createHash('sha256').update('expected').digest('hex');
  });
  try {
    writeFileSync(join(s.root, 'countries.geojson'), 'changed');
    assert.throws(() => verifyCommittedCorpus(s.root), /fixture hash mismatch: countries.geojson/);
  } finally { s.close(); }
});

function sceneSandbox(sentinels, composition) {
  const s = sandbox();
  for (const name of ['countries.geojson', 'hydro-river.geojson', 'hydro-lake.geojson']) {
    linkSync(new URL(`../tests/fixtures/world-rendering/${name}`, import.meta.url).pathname, join(s.root, name));
  }
  for (const [name, value] of Object.entries({'sentinels.geojson': sentinels, 'composition.json': composition})) {
    const bytes = JSON.stringify(value) + '\n';
    writeFileSync(join(s.root, name), bytes);
    s.manifest.fixtureHashes[name] = createHash('sha256').update(bytes).digest('hex');
  }
  writeFileSync(join(s.root, 'manifest.json'), JSON.stringify(s.manifest));
  return s;
}

const sentinel = (id, synthetic = true, lon = 179) => ({type: 'Feature', id,
  properties: {synthetic, purpose: 'test'}, geometry: {type: 'Polygon', coordinates: [
    [[lon, 70], [-179, 70], [lon, 70]],
  ]}});
const realSentinels = JSON.parse(readFileSync(new URL('../tests/fixtures/world-rendering/sentinels.geojson', import.meta.url))).features;
const tinyPolygon = {type: 'Polygon', coordinates: [[[0, 0], [1, 0], [0, 0]]]};
const validScene = {schema: 'pandoeditor-m71-composition', version: 1,
  controlCountryId: 'DEU', subunit: {id: 'sub', parent: 'DEU', sovereign: 'DEU', geometry: tinyPolygon},
  region: {id: 'region', parent: 'sub', sovereign: 'DEU', geometry: tinyPolygon}, distributions: [], generic: [], labels: [],
  hydroRefs: ['hydro-river.geojson', 'hydro-lake.geojson']};

for (const [label, sentinels, composition, message] of [
  ['duplicate sentinel ID', [sentinel('DATELINE'), sentinel('DATELINE')], validScene, /sentinel.*unique/i],
  ['unmarked synthetic', [sentinel('DATELINE', false), sentinel('POLAR')], validScene, /synthetic/i],
  ['invalid coordinate', [sentinel('DATELINE', true, null), sentinel('POLAR')], validScene, /coordinate/i],
  ['bad composition schema', realSentinels, {...validScene, schema: 'wrong'}, /composition schema/i],
  ['unresolved relation', realSentinels, {...validScene, region: {...validScene.region, parent: 'absent'}}, /reference/i],
]) {
  test(`rejects ${label} even with matching fixture hashes`, () => {
    const s = sceneSandbox({type: 'FeatureCollection', features: sentinels}, composition);
    try { assert.throws(() => verifyCommittedCorpus(s.root), message); }
    finally { s.close(); }
  });
}

test('rejects altered country geometry statistics even when fixture bytes still match', () => {
  const s = sceneSandbox({type: 'FeatureCollection', features: [sentinel('DATELINE'), sentinel('POLAR')]}, validScene);
  try {
    s.manifest.countries[0].coordinateCount++;
    writeFileSync(join(s.root, 'manifest.json'), JSON.stringify(s.manifest));
    assert.throws(() => verifyCommittedCorpus(s.root), /country geometry stats mismatch/i);
  } finally { s.close(); }
});

test('rejects an altered synthetic dateline ring even when the new file hash matches', () => {
  const real = JSON.parse(readFileSync(new URL('../tests/fixtures/world-rendering/sentinels.geojson', import.meta.url)));
  real.features[0].geometry.coordinates[0][0][0] = 178;
  const scene = JSON.parse(readFileSync(new URL('../tests/fixtures/world-rendering/composition.json', import.meta.url)));
  const s = sceneSandbox(real, scene);
  try { assert.throws(() => verifyCommittedCorpus(s.root), /sentinel coordinate contract/i); }
  finally { s.close(); }
});

test('rejects altered country risk tags with otherwise intact fixture files', () => {
  const real = JSON.parse(readFileSync(new URL('../tests/fixtures/world-rendering/sentinels.geojson', import.meta.url)));
  const scene = JSON.parse(readFileSync(new URL('../tests/fixtures/world-rendering/composition.json', import.meta.url)));
  const s = sceneSandbox(real, scene);
  try {
    s.manifest.countries[1].riskTags = ['control'];
    writeFileSync(join(s.root, 'manifest.json'), JSON.stringify(s.manifest));
    assert.throws(() => verifyCommittedCorpus(s.root), /country risk tags mismatch/i);
  } finally { s.close(); }
});
