import test from 'node:test';
import assert from 'node:assert/strict';
import {readSplitObservation,readSplitObservationBytes,packSplitObservations} from './web-split-fixtures.mjs';
import { runSplitLifecycleCorpus } from './web-split.mjs';

test('actual root entity line selection cannot archive exhausted source',async()=>{
 const corpus=await runSplitLifecycleCorpus({caseIds:['root-all-selected']});
 const row=corpus.cases[0];
 assert.equal(row.stages.selected.selection.remainingGeometry,null);
 assert.equal(row.stages.selected.selection.canAddPart,false);
 assert.equal(row.stages.archive.outcome,false);
 assert.equal(row.stages.review.outcome,false);
 assert.deepEqual(row.stages.cancel.document.document,row.stages.before.document.document);
 assert.equal(row.stages.cancel.document.history.undo,0);
});
test('actual child split creates one fresh sibling and replays exact identities',async()=>{
 const corpus=await runSplitLifecycleCorpus({caseIds:['child-two-crossing']});
 const row=corpus.cases[0];
 assert.equal(row.stages.confirm.outcome,true);
 assert.equal(row.referenceEffects.createdIds.length,1);
 assert.ok(row.referenceEffects.retainedIds.includes('source'));
 assert.deepEqual(row.stages.undo.document.document,row.stages.before.document.document);
 assert.deepEqual(row.stages.redo.document.document,row.stages.confirm.document.document);
});

test('dateline production entity store cut is valid but create containment blocks archive',async()=>{
 const row=(await runSplitLifecycleCorpus({caseIds:['root-date-line']})).cases[0];
 assert.equal(row.assessment.valid,true);assert.equal(row.assessment.split.candidates.length,2);
 assert.equal(row.stages.selected.selection.canAddPart,false);
 assert.equal(row.stages.archive.outcome,false);assert.equal(row.stages.review.outcome,false);
 assert.equal(row.stages.selected.errors.at(-1).code,'PL-COUNTRY-002');
 assert.deepEqual(row.stages.cancel.document.document,row.stages.before.document.document);
});
test('root deleted-child references fail strict commit and roll back atomically',async()=>{
 const rows=(await runSplitLifecycleCorpus({caseIds:['root-with-dependent-reference','root-with-dependent-label-settings']})).cases;
 for(const row of rows){assert.equal(row.stages.selected.selection.previewReady,true);assert.equal(row.stages.confirm.outcome,false);assert.deepEqual(row.stages.confirm.document.document,row.stages.before.document.document);assert.equal(row.stages.confirm.document.history.undo,0);}
});
test('browser bundle contains exact asset paths and browser runtime can execute the same production case',async()=>{
 const {splitBrowserSourceBundle}=await import('./web-split-browser-bundle.mjs');
 const {loadSplitModules}=await import('./web-split.mjs');
 const {workerFactory}=await import('./web-lifecycle.mjs');
 const bundle=await splitBrowserSourceBundle({caseIds:['child-two-crossing']});
 assert.equal(bundle.sources['assets/js/modules/app-object-picking.js'].sha256,'19e62d26000134b4c7ca695ed21e3a9bbaad176ed3b90b757f59d0b141c13e23');
 assert.equal(bundle.sources['assets/js/modules/app-territory-selection-workflow.js'].sha256,'626d2dd6c0a8263224272adacaa3303f41a28a83cf22214bdedfffd11a9bc44d');
 const runtime=(0,eval)(bundle.runtime.source)({assert}),loaded=await loadSplitModules();
 try{loaded.createWorker=workerFactory(loaded.root,loaded.manifest);const row=await runtime.runSplitLifecycleCase(loaded,bundle.cases[0]);assert.equal(row.stages.confirm.outcome,true);}finally{loaded.cleanup();}
});

test('complete pinned split lifecycle rerun matches every captured observation',async()=>{
 const {readFileSync}=await import('node:fs');
 const {splitCaseIds}=await import('./web-split.mjs');
 const expected=readSplitObservation('lifecycle-observations-v2');
 assert.deepEqual(expected.cases.map(row=>row.case),splitCaseIds);
 const actual=await runSplitLifecycleCorpus();
 assert.deepEqual(actual,expected);
 const baseCuts=JSON.parse(readFileSync(new URL('../../tests/fixtures/web-m97/cut-corpus.json',import.meta.url)));
 for(const original of baseCuts)assert.deepEqual(actual.cases.find(row=>row.case==='root-'+original.id).assessment,original.result);
 for(const row of actual.cases){
  assert.deepEqual(row.stages.cancel.document.document,row.stages.before.document.document,row.case+' cancel document');
  assert.equal(row.stages.cancel.document.history.undo,0,row.case+' cancel history');
  if(row.stages.confirm?.outcome){
   assert.equal(row.referenceEffects.createdIds.length,1,row.case+' one new identity');
   assert.deepEqual(row.stages.undo.document.document,row.stages.before.document.document,row.case+' undo');
   assert.deepEqual(row.stages.redo.document.document,row.stages.confirm.document.document,row.case+' redo');
  }else if(row.stages.confirm){assert.deepEqual(row.stages.confirm.document.document,row.stages.before.document.document,row.case+' rollback');assert.equal(row.stages.confirm.document.history.undo,0,row.case+' rollback history');}
 }
});

test('test-only candidate loader requires exact explicit bytes and preserves pinned baseline',async()=>{
 const {loadSplitCandidateModules}=await import('./web-split-candidate.mjs');
 const {splitCaseDefinition,runSplitLifecycleCase}=await import('./web-split.mjs');
 const candidateRoot=new URL('../../tests/fixtures/web-m97/lifecycle-source/',import.meta.url).pathname;
 const change={path:'assets/js/modules/app-cut-geometry.js',sha256:'b91c06a4eddc151890d4ea381eba7c57807d50fb34b19aa460e17a9e9f22cc30'};
 await assert.rejects(loadSplitCandidateModules({candidateRoot,candidateChanges:[{...change,sha256:'0'.repeat(64)}]}),/Candidate bytes changed/);
 const loaded=await loadSplitCandidateModules({candidateRoot,candidateChanges:[change]});
 try{assert.equal(loaded.testOnlyCandidate,true);const row=await runSplitLifecycleCase(loaded,splitCaseDefinition('root-date-line'));assert.equal(row.stages.archive.outcome,false);assert.equal(row.assessment.valid,true);}finally{loaded.cleanup();}
});

test('removed-child presentation and generic compatibility metadata preserve actual checkpoint differences',async()=>{
 const {readFileSync}=await import('node:fs');
 const corpus=readSplitObservation('presentation-observations');
 const style=corpus.cases.find(row=>row.case==='root-with-dependent-presentation');
 assert.equal(style.stages.confirm.outcome,true);assert.deepEqual(style.referenceEffects.deletedIds,['child-moved']);
 assert.equal(style.stages.confirm.document.presentation.itemVisibility.subunits['child-moved'],false);
 assert.deepEqual(style.stages.confirm.document.presentation.layerPresentation.objectStyles['territorial:entity:child-moved'],{opacity:0.5});
 assert.equal(Object.hasOwn(style.stages.redo.document.presentation.itemVisibility.subunits,'child-moved'),false);
 assert.deepEqual(style.stages.redo.document.presentation.layerPresentation.objectStyles,style.stages.confirm.document.presentation.layerPresentation.objectStyles);
 const generic=corpus.cases.find(row=>row.case==='root-with-dependent-generic-metadata');
 assert.equal(generic.stages.confirm.outcome,true);
 for(const stage of ['confirm','undo','redo'])assert.deepEqual(generic.stages[stage].document.genericFeatures,generic.stages.before.document.genericFeatures);
 assert.equal(generic.stages.confirm.document.genericFeatures[0].properties.source.details.legacyGenericSemantics.ownerId,'child-moved');
});

test('original discovery capture and rejection provenance stay byte-for-byte immutable',async()=>{
 const {readFileSync}=await import('node:fs'),{createHash}=await import('node:crypto');
 const bytes=readSplitObservationBytes('lifecycle-observations');
 assert.equal(createHash('sha256').update(bytes).digest('hex'),'b29f65be6069b98925d4c9a32c11794c723517ad52bd3dc842f6dff1fa2b7f5d');
});

test('custom candidate parent and descendant geometries are seeded without changing defaults',async()=>{
 const {loadSplitModules,seedSplitFeatures,splitCaseDefinition}=await import('./web-split.mjs');
 const loaded=await loadSplitModules();
 try{
  const definition=splitCaseDefinition('child-two-crossing');
  const parent={type:'Polygon',coordinates:[[[178,-10],[178,10],[-178,10],[-178,-10],[178,-10]]]};
  const child={id:'custom-child',parentId:'source',geometry:definition.source};
  const features=seedSplitFeatures(loaded.api,{...definition,parentGeometry:parent,dependentFeatures:[child]});
  assert.deepEqual(features[0].geometry,parent);assert.deepEqual(features.at(-1).geometry,child.geometry);assert.equal(features.at(-1).properties.parentId,'source');
  assert.notDeepEqual(seedSplitFeatures(loaded.api,definition)[0].geometry,parent);
 }finally{loaded.cleanup();}
});
test('candidate browser bundle propagates explicit source delta and custom cases',async()=>{
 const {splitBrowserSourceBundle}=await import('./web-split-browser-bundle.mjs');
 const {splitCaseDefinition}=await import('./web-split.mjs');
 const candidateRoot=new URL('../../tests/fixtures/web-m97/lifecycle-source/',import.meta.url).pathname;
 const candidateChanges=[{path:'assets/js/modules/app-cut-geometry.js',sha256:'b91c06a4eddc151890d4ea381eba7c57807d50fb34b19aa460e17a9e9f22cc30'}];
 const definition={...splitCaseDefinition('child-two-crossing'),id:'candidate-only-child'};
 const bundle=await splitBrowserSourceBundle({candidateRoot,candidateChanges,caseDefinitions:[definition]});
 assert.equal(bundle.behavioralCommit,'test-only-candidate');assert.equal(bundle.testOnlyCandidate,true);
 assert.deepEqual(bundle.candidateChanges,candidateChanges);assert.deepEqual(bundle.cases,[definition]);
});

test('compressed observation files are deterministic and lossless',()=>{for(const row of packSplitObservations({check:true}))assert.ok(row.bytes<190*1024,row.name+' exceeds storage budget');});

test('empty selection retains observed inactive cache and rebuilds before reactivated consumption',async()=>{
 const api=await import('./web-split.mjs');
 assert.equal(typeof api.splitReactivationCaseDefinition,'function');
 const loaded=await api.loadSplitModules();
 try{
  const row=await api.runSplitLifecycleCase(loaded,api.splitReactivationCaseDefinition());
  assert.deepEqual(Object.keys(row.stages),['before','candidates','emptied','reactivated','selected','archive','review','cancel','confirm','undo','redo']);
  const empty=row.stages.emptied;
  assert.equal(empty.outcome,false);assert.deepEqual(empty.selection.selectedCandidateIds,[]);
  assert.equal(empty.selection.currentGeometry,null);assert.equal(empty.selection.combinedGeometry,null);
  assert.equal(empty.selection.previewReady,false);assert.equal(empty.selection.canAddPart,false);
  assert.deepEqual(empty.selection.remainingGeometry,row.stages.candidates.selection.remainingGeometry,'Record the actual inactive cache without correcting it');
  const next=row.stages.reactivated;
  assert.equal(next.outcome,true);assert.equal(next.selection.selectedCandidateIds.length,1);
  assert.equal(next.selection.previewReady,true);assert.equal(next.selection.canAddPart,true);
  assert.deepEqual(next.selection.currentGeometry,row.stages.candidates.selection.currentGeometry);
  assert.deepEqual(next.selection.remainingGeometry,row.stages.candidates.selection.remainingGeometry);
  assert.equal(row.reactivation.candidateId,next.selection.selectedCandidateIds[0]);
  assert.ok(row.reactivation.rebuildEvidence.selectionRequests.length>0);assert.ok(row.reactivation.rebuildEvidence.previewRequests.length>0);
  assert.ok(row.reactivation.confirmationRebuildEvidence.selectionRequests.length>0);assert.ok(row.reactivation.confirmationRebuildEvidence.previewRequests.length>0);
  assert.equal(row.stages.archive.outcome,true);assert.equal(row.stages.review.outcome,true);assert.equal(row.stages.confirm.outcome,true);
  assert.deepEqual(row.stages.cancel.document.document,row.stages.before.document.document);
  assert.deepEqual(row.stages.undo.document.document,row.stages.before.document.document);
  assert.deepEqual(row.stages.redo.document.document,row.stages.confirm.document.document);
 }finally{loaded.cleanup();}
});

test('browser runtime serializes the separate reactivation scenario without changing the pinned inventory',async()=>{
 const {splitBrowserSourceBundle}=await import('./web-split-browser-bundle.mjs');
 const {loadSplitModules,splitReactivationCaseDefinition,splitCaseIds}=await import('./web-split.mjs');
 const {workerFactory}=await import('./web-lifecycle.mjs');
 assert.equal(splitCaseIds.length,27);assert.ok(!splitCaseIds.includes('root-empty-selection-reactivated'));
 const bundle=await splitBrowserSourceBundle({caseDefinitions:[splitReactivationCaseDefinition()]});
 const runtime=(0,eval)(bundle.runtime.source)({assert}),loaded=await loadSplitModules();
 try{loaded.createWorker=workerFactory(loaded.root,loaded.manifest);const row=await runtime.runSplitLifecycleCase(loaded,bundle.cases[0]);assert.equal(row.stages.emptied.outcome,false);assert.equal(row.stages.reactivated.outcome,true);assert.equal(row.stages.confirm.outcome,true);}finally{loaded.cleanup();}
});

test('child entry is deterministic when cold readiness polls would otherwise separate duplicate source jobs',async()=>{
 const {loadSplitModules,runSplitLifecycleCase,splitCaseDefinition}=await import('./web-split.mjs');
 const loaded=await loadSplitModules();
 let schedules=0,deferredUntilCompletion=false;
 const productionFactory=loaded.api.createMapEditWorkerClient;
 loaded.api={...loaded.api,createMapEditWorkerClient(options){let client;
  client=productionFactory({...options,schedule(callback,delay){
   if(++schedules!==2)return setTimeout(callback,delay);
   const deadline=Date.now()+3000;
   const poll=()=>{if(client.stats().completed>0){deferredUntilCompletion=true;callback();}else if(Date.now()>deadline)callback();else setTimeout(poll,5);};
   return setTimeout(poll,delay);
  }});return client;
 }};
 try{
  const actual=await runSplitLifecycleCase(loaded,splitCaseDefinition('child-two-crossing'));
  const expected=readSplitObservation().cases.find(row=>row.case==='child-two-crossing');
  assert.deepEqual(actual,expected,'Preserve raw real cancellation diagnostics and all original observations');
  assert.equal(deferredUntilCompletion,false,'A public readiness boundary must prevent independent cold polls from racing');
 }finally{loaded.cleanup();}
});

test('settling drains real interrupted setup-source callbacks before repeated entry',async()=>{
 const {loadSplitModules,createSplitRuntime,splitCaseDefinition,settleSplit}=await import('./web-split.mjs');
 const loaded=await loadSplitModules(),runtime=createSplitRuntime(loaded,splitCaseDefinition('child-two-crossing'));
 try{
  await runtime.prepareWorker();const before=runtime.snapshot();
  assert.equal(runtime.drafts.enterTerritorialUnitSplitMode('source'),true);
  assert.equal(runtime.h.pendingSplitWorkerRequests,2,'Both actual setup-source reads must be in flight');
  runtime.h.workflow.clear();await settleSplit(runtime.h);
  assert.equal(runtime.h.pendingSplitWorkerRequests,0);
  assert.deepEqual(runtime.snapshot().document.document,before.document.document);
  assert.deepEqual(runtime.h.errors,[],'Cancelled session source callbacks must follow production suppression, not leak later');
  assert.equal(runtime.drafts.enterTerritorialUnitSplitMode('source'),true);await settleSplit(runtime.h);
  assert.equal(runtime.h.pendingSplitWorkerRequests,0);assert.equal(runtime.h.workflow.activeSession().setupSourceCache.pending,false);
  assert.deepEqual(runtime.h.errors,[{code:'PL-TERRITORIAL-SOURCE',message:'더 최신 편집 요청으로 대체했습니다.'}]);
 }finally{runtime.h.workflow.clear();await settleSplit(runtime.h);runtime.client.stop();loaded.cleanup();}
});
