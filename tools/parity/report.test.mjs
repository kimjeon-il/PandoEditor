import assert from 'node:assert/strict';
import test from 'node:test';
import { summarizeFeatures, validateReport, validateApprovedReport, expectedCaseIds } from './report.mjs';
import { sha256 } from './provenance.mjs';
const registry = { features: [{ id: 'edit', title: 'Edit', axes: ['rules', 'data', 'state'] }] };
test('historical and one-sided test passes cannot promote current parity', () => {
  const cases = [{ featureId: 'edit', id: 'old', kind: 'historical', axes: ['rules', 'data', 'state'], status: 'PASS' }];
  assert.equal(summarizeFeatures(registry, cases, ['edit'])[0].axes.data.status, 'NOT_RUN');
  cases.push({ ...cases[0], id: 'current', kind: 'current', axes: ['rules'], status: 'PASS' });
  assert.equal(summarizeFeatures(registry, cases, ['edit'])[0].axes.rules.status, 'PASS');
  assert.equal(summarizeFeatures(registry, cases, ['edit'])[0].axes.state.status, 'NOT_RUN');
});
test('reports require complete case identity accounting and source verification', () => {
  const report = { schema: 'web-app-parity-report', version: 1, cases: [{ id: 'one', status: 'NOT_RUN' }], expectedCaseIds: ['one'],
    sources: { web: { head: 'a'.repeat(40) }, app: { head: 'b'.repeat(40) } }, contract: { files: ['pin'] }, provenanceVerified: true,
    matrix: [{ id: 'edit', axes: Object.fromEntries(['availability','rules','data','state','interaction'].map(axis => [axis,{status:'NOT_RUN'}])) }] };
  assert.equal(validateReport(report), report);
  assert.throws(() => validateReport({ ...report, expectedCaseIds: ['one', 'missing'] }), /Incomplete/);
  assert.throws(() => validateReport({ ...report, cases: [] }), /Missing/);
  assert.throws(() => validateReport({ ...report, provenanceVerified: false }), /Unverified/);
  const forged = structuredClone(report); forged.matrix[0].axes.data = {status:'PASS',evidence:['one']};
  assert.throws(() => validateReport(forged), /current comparison/);
});

test('missing required supporting evidence blocks an otherwise paired axis', () => {
  const cases = [
    { featureId: 'edit', id: 'pair', kind: 'current', axes: ['rules'], status: 'PASS' },
    { featureId: 'edit', id: 'unit', kind: 'supporting', axes: ['rules'], status: 'NOT_RUN' },
  ];
  assert.equal(summarizeFeatures(registry, cases, ['edit'])[0].axes.rules.status, 'NOT_RUN');
});

test('approved registry detects a whole feature or case removed from a report', () => {
  const registry = { features: ['a','b'].map(id => ({id,title:id,axes:['rules'],cases:[{id:id+':suite',adapter:'selection',axes:['rules'],observationIds:['first','second']}]})) };
  const registryText = JSON.stringify(registry);
  const path = 'tests/fixtures/portability/index.json';
  const pin = {webContractCommit:'c'.repeat(40),sha256:{[path]:sha256(registryText)}};
  const cases = registry.features.flatMap(feature => feature.cases[0].observationIds.map(id => ({id:feature.id+':suite/'+id,
    definitionId:feature.id+':suite',featureId:feature.id,axes:['rules'],kind:'current',status:'PASS',differences:[]})));
  const report = {schema:'web-app-parity-report',version:1,registryText,selected:['a','b'],cases,expectedCaseIds:expectedCaseIds(registry,['a','b']),
    sources:{web:{head:'a'.repeat(40)},app:{head:'b'.repeat(40)}},provenanceVerified:true,
    contract:{commit:pin.webContractCommit,files:[{path,sha256:pin.sha256[path]}]},matrix:summarizeFeatures(registry,cases,['a','b'])};
  assert.deepEqual(validateApprovedReport(report,pin),registry);
  const partial = structuredClone(report);partial.cases.pop();partial.expectedCaseIds.pop();
  assert.throws(()=>validateApprovedReport(partial,pin),/accounting|evidence/);
  const removed = structuredClone(report);removed.selected=['a'];removed.cases=removed.cases.filter(row=>row.featureId==='a');
  removed.expectedCaseIds=removed.cases.map(row=>row.id);removed.matrix=removed.matrix.filter(row=>row.id==='a');
  assert.throws(()=>validateApprovedReport(removed,pin),/Matrix/);
  assert.throws(()=>validateApprovedReport({...report,registryText:'{}'},pin),/approved registry/);
  const moved = structuredClone(report);moved.cases[0].featureId='b';moved.cases[0].definitionId='b:suite';
  assert.throws(()=>validateApprovedReport(moved,pin),/another definition/);
});
