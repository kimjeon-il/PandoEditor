import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

export const WEB_COMMIT = 'ebcfae4d27b29cbbea6416a7045a4806930204be';
const MANIFEST_SHA256 = '852af3bf7f59cab73a44862c91eb1d60e9db45f8e0ee561f9a265a702b93721c';
const SOURCE_MANIFEST_SHA256 = 'd6a128f9f9a9ce6b81590d0c768a5f3d2911906eec537d7e1abf7b1db69aebdd';
const defaultFixtureRoot = fileURLToPath(new URL('../tests/fixtures/web-v10-exchange/', import.meta.url));
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const json = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const kinds = ['static', 'complex', 'calendar-boundaries'];

function containedFile(root, relative) {
  assert.equal(typeof relative, 'string');
  assert.ok(relative && !relative.includes('\\') && !relative.split('/').includes('..') && !path.isAbsolute(relative));
  const file = path.resolve(root, relative), base = path.resolve(root);
  assert.ok(file.startsWith(base + path.sep), `snapshot path escape: ${relative}`);
  return file;
}

function verifyRows(root, rows) {
  const seen = new Set();
  for (const row of rows) {
    assert.ok(!seen.has(row.file), `duplicate snapshot file: ${row.file}`); seen.add(row.file);
    const bytes = fs.readFileSync(containedFile(root, row.file));
    assert.equal(bytes.length, row.bytes, `${row.file}: exact bytes`);
    assert.equal(hash(bytes), row.sha256, `${row.file}: SHA256`);
    assert.equal(createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`), bytes])).digest('hex'), row.gitBlob, `${row.file}: Git blob`);
  }
}

export function verifyFixtureSnapshot(fixtureRoot = defaultFixtureRoot) {
  const manifestBytes = fs.readFileSync(path.join(fixtureRoot, 'manifest.json'));
  const sourceBytes = fs.readFileSync(path.join(fixtureRoot, 'source-manifest.json'));
  assert.equal(hash(manifestBytes), MANIFEST_SHA256, 'immutable independent fixture manifest');
  assert.equal(hash(sourceBytes), SOURCE_MANIFEST_SHA256, 'immutable fixed production source manifest');
  const manifest = JSON.parse(manifestBytes), source = JSON.parse(sourceBytes);
  assert.equal(manifest.sourceCommit, WEB_COMMIT); assert.equal(source.sourceCommit, WEB_COMMIT);
  assert.equal(manifest.projectSchemaVersion, 10); assert.equal(manifest.territorialModelSchemaVersion, 6);
  assert.deepEqual(manifest.cases, kinds); assert.equal(manifest.fileCount, 9); assert.equal(source.fileCount, 30);
  assert.equal(sourceBytes.length, manifest.sourceManifest.bytes);
  assert.equal(hash(sourceBytes), manifest.sourceManifest.sha256);
  verifyRows(fixtureRoot, manifest.files); verifyRows(fixtureRoot, source.sources);
  verifyRows(fixtureRoot, [manifest.upstreamReadme]);
  return { sourceCommit: WEB_COMMIT, inputFileCount: manifest.files.length, sourceFileCount: source.sources.length,
    inputBytes: manifest.totalBytes, sourceBytes: source.totalBytes, manifestSha256: MANIFEST_SHA256,
    sourceManifestSha256: SOURCE_MANIFEST_SHA256, sqlWasmSha256: source.sqlWasm.sha256 };
}

function rowsByIdentity(rows, archive = false) {
  assert.ok(Array.isArray(rows));
  const result = {};
  for (const row of rows) {
    const key = JSON.stringify(archive ? [row.id, row.version] : [row.id]);
    assert.ok(!Object.hasOwn(result, key), `duplicate identity ${key}`);
    Object.defineProperty(result, key, { value: row, enumerable: true });
  }
  return result;
}

/** Existing exchange contract: only identity/archive/record row order is immaterial. */
export function compareProject(actual, expected, stage) {
  assert.equal(actual.format, 'pandolab-project-state', `${stage}: project format`);
  assert.equal(actual.schemaVersion, 10, `${stage}: Web10 header`);
  assert.equal(actual.territorialModel.schemaVersion, 6, `${stage}: model6 header`);
  assert.deepEqual(rowsByIdentity(actual.territorialEntities), rowsByIdentity(expected.territorialEntities), `${stage}: exact identities/metadata/sourceEntityId`);
  assert.equal(actual.timelineRecords.schemaVersion, expected.timelineRecords.schemaVersion, `${stage}: timeline schema`);
  for (const key of ['lifetimes', 'geometryBindings', 'parentRelations'])
    assert.deepEqual(rowsByIdentity(actual.timelineRecords[key]), rowsByIdentity(expected.timelineRecords[key]), `${stage}: ${key}`);
  assert.deepEqual(rowsByIdentity(actual.geometries, true), rowsByIdentity(expected.geometries, true), `${stage}: complete archive`);
  return actual;
}

export function compareFullWebProject(actual, fixed, stage) {
  compareProject(actual, fixed, stage);
  const collections = ['territorialEntities', 'timelineRecords', 'geometries'];
  assert.deepEqual(Object.keys(actual).sort(), Object.keys(fixed).sort(), `${stage}: complete Web fields`);
  for (const key of Object.keys(fixed).filter(key => !collections.includes(key)))
    assert.deepEqual(actual[key], fixed[key], `${stage}: retained project field ${key}`);
  return actual;
}

export function checkWeb(api, project, expected, fixed, label, record = () => {}) {
  const inspect = (stage, action) => {
    try { const value = action(); record({ stage, passed: true }); return value; }
    catch (error) { record({ stage, passed: false, error: error.message }); throw error; }
  };
  // Production readers normalize text and dates. Inspect the original output
  // first so that normalization cannot conceal loss at any file boundary.
  inspect(label+'.raw', () => {
    compareProject(project, expected, label+'.raw');
    compareFullWebProject(project, fixed, label+'.raw');
  });
  const stored = inspect(label+'.reader', () => api.prepareProjectForStorage(project));
  inspect(label+'.prepared', () => {
    compareProject(stored, expected, label+'.prepared');
    compareFullWebProject(stored, fixed, label+'.prepared');
  });
  return stored;
}

// Check production-native fields against independent Web bytes. This is a
// checker only: no native expected file or replacement codec is produced.
export function compareNativeStorage(actual, fixed, stage) {
  assert.equal(actual.format, 'pandoeditor-project', `${stage}: native format`);
  assert.equal(actual.version, 10, `${stage}: native10 header`);
  const units = rowsByIdentity(actual.units), identities = rowsByIdentity(fixed.territorialEntities);
  assert.deepEqual(Object.keys(units).sort(), Object.keys(identities).sort(), `${stage}: native identity count`);
  const capitals = [], symbols = [];
  for (const key of Object.keys(identities)) {
    const row = units[key], p = identities[key].properties, metadata = structuredClone(p.metadata);
    const ref = { domain: 'territorial', id: row.id };
    if (Object.hasOwn(metadata, 'capital')) { capitals.push({ ref, capital: metadata.capital }); delete metadata.capital; }
    if (Object.hasOwn(metadata, 'flagDataUrl')) {
      symbols.push({ ref, policy: metadata.flagDataUrl === null ? 'none' : 'embedded',
        embeddedDataUrl: metadata.flagDataUrl ?? '', defaultCountryId: '', defaultFlagDataUrl: null });
      delete metadata.flagDataUrl;
    }
    const nameSource = metadata.nameSource, libraryOrigin = metadata.libraryOrigin;
    delete metadata.nameSource; delete metadata.libraryOrigin;
    for (const [nativeKey, webKey] of [['kind','entityKind'], ['name','name'], ['notes','notes'], ['locked','locked'],
      ['sourceFolderId','sourceFolderId'], ['sourceEntityId','sourceEntityId'], ['sourceGeometryVersion','sourceGeometryVersion']])
      assert.deepEqual(row[nativeKey], p[webKey], `${stage}: ${row.id}.${nativeKey}`);
    assert.deepEqual(row.metadata, metadata, `${stage}: ${row.id} metadata owner`);
    assert.equal(row.baseName, nameSource?.baseName ?? '', `${stage}: ${row.id} baseName`);
    // Absent nameSource denotes the existing explicit nonempty name. All
    // immutable nine-file corpus names are nonempty; no empty-name coverage.
    assert.equal(row.nameExplicit, nameSource?.nameExplicit ?? true, `${stage}: ${row.id} nameExplicit`);
    assert.deepEqual(row.libraryOrigin, libraryOrigin ?? null, `${stage}: ${row.id} libraryOrigin`);
    const style = actual.presentation.objectStyles.territorial[row.id];
    assert.equal(style.color, p.style.color ?? null, `${stage}: ${row.id} color`);
    assert.equal(style.opacity, 1, `${stage}: ${row.id} opacity`);
  }
  const ownership = rows => {
    assert.ok(Array.isArray(rows), `${stage}: native ownership rows`);
    const result = {};
    for (const row of rows) {
      const key = JSON.stringify(row.ref);
      assert.ok(!Object.hasOwn(result, key), `${stage}: duplicate ownership ${key}`);
      Object.defineProperty(result, key, { value: row, enumerable: true });
    }
    return result;
  };
  assert.deepEqual(ownership(actual.content.countryDetails), ownership(capitals), `${stage}: capital moved to countryDetails`);
  assert.deepEqual(ownership(actual.content.symbols), ownership(symbols), `${stage}: flags moved to symbols`);
  assert.equal(actual.timelineRecords.schemaVersion, fixed.timelineRecords.schemaVersion, `${stage}: native timeline schema`);
  for (const key of ['lifetimes','geometryBindings','parentRelations'])
    assert.deepEqual(rowsByIdentity(actual.timelineRecords[key]), rowsByIdentity(fixed.timelineRecords[key]), `${stage}: native ${key}`);
  assert.deepEqual(rowsByIdentity(actual.geometries, true), rowsByIdentity(fixed.geometries, true), `${stage}: native complete archive`);
  for (const key of ['version','savedAt','baseDataset','landObjectModel','territorialModel','distributionModel',
    'sourceInfo','physicalSourceInfo','physicalSettings'])
    assert.deepEqual(actual.exchangeMetadata[key], fixed[key], `${stage}: native common ${key}`);
  // The nine immutable corpus files have empty inline content domains.
  // Refuse to silently claim coverage if this checker is given other content.
  for (const [nativeKey, webKey] of [['labels','labels'], ['hydro','hydroEdits'], ['genericFeatures','genericFeatures'],
    ['distributionLayers','distributionLayers'], ['distributionEntries','distributionEntries']]) {
    assert.deepEqual(fixed[webKey], [], `${stage}: unsupported additional inline corpus content`);
    assert.deepEqual(actual.content[nativeKey], [], `${stage}: native ${nativeKey}`);
  }
  assert.deepEqual(actual.extensions, [], `${stage}: unexpected native extensions`);
  return actual;
}

export const TRACE_FILES = Object.freeze(['read.web.json', 'saved.native.json', 'reopened.web.json',
  'saved.native.gpkg', 'package-reopened.web.json', 'export.web.json', 'activation.json', 'trace-status.json']);

async function loadWeb(root) {
  const load = relative => import(pathToFileURL(path.join(root, 'source', relative)));
  const reader = await load('assets/js/modules/project-state.js');
  const serializer = await load('assets/js/modules/project-serializer.js');
  const worker = await load('tests/helpers/production-geopackage.mjs');
  return { ...reader, ...serializer, ...worker };
}

function serialize(api, project) {
  const { territorialEntities, ...projectFields } = project;
  const physical = project.physicalSourceInfo;
  return api.createProjectSerializer({ appVersion: project.version, baseDataset: project.baseDataset,
    distributionModes: project.distributionModel.sourceModes, terrainDataset: physical.terrain.dataset,
    hydroDataset: physical.hydro.dataset, now: () => new Date(project.savedAt),
    readSnapshot: () => ({ territorialEntities, projectFields,
      terrainSourceInfo: physical.terrain, hydroManifest: physical.hydro }) }).buildProject();
}

function activation(api, project, kind) {
  if (kind === 'static') { api.prepareProjectForActivation(project); return 'OK'; }
  assert.throws(() => api.prepareProjectForActivation(project), { code: 'TIMELINE_ACTIVATION' });
  return 'TIMELINE_ACTIVATION';
}

const negativeCases = [
  ['year-zero', 'life:A', 'validFrom', '0000-02', 'TIMELINE_INTERVAL'],
  ['nonleap-1900', 'B:new', 'validFrom', '1900-02-29', 'TIMELINE_INTERVAL'],
  ['inclusive-overlap', 'A:new', 'validFrom', '+12000-02-28', 'TIMELINE_OVERLAP'],
  ['leap-day-gap', 'A:new', 'validFrom', '+12000-03', 'TIMELINE_GAP'],
];

function writeEvidence(directory, name, bytes) {
  if (!directory) return;
  fs.mkdirSync(directory, { recursive: true });
  const file = path.join(directory, name);
  fs.writeFileSync(file, bytes, { flag: 'wx' });
}
const encodedJson = value => JSON.stringify(value, null, 2) + '\n';

function runNative(probe, args, evidence, name, processes) {
  const started = Date.now();
  const result = spawnSync(probe, args, { encoding: 'utf8', timeout: 120000, maxBuffer: 16*1024*1024,
    env: { ...process.env, QT_QPA_PLATFORM: 'offscreen' } });
  const processRecord = { name, pid: result.pid, args, exitCode: result.status, signal: result.signal,
    error: result.error?.message ?? null, durationMs: Date.now()-started };
  processes.push(processRecord);
  writeEvidence(evidence, name+'.native.stdout.txt', result.stdout || '');
  writeEvidence(evidence, name+'.native.stderr.txt', result.stderr || '');
  if (result.error) throw result.error;
  return result;
}

async function runOracle({ fixtureRoot = defaultFixtureRoot, probe, evidenceDir } = {}) {
  const provenance = verifyFixtureSnapshot(fixtureRoot), api = await loadWeb(fixtureRoot);
  const nativeExecuted = Boolean(probe), processes = [], traces = [], actualCalls = [];
  if (nativeExecuted) {
    assert.ok(evidenceDir, 'native execution requires a separate evidence directory');
    assert.ok(fs.existsSync(probe), 'real native timeline_project_tests executable required');
  }
  if (evidenceDir) fs.mkdirSync(evidenceDir, { recursive: true });
  const run = evidenceDir ? fs.mkdtempSync(path.join(evidenceDir, 'web10-exchange-')) : null;
  const cases = [];
  let stages, stage;
  async function checked(label, action) {
    stage = label;
    try { const value = await action(); stages.push({ stage: label, passed: true }); return value; }
    catch (error) { stages.push({ stage: label, passed: false, error: error.message }); throw error; }
  }
  const check = (project, expected, fixed, label) => checkWeb(api, project, expected, fixed, label,
    result => { stage = result.stage; stages.push(result); });
  async function workerRead(file, name) {
    const bytes = fs.readFileSync(file);
    actualCalls.push({ name, stage: 'web.worker.read', file, inputFileSha256: hash(bytes) });
    return (await api.productionGeoPackage('read', bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset+bytes.byteLength))).metadata.projectState;
  }
  async function webRoundTrip(project, expected, fixed, name) {
    const serialized = await checked('web.serializer', () => check(serialize(api, project), expected, fixed, name+'.serializer'));
    const jsonFile = run && path.join(run, name+'.web.json'), gpkgFile = run && path.join(run, name+'.web.gpkg');
    writeEvidence(run, name+'.web.json', encodedJson(serialized));
    const packed = await checked('web.worker.write', async () => {
      actualCalls.push({ name, stage: 'web.worker.write', serializerOutputJsonSha256: hash(Buffer.from(encodedJson(serialized))) });
      return api.productionGeoPackage('write', new ArrayBuffer(0), serialized);
    });
    writeEvidence(run, name+'.web.gpkg', new Uint8Array(packed.buffer));
    const reopened = await checked('web.worker.read', async () => {
      actualCalls.push({ name, stage: 'web.worker.read', actualWorkerGeoPackageSha256: hash(new Uint8Array(packed.buffer)) });
      return check((await api.productionGeoPackage('read', packed.buffer)).metadata.projectState, expected, fixed, name+'.worker-reread');
    });
    return { project: reopened, jsonFile, gpkgFile, serialized,
      serializerOutputJsonSha256: hash(Buffer.from(encodedJson(serialized))),
      actualWorkerGeoPackageSha256: hash(new Uint8Array(packed.buffer)) };
  }
  async function trace(kind, inputFile, name, expected, fixed, fixtureKind) {
    const directory = path.join(run, name);
    assert.equal(fs.existsSync(directory), false, 'trace directory must be fresh');
    const trace = { name, kind, inputFile, inputFileSha256: hash(fs.readFileSync(inputFile)),
      directory, complete: false, expectedArtifactCount: TRACE_FILES.length, checkedArtifactCount: 0, files: [] };
    traces.push(trace);
    const result = await checked('app.trace.process', () => runNative(probe,
      ['--trace-file', kind, inputFile, directory], run, name, processes));
    trace.exitCode = result.status;
    await checked('app.trace.status', () => {
      assert.equal(result.status, 0, `${name}: native trace process ${result.stderr}`);
      const status = json(path.join(directory, 'trace-status.json')); trace.status = status;
      assert.equal(status.complete, true, `${name}: native trace ended early`);
      assert.equal(status.stage, 'app.export', `${name}: complete export stage`);
      assert.equal(status.error, '', `${name}: unexpected trace error`);
      assert.match(result.stdout, /^1 production file read\/save\/reopen\/export trace completed\r?\n$/, `${name}: actual completed trace count`);
      assert.deepEqual(fs.readdirSync(directory).sort(), [...TRACE_FILES].sort(), `${name}: exact trace artifact count`);
    });
    for (const file of TRACE_FILES) {
      const bytes = fs.readFileSync(path.join(directory, file));
      assert.ok(bytes.length, `${name}: empty ${file}`);
      trace.files.push({ file, bytes: bytes.length, sha256: hash(bytes) });
    }
    for (const file of ['read.web.json','reopened.web.json','package-reopened.web.json','export.web.json'])
      await checked('app.'+file, () => check(json(path.join(directory, file)), expected, fixed, name+'.'+file));
    await checked('app.saved.native.json', () => compareNativeStorage(json(path.join(directory, 'saved.native.json')), fixed, name+'.saved.native.json'));
    await checked('app.saved.native.gpkg', () => {
      // Its production decode is independently checked at package-reopened.web.json.
      const bytes = fs.readFileSync(path.join(directory, 'saved.native.gpkg'));
      assert.equal(bytes.subarray(0,16).toString(), 'SQLite format 3\0', `${name}: actual native GeoPackage SQLite file`);
    });
    await checked('app.activation', () => assert.equal(json(path.join(directory, 'activation.json')).result,
      fixtureKind === 'static' ? 'OK' : 'TIMELINE_ACTIVATION', `${name}: native activation`));
    trace.checkedArtifactCount = TRACE_FILES.length; trace.complete = true;
    return { directory, output: json(path.join(directory, 'export.web.json')),
      nativeFile: path.join(directory, 'saved.native.json'), nativePackage: path.join(directory, 'saved.native.gpkg') };
  }
  for (const kind of kinds) for (const format of ['json', 'gpkg']) {
    const expected = json(path.join(fixtureRoot, kind+'.expected.json'));
    const fixed = json(path.join(fixtureRoot, kind+'.json'));
    const fixtureFile = path.join(fixtureRoot, kind+'.'+format);
    for (const direction of nativeExecuted ? ['web-app-web','app-web-app'] : ['web-only']) {
      const name = nativeExecuted ? `${direction}-${kind}-${format}` : `${kind}-${format}`;
      stages = []; stage = 'web.fixed-input';
      const details = { name, direction, kind, format, nativeExecuted, stages,
        inputFileSha256: hash(fs.readFileSync(fixtureFile)),
        expectedFileSha256: hash(fs.readFileSync(path.join(fixtureRoot, kind+'.expected.json'))) };
      try {
        let input = await checked('web.fixed-input', async () => check(format === 'json' ? json(fixtureFile)
          : await workerRead(fixtureFile, name+'.fixed'), expected, fixed, name+'.fixed-input'));
        details.parsedInputJsonSha256 = hash(Buffer.from(JSON.stringify(input)));
        let final;
        if (!nativeExecuted) {
          final = await webRoundTrip(input, expected, fixed, name);
        } else if (direction === 'web-app-web') {
          // Preserve the original fixed JSON/GPKG file boundary as the first App input.
          const middle = await trace(format === 'json' ? 'web' : 'gpkg', fixtureFile, name+'.app', expected, fixed, kind);
          final = await webRoundTrip(middle.output, expected, fixed, name+'.return-web');
        } else {
          // Bootstrap is explicit, checked, and excluded from the measured B origin.
          const seed = await trace(format === 'json' ? 'web' : 'gpkg', fixtureFile, name+'.bootstrap', expected, fixed, kind);
          const nativeInput = format === 'json' ? seed.nativeFile : seed.nativePackage;
          details.nativeOrigin = { bootstrapTrace: name+'.bootstrap', nativeInput,
            nativeInputSha256: hash(fs.readFileSync(nativeInput)), bootstrapIsMeasuredOrigin: false };
          const origin = await trace(format === 'json' ? 'native' : 'gpkg', nativeInput, name+'.native-origin', expected, fixed, kind);
          const web = await webRoundTrip(origin.output, expected, fixed, name+'.web');
          const returned = await trace(format === 'json' ? 'web' : 'gpkg', format === 'json' ? web.jsonFile : web.gpkgFile,
            name+'.app-return', expected, fixed, kind);
          final = { ...web, project: returned.output };
        }
        details.serializerOutputJsonSha256 = final.serializerOutputJsonSha256;
        details.actualWorkerGeoPackageSha256 = final.actualWorkerGeoPackageSha256;
        details.activation = await checked('web.activation', () => activation(api, final.project, kind));
        cases.push({ ...details, passed: true, mismatch: 0, skip: 0 });
      } catch (error) {
        cases.push({ ...details, passed: false, firstDivergingStage: stage, mismatch: 1, skip: 0, error: error.message });
      }
    }
  }
  for (const [name, id, field, value, code] of negativeCases) {
    stages = []; stage = 'web.reject';
    const details = { name, direction: 'rejection', expectedCode: code, nativeExecuted, stages };
    try {
      const input = json(path.join(fixtureRoot, 'calendar-boundaries.json'));
      Object.values(input.timelineRecords).filter(Array.isArray).flat().find(row => row.id === id)[field] = value;
      await checked('web.reject', () => assert.throws(() => api.prepareProjectForStorage(input), { code }));
      if (nativeExecuted) {
        writeEvidence(run, name+'.invalid.json', encodedJson(input));
        const output = path.join(run, name+'.rejected-output.json');
        await checked('app.reject', () => {
          const result = runNative(probe, [path.join(run, name+'.invalid.json'), output], run, name, processes);
          details.processExit = result.status; details.nativeError = result.stderr.trim();
          assert.equal(result.status, 1, `${name}: real native rejection`);
          assert.ok(result.stderr.includes(code), `${name}: native category ${code}`);
          assert.equal(fs.existsSync(output), false, `${name}: rejected result was published`);
        });
      }
      cases.push({ ...details, passed: true, mismatch: 0, skip: 0 });
    } catch (error) { cases.push({ ...details, passed: false, firstDivergingStage: stage, mismatch: 1, skip: 0, error: error.message }); }
  }
  const fail = cases.filter(row => !row.passed).length;
  assert.equal(cases.length, nativeExecuted ? 16 : 10, 'mandatory case count');
  const directions = Object.fromEntries(['web-only','web-app-web','app-web-app','rejection'].map(direction => {
    const rows = cases.filter(row => row.direction === direction);
    const expected = direction === 'rejection' ? 4 : direction === 'web-only' ? (nativeExecuted ? 0 : 6) : (nativeExecuted ? 6 : 0);
    assert.equal(rows.length, expected, `${direction}: mandatory processed count`);
    return [direction, { expected, processed: rows.length, passed: rows.filter(row => row.passed).length,
      failed: rows.filter(row => !row.passed).length, mismatch: rows.filter(row => !row.passed).length, skip: 0,
      nativeExecuted: nativeExecuted && direction !== 'web-only' }];
  }));
  const expectedTraceCount = nativeExecuted ? 24 : 0, expectedProcessCount = nativeExecuted ? 28 : 0;
  if (!fail) {
    assert.equal(traces.length, expectedTraceCount, 'mandatory actual native trace count');
    assert.equal(processes.length, expectedProcessCount, 'mandatory actual process count');
  }
  const report = { schema: 'pando-web-v10-exchange-evidence', version: 2, ...provenance, node: process.version,
    nativeExecuted, comparisonContract: 'literal raw output before production reader; prepared output checked separately; only identity/record/archive rows unordered',
    nativeProbe: probe ? { path: path.resolve(probe), sha256: hash(fs.readFileSync(probe)) } : null,
    evidenceDir: run, cases, directions, processes, traces, actualCalls,
    traceCount: traces.length, expectedTraceCount, processCount: processes.length, expectedProcessCount,
    completedTraceCount: traces.filter(row => row.complete).length,
    pass: cases.length-fail, fail, mismatch: fail, skip: 0,
    limits: ['Headless production codec/Worker; no browser DOM, GPU or device performance claim.',
      'The immutable nine-file corpus has no populated flag or inline-content rows; absence and ownership are checked, not populated flag coverage.'] };
  writeEvidence(run, 'results.json', encodedJson(report));
  return report;
}

export const runWebOracle = options => runOracle({ ...options, probe: undefined });
export const runNativeOracle = options => { assert.ok(options.probe); return runOracle(options); };

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const [mode, first, second] = process.argv.slice(2);
  try {
    if (mode === '--verify-only') console.log(JSON.stringify(verifyFixtureSnapshot()));
    else {
      assert.ok(mode === '--web-only' ? first : mode === '--native' && first && second,
        'usage: web-v10-exchange-oracle.mjs --verify-only | --web-only <evidence-dir> | --native <real-probe> <evidence-dir>');
      const report = mode === '--web-only' ? await runWebOracle({ evidenceDir: path.resolve(first) })
        : await runNativeOracle({ probe: path.resolve(first), evidenceDir: path.resolve(second) });
      console.log(JSON.stringify(report)); if (report.fail) process.exitCode = 1;
    }
  } catch (error) { console.error(error.stack); process.exitCode = 1; }
}
