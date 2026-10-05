import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { pathToFileURL, fileURLToPath } from 'node:url';

const WEB_SHA = '7547c473e355c6656daf965e652dfe801ca70183';
const MANIFEST_SHA = 'c81429244ad6419dbea44d87d3f23bf726a415be375937f4ec644b6442678e9c';
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const readJson = file => JSON.parse(fs.readFileSync(file, 'utf8'));
function git(root, ...args) {
  const child = spawnSync('git', ['-C', root, ...args], { maxBuffer: 64 * 1024 * 1024 });
  assert.equal(child.status, 0, child.stderr?.toString());
  return child.stdout;
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
// Only identity/record/archive collections are unordered. Nested arrays and
// geometry coordinates remain exact; no endpoint parsing or numeric rounding.
export function compareProject(actual, expected, stage) {
  assert.deepEqual(rowsByIdentity(actual.territorialEntities), rowsByIdentity(expected.territorialEntities), `${stage}: identities/metadata`);
  assert.equal(actual.timelineRecords.schemaVersion, expected.timelineRecords.schemaVersion, `${stage}: record schema`);
  for (const key of ['lifetimes', 'geometryBindings', 'parentRelations'])
    assert.deepEqual(rowsByIdentity(actual.timelineRecords[key]), rowsByIdentity(expected.timelineRecords[key]), `${stage}: ${key}`);
  assert.deepEqual(rowsByIdentity(actual.geometries, true), rowsByIdentity(expected.geometries, true), `${stage}: complete archive`);
}

export async function runFinalExchange({ probe, webRoot, manifestPath, evidenceDir }) {
  assert.ok(probe && webRoot && manifestPath && evidenceDir,
    'usage: timeline-exchange-oracle.mjs --final <probe> <fixed-web-checkout> <web-manifest> <evidence-dir>');
  probe = path.resolve(probe); webRoot = path.resolve(webRoot);
  assert.ok(fs.existsSync(probe), 'native executable missing');
  const manifestBytes = fs.readFileSync(manifestPath), manifest = JSON.parse(manifestBytes);
  assert.equal(hash(manifestBytes), MANIFEST_SHA, 'handoff manifest bytes changed');
  assert.equal(manifest.WEB_CANDIDATE_SHA, WEB_SHA);
  assert.equal(git(webRoot, 'rev-parse', 'HEAD').toString().trim(), WEB_SHA);
  assert.equal(git(webRoot, 'status', '--porcelain', '--untracked-files=no').toString().trim(), '', 'web checkout changed');
  const consumed = [];
  for (const row of manifest.files.filter(row => row.platform === 'web')) {
    const blob = git(webRoot, 'show', `${WEB_SHA}:${row.path}`);
    const bytes = fs.readFileSync(path.join(webRoot, row.path));
    assert.equal(hash(blob), row.gitBlobSha256, `manifest/blob mismatch: ${row.path}`);
    if (!bytes.equals(blob)) {
      assert.ok(!row.path.endsWith('.gpkg') && !bytes.includes(0), `binary changed: ${row.path}`);
      assert.ok(Buffer.from(bytes.toString('utf8').replace(/\r\n/g, '\n')).equals(blob), `content changed: ${row.path}`);
    }
    consumed.push({ path: row.path, gitBlobSha256: hash(blob), consumedFileSha256: hash(bytes) });
  }
  const appRoot = fileURLToPath(new URL('../', import.meta.url));
  const appSha = git(appRoot, 'rev-parse', 'HEAD').toString().trim();
  if (process.env.PANDOEDITOR_EXPECT_APP_SHA) {
    assert.equal(appSha, process.env.PANDOEDITOR_EXPECT_APP_SHA);
    assert.equal(git(appRoot, 'status', '--porcelain').toString().trim(), '', 'fixed app checkout changed');
  }
  const contract = manifest.files.find(row => row.platform === 'web' && row.path === 'docs/timeline-contract.md');
  assert.equal(hash(git(appRoot, 'show', `${appSha}:docs/timeline-contract.md`)), contract.gitBlobSha256);
  fs.mkdirSync(path.resolve(evidenceDir), { recursive: true });
  const run = fs.mkdtempSync(path.join(path.resolve(evidenceDir), 'bidirectional-'));
  const saveJson = (name, value) => { const file = path.join(run, name); fs.writeFileSync(file, JSON.stringify(value, null, 2)+'\n'); return file; };
  const webImport = file => import(pathToFileURL(path.join(webRoot, file)));
  const { prepareProjectForStorage, prepareProjectForActivation } = await webImport('assets/js/modules/project-state.js');
  const { createProjectSerializer } = await webImport('assets/js/modules/project-serializer.js');
  const { productionGeoPackage } = await webImport('tests/helpers/production-geopackage.mjs');
  const fixtures = path.join(webRoot, 'tests/fixtures/timeline-exchange');
  const cases = [], processes = [];
  // App output uses the existing encodeWeb JSON boundary. Its native GPKG
  // carries native project_state and is not advertised as a web-format export.
  // The gpkg fixture routes exercise actual web GPKG reads by both platforms.
  let stage;
  function execute(args, name) {
    const started = Date.now(), result = spawnSync(probe, args, { encoding: 'utf8', timeout: 120000, maxBuffer: 16 * 1024 * 1024 });
    processes.push({ name, pid: result.pid, args, status: result.status, signal: result.signal, error: result.error?.message, durationMs: Date.now()-started });
    fs.writeFileSync(path.join(run, name+'.stdout.txt'), result.stdout || '');
    fs.writeFileSync(path.join(run, name+'.stderr.txt'), result.stderr || '');
    if (result.error) throw result.error;
    return result;
  }
  function check(project, expected, label) {
    stage = label;
    assert.equal(project.schemaVersion, 9, `${label}: project schema`);
    const value = prepareProjectForStorage(project);
    compareProject(value, expected, label);
    return value;
  }
  function serialize(project) {
    const { territorialEntities, ...projectFields } = project;
    const snapshot = { territorialEntities, projectFields };
    return createProjectSerializer({ appVersion: project.version, baseDataset: project.baseDataset,
      distributionModes: ['territorial', 'geometry'], terrainDataset: 'terrain', hydroDataset: 'hydro',
      readSnapshot: () => snapshot, now: () => new Date(project.savedAt) }).buildProject();
  }
  function trace(kind, input, name, expected) {
    stage = 'app.read';
    const dir = path.join(run, name);
    const result = execute(['--trace-file', kind, input, dir], name);
    const statusPath = path.join(dir, 'trace-status.json');
    if (fs.existsSync(statusPath)) stage = readJson(statusPath).stage;
    for (const file of ['read.web.json', 'reopened.web.json', 'package-reopened.web.json', 'export.web.json'])
      if (fs.existsSync(path.join(dir, file))) check(readJson(path.join(dir, file)), expected, 'app.'+file);
    if (fs.existsSync(statusPath)) stage = readJson(statusPath).stage;
    assert.equal(result.status, 0, `${name}: ${result.stderr}`);
    assert.equal(readJson(statusPath).complete, true, 'native trace ended early');
    for (const file of ['read.web.json', 'reopened.web.json', 'package-reopened.web.json', 'export.web.json'])
      check(readJson(path.join(dir, file)), expected, 'app.'+file);
    assert.ok(fs.existsSync(path.join(dir, 'saved.native.json')), 'native save missing');
    assert.ok(fs.existsSync(path.join(dir, 'saved.native.gpkg')), 'native package save missing');
    stage = 'app.activation';
    const activation = readJson(path.join(dir, 'activation.json')).result;
    assert.equal(activation, name.includes('-static-') ? 'OK' : 'TIMELINE_ACTIVATION');
    return { dir, output: readJson(path.join(dir, 'export.web.json')) };
  }
  async function workerWriteRead(project, expected, name) {
    stage = 'web.serializer'; const serialized = check(serialize(project), expected, stage);
    saveJson(name+'.web.json', serialized);
    stage = 'web.worker.write'; const packed = await productionGeoPackage('write', new ArrayBuffer(0), serialized);
    fs.writeFileSync(path.join(run, name+'.gpkg'), new Uint8Array(packed.buffer));
    stage = 'web.worker.read'; const reread = await productionGeoPackage('read', packed.buffer);
    return { project: check(reread.metadata.projectState, expected, stage),
      file: path.join(run, name+'.gpkg'), json: path.join(run, name+'.web.json') };
  }
  function failure(name, error, extra = {}) {
    cases.push({ name, pass: false, firstDivergingStage: stage, error: error.message,
      expected: error.expected, actual: error.actual,
      responsibility: stage?.startsWith('app.') ? 'app' : stage?.startsWith('web.') ? 'web' : 'inspect contract', ...extra });
    console.error('FAIL', name, stage, error.message);
  }
  for (const kind of ['static', 'complex', 'calendar-boundaries']) for (const format of ['json', 'gpkg']) {
    const expected = readJson(path.join(fixtures, kind+'.expected.json'));
    for (const direction of ['web-app-web', 'app-web-app']) {
      const name = `${direction}-${kind}-${format}`;
      try {
        stage = 'web.fixed-input';
        let input;
        if (format === 'json') input = readJson(path.join(fixtures, kind+'.json'));
        else { const bytes = fs.readFileSync(path.join(fixtures, kind+'.gpkg'));
          input = (await productionGeoPackage('read', bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset+bytes.byteLength))).metadata.projectState; }
        input = check(input, expected, stage);
        const source = saveJson(name+'.source.json', input);
        let final;
        if (direction === 'web-app-web') {
          const saved = await workerWriteRead(input, expected, name+'.initial');
          const middle = trace(format === 'gpkg' ? 'gpkg' : 'web', format === 'gpkg' ? saved.file : saved.json, name+'.app', expected);
          final = (await workerWriteRead(middle.output, expected, name+'.final')).project;
        } else {
          // Seed a real native file once; the measured B direction begins at
          // its subsequent native decode, not at the fixture bootstrap.
          const seed = trace(format === 'gpkg' ? 'gpkg' : 'web', format === 'gpkg' ? path.join(fixtures, kind+'.gpkg') : source, name+'.seed', expected);
          const middle = trace(format === 'gpkg' ? 'gpkg' : 'native', path.join(seed.dir, format === 'gpkg' ? 'saved.native.gpkg' : 'saved.native.json'), name+'.app', expected);
          const web = await workerWriteRead(middle.output, expected, name+'.web');
          final = trace(format === 'gpkg' ? 'gpkg' : 'web', format === 'gpkg' ? web.file : web.json, name+'.final', expected).output;
        }
        stage = 'web.activation';
        if (kind === 'static') prepareProjectForActivation(final);
        else assert.throws(() => prepareProjectForActivation(final), { code: 'TIMELINE_ACTIVATION' });
        cases.push({ name, direction, kind, format, pass: true }); console.log('PASS', name);
      } catch (error) { failure(name, error, { direction, kind, format }); }
    }
  }
  const negatives = [
    ['year-zero', 'calendar-boundaries', 'TIMELINE_INTERVAL', p => p.timelineRecords.lifetimes.find(r => r.id === 'life:A').validFrom = '0000-02'],
    ['nonleap-1900', 'calendar-boundaries', 'TIMELINE_INTERVAL', p => p.timelineRecords.geometryBindings.find(r => r.id === 'B:new').validFrom = '1900-02-29'],
    ['inclusive-overlap', 'calendar-boundaries', 'TIMELINE_OVERLAP', p => p.timelineRecords.geometryBindings.find(r => r.id === 'A:new').validFrom = '+12000-02-28'],
    ['leap-day-gap', 'calendar-boundaries', 'TIMELINE_GAP', p => p.timelineRecords.geometryBindings.find(r => r.id === 'A:new').validFrom = '+12000-03'],
    ['missing-geometry', 'static', 'TIMELINE_GEOMETRY', p => p.timelineRecords.geometryBindings[0].geometryRef.version = 999],
    ['duplicate-record', 'static', 'TIMELINE_ID', p => p.timelineRecords.geometryBindings[0].id = p.timelineRecords.lifetimes[0].id],
    ['missing-parent', 'static', 'TIMELINE_PARENT', p => p.timelineRecords.parentRelations[0].parentId = 'missing'],
    ['reversed', 'static', 'TIMELINE_INTERVAL', p => Object.assign(p.timelineRecords.lifetimes[0], { validFrom: '1915', validTo: '1914' })],
  ];
  // File-boundary type rejection is additional to (and not counted as passing)
  // typed record/storage parity tools' excluded JS wire cases. First-error
  // category ordering for malformed wire types is not a shared contract.
  for (const [name, value] of [['boolean', false], ['number', 1], ['object', {}], ['array', []]]) negatives.push([
    'endpoint-wire-'+name, 'static', null, p => p.timelineRecords.lifetimes[0].validFrom = value]);
  for (const [name, value] of [['string', '1'], ['fraction', 1.5], ['null', null]]) negatives.push([
    'version-wire-'+name, 'static', null, p => p.geometries[0].version = value]);
  for (const [name, kind, code, mutate] of negatives) {
    try {
      const input = readJson(path.join(fixtures, kind+'.json')); mutate(input);
      const source = saveJson(name+'.source.json', input), output = path.join(run, name+'.rejected-output.json');
      stage = 'web.reject'; let webError;
      try { prepareProjectForStorage(input); } catch (error) { webError = error; }
      assert.ok(webError, 'web accepted invalid fixed-file mutation'); if (code) assert.equal(webError.code, code);
      stage = 'app.reject'; const result = execute([source, output], name);
      assert.equal(result.status, 1, 'expected native rejection exit 1');
      assert.equal(fs.existsSync(output), false, 'rejected file published output');
      if (code) assert.ok(result.stderr.includes(code), `expected ${code}, received ${result.stderr}`);
      cases.push({ name, direction: 'rejection', pass: true, webError: webError.code, appError: result.stderr.trim() });
      console.log('PASS rejection', name);
    } catch (error) { failure(name, error, { direction: 'rejection' }); }
  }
  assert.equal(cases.length, 12+negatives.length, 'mandatory cases omitted');
  const report = { WEB_CANDIDATE_SHA: WEB_SHA, APP_CANDIDATE_SHA: appSha,
    exchangeBoundary: { appToWeb: 'production encodeWeb JSON', webToApp: 'JSON or web-produced GeoPackage',
      nativeGeoPackage: 'native storage/reopen only; not a common web export' },
    appWorkingTree: git(appRoot, 'status', '--porcelain').toString(), MANIFEST_SHA256: MANIFEST_SHA,
    CONTRACT_HASHES: { web: contract.gitBlobSha256, app: contract.gitBlobSha256 }, consumed,
    probeSha256: hash(fs.readFileSync(probe)), processes, cases,
    directions: Object.fromEntries(['web-app-web', 'app-web-app', 'rejection'].map(direction => {
      const rows = cases.filter(row => row.direction === direction);
      return [direction, { expected: direction === 'rejection' ? negatives.length : 6,
        processed: rows.length, passed: rows.filter(row => row.pass).length, failed: rows.filter(row => !row.pass).length, skipped: 0 }];
    })) };
  saveJson('results.json', report);
  assert.equal(git(webRoot, 'rev-parse', 'HEAD').toString().trim(), WEB_SHA);
  assert.equal(git(webRoot, 'status', '--porcelain', '--untracked-files=no').toString().trim(), '', 'web changed during run');
  assert.equal(git(appRoot, 'rev-parse', 'HEAD').toString().trim(), appSha, 'app commit changed during run');
  if (process.env.PANDOEDITOR_EXPECT_APP_SHA)
    assert.equal(git(appRoot, 'status', '--porcelain').toString().trim(), '', 'app changed during run');
  const fail = cases.filter(row => !row.pass).length;
  console.log(`${cases.length-fail}/${cases.length} final exchange cases passed, ${fail} failed, 0 skipped; ${run}`);
  if (fail) process.exitCode = 1;
  return report;
}
