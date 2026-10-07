#!/usr/bin/env node
// One registry-driven runner. Historical and one-sided evidence never implies
// current behavioral parity. Missing observations remain visible in the matrix.
import { spawnSync, execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync, mkdtempSync, existsSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { sourceIdentity, verifyContractPin, verifyBuildReceipt, verifyCTestCommand, sha256 } from './parity/provenance.mjs';
import { prepareNativeBuild } from './parity/native-build.mjs';
import { nativeTargets, observeCurrent } from './parity/current-adapters.mjs';
import { summarizeFeatures, markdownReport, expectedCaseIds as plannedCaseIds } from './parity/report.mjs';

const arg = flag => { const i = process.argv.indexOf(flag); return i < 0 ? null : process.argv[i + 1]; };
const json = path => JSON.parse(readFileSync(path, 'utf8').replace(/^\uFEFF/, ''));
const imported = (root, path) => import(pathToFileURL(resolve(root, path)));
const escapeRegex = value => value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
const targetForTest = name => name === 'selection_state_tests' ? 'selection_probe' : name;
async function main() {
  const appRoot = resolve(arg('--root') ?? fileURLToPath(new URL('..', import.meta.url)));
  if (!arg('--web-root')) throw new Error('Required: --web-root WEB_CHECKOUT');
  const webRoot = resolve(arg('--web-root'));
  const registryText = readFileSync(resolve(webRoot, 'tests/fixtures/portability/index.json'), 'utf8');
  const registry = JSON.parse(registryText);
  const { validateRegistry, selectFeatures } = await imported(webRoot, 'tools/parity/registry.mjs');
  const { compareObservations, classifyResult, evaluateGates } = await imported(webRoot, 'tools/parity/contract.mjs');
  validateRegistry(registry);
  const explicit = arg('--features')?.split(',').filter(Boolean) ?? null;
  const impact = selectFeatures(registry, arg('--changed') ? json(resolve(arg('--changed'))) : null, explicit);
  const selected = registry.features.filter(row => impact.selected.includes(row.id));
  const definitions = selected.flatMap(feature => feature.cases.map(row => ({ ...row, featureId: feature.id })));
  const targets = [...new Set(definitions.flatMap(row => nativeTargets[row.adapter] ? [nativeTargets[row.adapter]]
    : (row.adapter === 'ui-evidence' && !process.argv.includes('--ui') ? [] : row.nativeTargets ?? (row.nativeTests ?? []).map(targetForTest))))];
  if (process.argv.includes('--list')) {
    console.log(JSON.stringify({ selected: impact.selected, unclassifiedPaths: impact.unclassifiedPaths, targets,
      cases: definitions.map(row => ({ id: row.id, adapter: row.adapter, featureId: row.featureId })) }, null, 2));
    if (impact.unclassifiedPaths.length) process.exitCode = 1;
    return;
  }
  if (!arg('--out') || !arg('--build-dir')) throw new Error('Required: --out REPORT.json --build-dir NATIVE_BUILD [--build] [--ui]');
  const output = resolve(arg('--out')), buildRoot = resolve(arg('--build-dir'));
  mkdirSync(dirname(output), { recursive: true });
  // Fresh evidence per invocation, even when the report destination is reused.
  const evidenceRoot = mkdtempSync(output + '.evidence-');
  const sources = { web: sourceIdentity(webRoot), app: sourceIdentity(appRoot) };
  const problems = [], checks = [], cases = [], expectedCaseIds = plannedCaseIds(registry, impact.selected);
  let contract = null, provenanceVerified = true, receipt = null;
  try {
    const pin = json(resolve(appRoot, 'docs/platform-portability-pin.json'));
    for (const file of registry.requiredFixtures ?? []) if (!Object.hasOwn(pin.sha256 ?? {}, file)) throw new Error('Required contract file is not pinned: ' + file);
    contract = verifyContractPin(webRoot, pin);
  }
  catch (error) { problems.push(error.message); provenanceVerified = false; }
  if (process.argv.includes('--official') && (!sources.web.clean || !sources.app.clean)) {
    problems.push('Official evidence requires two clean checkouts'); provenanceVerified = false;
  }
  if (process.argv.includes('--build')) {
    try { receipt = prepareNativeBuild({ appRoot, buildRoot, targets, cmake: arg('--cmake') ?? 'cmake' }); }
    catch (error) { problems.push('Native build failed: ' + error.message); provenanceVerified = false; }
  } else if (existsSync(resolve(buildRoot, 'parity-build-receipt.json'))) receipt = json(resolve(buildRoot, 'parity-build-receipt.json'));
  const knownPath = resolve(webRoot, 'tests/fixtures/portability/known-differences.json');
  const known = existsSync(knownPath) ? json(knownPath).cases : [];
  const memo = new Map();
  function command(key, command, args, cwd, timeout = 180000) {
    if (memo.has(key)) return memo.get(key);
    const result = spawnSync(command, args, { cwd, encoding: 'utf8', timeout, maxBuffer: 64 * 1024 * 1024,
      env: { ...process.env, ...(process.platform === 'win32' ? {} : { QT_QPA_PLATFORM: 'offscreen', QT_QUICK_BACKEND: 'software' }) } });
    const text = (result.stdout ?? '') + '\n' + (result.stderr ?? '');
    const log = resolve(evidenceRoot, checks.length + '.log'); writeFileSync(log, text);
    const skipped = /(?:# skipped [1-9]|ℹ skipped [1-9]|SKIP\s*:|\b[1-9][0-9]* skipped\b)/.test(text);
    const row = { id: key, command: [command, ...args], exitCode: result.status, signal: result.signal,
      status: result.error || result.signal ? 'ERROR' : result.status !== 0 ? 'FAIL' : skipped ? 'NOT_RUN' : 'PASS',
      error: result.error?.message ?? null, log, sha256: sha256(text) };
    checks.push(row); memo.set(key, row); return row;
  }
  const getBinary = target => {
    if (!receipt) throw new Error('Native build receipt is missing; use --build');
    return verifyBuildReceipt(appRoot, buildRoot, receipt, target);
  };
  let ctestListing;
  const registeredCommand = (test, targets) => {
    if (!ctestListing) {
      const raw = execFileSync(arg('--ctest') ?? 'ctest', ['--test-dir', buildRoot, '-C', receipt.configuration || 'Release', '--show-only=json-v1'], {encoding:'utf8',maxBuffer:32*1024*1024});
      writeFileSync(resolve(evidenceRoot, 'ctest-discovery.json'), raw);
      ctestListing = JSON.parse(raw);
    }
    const entry = ctestListing.tests.find(row => row.name === test);
    return verifyCTestCommand(entry?.command, targets.map(getBinary));
  };
  for (const definition of definitions) {
    const base = { featureId: definition.featureId, axes: definition.axes, definitionId: definition.id };
    try {
      if (nativeTargets[definition.adapter]) {
        const observations = await observeCurrent(definition.adapter, { webRoot, appRoot, evidenceRoot, binary: getBinary(nativeTargets[definition.adapter]) });
        if (!observations.length) throw new Error('Adapter emitted no cases');
        const ids = observations.map(row => row.id);
        if (!definition.observationIds || new Set(ids).size !== ids.length || JSON.stringify([...ids].sort()) !== JSON.stringify([...definition.observationIds].sort())) throw new Error('Adapter observation identity/count does not match the registered corpus');
        writeFileSync(resolve(evidenceRoot, definition.adapter + '.json'), JSON.stringify(observations, null, 2) + '\n');
        for (const observation of observations) {
          const id = definition.id + '/' + observation.id;
          const differences = observation.status ? [] : compareObservations(observation.web, observation.app, observation.expected);
          cases.push({ ...base, id, kind: 'current', status: observation.status ?? classifyResult(id, differences, known.find(row => row.caseId === id)),
            step: observation.step ?? null, differences, reason: observation.reason ?? null });
        }
      } else {
        if (!['evidence', 'ui-evidence', 'historical'].includes(definition.adapter)) throw new Error('Unknown adapter: ' + definition.adapter);
        const evidence = [];
        const isUi = definition.adapter === 'ui-evidence';
        const kind = definition.adapter === 'historical' ? 'historical' : 'supporting';
        if (isUi && (!process.argv.includes('--ui') || process.platform !== 'win32')) {
          cases.push({ ...base, id: definition.id, kind: 'supporting', status: 'NOT_RUN', reason: 'Actual Qt input requires --ui on Windows; not replaced by offscreen evidence.' }); continue;
        }
        for (const file of definition.webTests ?? []) {
          if (!existsSync(resolve(webRoot, file))) throw new Error('Registered Web test missing: ' + file);
          const args = isUi ? [resolve(webRoot, 'node_modules/@playwright/test/cli.js'), 'test', file, '--workers=1',
            '--output=' + resolve(evidenceRoot, 'browser')] : ['--test', file];
          evidence.push(command('web:' + file, process.execPath, args, webRoot));
        }
        for (const test of definition.nativeTests ?? []) {
          const nativeCommand = registeredCommand(test, definition.nativeTargets ?? [targetForTest(test)]);
          if (isUi) {
            // Windows GUI executables may have no redirected stdout. Require the
            // QTest file logger so a successful process cannot conceal QSKIP.
            const key='app-ui:'+test;
            const log=resolve(evidenceRoot,'qt-'+test+'.txt');
            const wasRun=memo.has(key);
            const uiDirectory=resolve(evidenceRoot,'qt-'+test);mkdirSync(uiDirectory,{recursive:true});
            const result=command(key,getBinary(targetForTest(test)),['-platform','windows','-o',log+',txt'],uiDirectory,definition.timeoutMs??600000);
            if (!wasRun) {
              const text=existsSync(log)?readFileSync(log,'utf8'):'';
              result.qtLog=log;result.qtLogSha256=sha256(text);
              if (!/Totals:.*\b[1-9][0-9]* passed/.test(text)) {result.status='ERROR';result.error??='Missing QTest completion evidence';}
              else if (/SKIP\s*:|\b[1-9][0-9]* skipped\b/.test(text)&&result.status==='PASS') result.status='NOT_RUN';
            }
            evidence.push(result);
          }
          else {
            const result = command('app:' + test, arg('--ctest') ?? 'ctest',
              ['--test-dir', buildRoot, '-C', receipt.configuration || 'Release', '-R', '^' + escapeRegex(test) + '$', '--no-tests=error', '--verbose'], appRoot);
            result.nativeCommand = nativeCommand; evidence.push(result);
          }
        }
        const failed = evidence.find(row => row.status !== 'PASS');
        cases.push({ ...base, id: definition.id, kind, sourceCommit: definition.sourceCommit ?? null, status: failed?.status ?? (evidence.length ? 'PASS' : 'NOT_RUN'),
          checks: evidence.map(row => row.id), reason: definition.reason });
      }
    } catch (error) {
      const ids = definition.observationIds?.map(id => definition.id + '/' + id) ?? [definition.id];
      for (const id of ids) if (!cases.some(row => row.id === id)) cases.push({ ...base, id,
        kind: nativeTargets[definition.adapter] ? 'current' : definition.adapter === 'historical' ? 'historical' : 'supporting',
        status: 'ERROR', reason: error.message, differences: [] });
    }
  }
  for (const side of ['web', 'app']) if (sourceIdentity(side === 'web' ? webRoot : appRoot).fingerprint !== sources[side].fingerprint) {
    problems.push(side + ' sources changed during verification'); provenanceVerified = false;
  }
  const matrix = summarizeFeatures(registry, cases, impact.selected);
  const axisRows = matrix.filter(row => row.selected).flatMap(row => Object.values(row.axes));
  const gates = evaluateGates(axisRows, { complete: selected.length === registry.features.length,
    provenanceVerified, unclassifiedPaths: impact.unclassifiedPaths });
  const report = { schema: 'web-app-parity-report', version: 1, generatedAt: new Date().toISOString(),
    registryText, candidateContract: { files: (registry.requiredFixtures ?? []).map(path => ({path,
      sha256: existsSync(resolve(webRoot, path)) ? sha256(readFileSync(resolve(webRoot, path))) : null })) },
    sources, contract, provenanceVerified, build: receipt, selected: impact.selected, expectedCaseIds, cases, checks, matrix,
    unclassifiedPaths: impact.unclassifiedPaths, problems, gates,
    totals: Object.fromEntries(['PASS', 'FAIL', 'KNOWN_DIFFERENCE', 'UNSUPPORTED', 'NOT_RUN', 'ERROR', 'N/A'].map(status => [status, cases.filter(row => row.status === status).length])) };
  writeFileSync(output, JSON.stringify(report, null, 2) + '\n');
  writeFileSync(output.replace(/\.json$/i, '') + '.md', markdownReport(report));
  console.log(JSON.stringify({ report: output, ...gates, totals: report.totals, problems, unclassifiedPaths: impact.unclassifiedPaths }, null, 2));
  if (gates.regression !== 'PASS') process.exitCode = 1;
}
main().catch(error => { console.error('Web/App parity runner failed: ' + error.stack); process.exitCode = 1; });
