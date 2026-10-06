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

test('compiled preview exposes actual reparent decisions without changing canonical ancestry',async()=>{
 assert.ok(binary&&existsSync(binary),'compiled m973_split_controller_probe is required');
 const {splitControllerDefinitions}=await import('./split-browser-runner.mjs'),{normalizeSplitNativeCase,compareSplitObservations}=await import('./split-differential.mjs');
 const definition=splitControllerDefinitions([splitCaseDefinition('child-with-dependents')]).definitions[0],loaded=await loadSplitModules();
 try{
  const run=spawnSync(binary,[],{input:JSON.stringify([{case:definition.id,definition,features:seedSplitFeatures(loaded.api,definition)}]),encoding:'utf8',timeout:120000});assert.equal(run.status,0,run.stderr);
  const raw=JSON.parse(run.stdout)[0];assert.ok(raw.stages.review.raw.splitPreview.rows.filter(r=>r.after!==null).every(r=>typeof r.parentId==='string'),'actual receipt rows must explicitly observe parent identities');const row=normalizeSplitNativeCase(raw),preview=row.stages.review.previewFeatures;
  assert.equal(row.stages.review.previewPresent,true);assert.equal(raw.stages.review.raw.splitPreview.status,'completed');assert.equal(raw.stages.review.raw.splitPreviewReceiptPresent,true);
  // Protocol mutation: completed validation-blocking receipts still describe a preview.
  const blocked=structuredClone(raw);blocked.stages.review.raw.splitPreview.ok=false;blocked.stages.review.raw.splitPreview.blocking=true;assert.equal(normalizeSplitNativeCase(blocked).stages.review.previewPresent,true);
  assert.deepEqual(preview.map(({id,parentId})=>({id,parentId})),[{id:'$created-preview',parentId:'parent'},{id:'source',parentId:'parent'},{id:'child-moved',parentId:'$created-preview'},{id:'child-cross',parentId:'source'},{id:'grandchild-cross',parentId:'child-cross'}]);
  assert.equal(row.stages.review.features.find(f=>f.id==='child-moved').parentId,'source');assert.equal(raw.stages.review.documentSha256,raw.stages.before.documentSha256);assert.equal(raw.stages.cancel.documentSha256,raw.stages.before.documentSha256);
  assert.equal(row.stages.confirm.features.find(f=>f.id==='child-moved').parentId,'$created');assert.equal(row.stages.undo.features.find(f=>f.id==='child-moved').parentId,'source');assert.equal(row.stages.redo.features.find(f=>f.id==='child-moved').parentId,'$created');
  for(const mutate of [r=>delete r.stages.review.previewFeatures[0].parentId,r=>delete r.stages.review.raw.splitPreview,r=>delete r.stages.review.raw.splitPreview.rows[0].parentId,r=>r.stages.review.raw.splitPreview.rows[0].parentId='source',r=>r.stages.review.previewFeatures[0].parentId='unobserved-parent']){const corrupt=structuredClone(raw);mutate(corrupt);assert.throws(()=>normalizeSplitNativeCase(corrupt),/missing|parent|preview|identity|receipt/i);}
  const bad=structuredClone(row);bad.stages.review.previewFeatures.find(f=>f.id==='child-moved').parentId='source';assert.ok((await compareSplitObservations(row,bad)).some(d=>d.field==='review.previewFeatures.metadata'));
 }finally{loaded.cleanup();}
});

test('failed child exhaustion retains its raw receipt separately from absent preview',async()=>{
 assert.ok(binary&&existsSync(binary),'compiled m973_split_controller_probe is required');
 const {splitControllerDefinitions}=await import('./split-browser-runner.mjs'),{normalizeSplitNativeCase}=await import('./split-differential.mjs');
 const definition=splitControllerDefinitions([splitCaseDefinition('child-all-selected')]).definitions[0],loaded=await loadSplitModules();
 try{
  const run=spawnSync(binary,[],{input:JSON.stringify([{case:definition.id,definition,features:seedSplitFeatures(loaded.api,definition)}]),encoding:'utf8',timeout:120000});assert.equal(run.status,0,run.stderr);const raw=JSON.parse(run.stdout)[0],review=raw.stages.review;
  assert.equal(review.raw.splitPreview.status,'failed');assert.equal(review.raw.splitPreviewReceiptPresent,true);assert.equal(review.raw.splitPreviewPresent,false);assert.equal(review.raw.splitPreview.detail,'SPLIT_SOURCE_EXHAUSTED');assert.equal(review.raw.splitPreview.blocking,true);assert.equal(review.raw.splitPreview.ok,false);assert.deepEqual(review.raw.splitPreview.rows,[]);assert.equal(review.raw.splitPreview.transferredGeometry.type,'Polygon');
  assert.equal(review.state.previewBlocking,true);assert.equal(review.state.previewReady,false);assert.equal(review.state.canAddPart,false);assert.equal(review.state.canApply,false);assert.equal(raw.stages.archive.outcome,false);assert.equal(review.state.error,'SPLIT_SOURCE_EXHAUSTED');assert.equal(review.outcome,false);assert.equal(review.documentSha256,raw.stages.before.documentSha256);assert.equal(raw.stages.cancel.documentSha256,raw.stages.before.documentSha256);assert.deepEqual(review.history,{canUndo:false,canRedo:false});assert.equal(Object.hasOwn(raw.stages,'confirm'),false);
  const normalized=normalizeSplitNativeCase(raw);assert.equal(normalized.stages.review.previewPresent,false);assert.deepEqual(normalized.stages.review.previewFeatures,[]);
  for(const mutate of [r=>delete r.stages.review.raw.splitPreview.status,r=>delete r.stages.review.raw.splitPreviewReceiptPresent,r=>delete r.stages.review.raw.splitPreviewPresent,r=>r.stages.review.raw.splitPreviewPresent=true,r=>r.stages.review.raw.splitPreviewReceiptPresent=false,r=>r.stages.review.previewFeatures.push({id:'source',parentId:'parent',geometry:r.stages.review.raw.splitPreview.transferredGeometry})]){const bad=structuredClone(raw);mutate(bad);assert.throws(()=>normalizeSplitNativeCase(bad),/missing|preview|receipt|status|failed/i);}
 }finally{loaded.cleanup();}
});
