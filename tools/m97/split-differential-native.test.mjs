import test from 'node:test';
import assert from 'node:assert/strict';
import {existsSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {loadSplitModules,splitCaseDefinition,seedSplitFeatures} from './web-split.mjs';
const binary=process.env.PANDO_M973_SPLIT_PROBE;
test('compiled split controller observes actual candidates, cancel, commit, exact undo and redo',async()=>{
 assert.ok(binary&&existsSync(binary),'compiled m973_split_controller_probe is required');
 const loaded=await loadSplitModules();
 try{const definition=splitCaseDefinition('root-two-crossing');const input=[{case:definition.id,definition,features:seedSplitFeatures(loaded.api,definition)}];
 const run=spawnSync(binary,[],{input:JSON.stringify(input),encoding:'utf8',timeout:120000});assert.equal(run.status,0,run.stderr);
 const row=JSON.parse(run.stdout)[0];const {normalizeSplitNativeCase}=await import('./split-differential.mjs');normalizeSplitNativeCase(row);for(const mutate of [r=>delete r.stages.selected.raw.remainingGeometry,r=>delete r.stages.selected.raw.combinedGeometry,r=>delete r.stages.before.features[0].parentId,r=>delete r.stages.before.state.active,r=>delete r.events[0].active]){const corrupt=structuredClone(row);mutate(corrupt);assert.throws(()=>normalizeSplitNativeCase(corrupt),/missing|parent|geometry|observation/i);}assert.equal(row.case,definition.id);assert.equal(row.stages.candidates.raw.candidates.length,2);assert.equal(row.stages.candidates.raw.candidates[0].geometry.type,'Polygon');
 assert.equal(row.stages.cancel.documentSha256,row.stages.before.documentSha256);assert.equal(row.stages.confirm.outcome,true);assert.equal(row.stages.undo.documentSha256,row.stages.before.documentSha256);assert.equal(row.stages.redo.documentSha256,row.stages.confirm.documentSha256);assert.equal(row.stages.undo.history.canUndo,false);assert.equal(row.stages.redo.history.canRedo,false);
 assert.ok(row.events.some(e=>e.selectionPending));assert.ok(row.events.some(e=>e.previewPending));assert.ok(row.events.some(e=>e.applying));assert.equal(row.inputObservations.length,2);
 }finally{loaded.cleanup();}
});
test('compiled split controller rejects incomplete fixture protocol',()=>{
 assert.ok(binary&&existsSync(binary),'compiled m973_split_controller_probe is required');const run=spawnSync(binary,[],{input:'[{"case":"bad"}]',encoding:'utf8',timeout:10000});assert.notEqual(run.status,0);assert.match(run.stderr,/fixture|definition|source/);
});
test('bounded screen-preimage adapter preserves exact geographic input or fails closed',()=>{
 assert.ok(binary&&existsSync(binary),'compiled m973_split_controller_probe is required');const run=spawnSync(binary,['--input-contract-self-test'],{input:'[]',encoding:'utf8',timeout:10000});assert.equal(run.status,0,run.stderr);const report=JSON.parse(run.stdout);assert.equal(report.schema,'m973-exact-screen-preimage-tests');assert.equal(report.passed,7);assert.equal(report.maximumUlpRadius,4);
});
test('compiled controller rebuilds an emptied selection before archive and commit',async()=>{
 assert.ok(binary&&existsSync(binary),'compiled m973_split_controller_probe is required');const {splitReactivationCaseDefinition}=await import('./web-split.mjs');const definition=splitReactivationCaseDefinition(),loaded=await loadSplitModules();
 try{const run=spawnSync(binary,[],{input:JSON.stringify([{case:definition.id,definition,features:seedSplitFeatures(loaded.api,definition)}]),encoding:'utf8',timeout:120000});assert.equal(run.status,0,run.stderr);const row=JSON.parse(run.stdout)[0];assert.equal(row.stages.emptied?.outcome,false);assert.equal(row.stages.reactivated?.outcome,true);assert.deepEqual(row.stages.emptied.state.selectedCandidateIds,[]);assert.equal(row.stages.reactivated.state.previewReady,true);assert.equal(row.stages.confirm.outcome,true);assert.equal(row.stages.undo.documentSha256,row.stages.before.documentSha256);assert.equal(row.reactivationEvidence?.length,2,'both actual reactivation cycles must be observed');for(const cycle of row.reactivationEvidence){assert.ok(cycle.events.some(e=>e.selectionPending));assert.ok(cycle.events.some(e=>e.previewPending));}
 }finally{loaded.cleanup();}
});
