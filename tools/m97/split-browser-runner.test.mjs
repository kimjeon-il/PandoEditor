import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
const api=await import('./split-browser-runner.mjs').catch(error=>{if(error.code==='ERR_MODULE_NOT_FOUND')return {};throw error;});
const requireApi=()=>{assert.equal(typeof api.createSplitCaptureSuite,'function','same-origin actual-browser capture suite required');return api;};
test('bounded kernel stress exactly preserves independent 250-case payload construction',()=>{
 requireApi();const cases=api.splitKernelStressCases();assert.equal(cases.length,250);assert.equal(cases.filter(c=>c.payload.view.kind==='globe').length,50);
 const hash=createHash('sha256').update(JSON.stringify(cases.map(c=>c.payload))).digest('hex');assert.equal(hash,'fabbbb99853ac3ceaef31e4099c9192b8d66c7c9a30fc3aa8dc77fbda044fe1a');
});
test('split browser identity binds every exact source, case, native seed, runtime and commit',async()=>{
 const {createSplitCaptureSuite,verifySplitCaptureSuite}=requireApi();const suite=await createSplitCaptureSuite({commit:'a'.repeat(40),runId:'protocol-test'});verifySplitCaptureSuite(suite);assert.ok(suite.bundle.cases.length>=25);assert.equal(suite.kernelCases.length,277);assert.equal(suite.nativeCases.length,suite.bundle.cases.length+12);
 for(const mutate of [s=>s.bundle.cases.pop(),s=>s.bundle.sources[Object.keys(s.bundle.sources)[0]].source+=' ',s=>s.nativeCases[0].features[0].geometry.coordinates[0][0][0]++,s=>s.runtimePin.v8='wrong',s=>s.identity.commit='b'.repeat(40),s=>s.kernelCases.pop()]){const bad=structuredClone(suite);mutate(bad);assert.throws(()=>verifySplitCaptureSuite(bad),/identity|hash|source|runtime|pin|case/i);}
});
test('capture page retains actual module paths and actual worker client without Node goldens',async()=>{
 const {createSplitCaptureSuite,createSplitCapturePage}=requireApi();const suite=await createSplitCaptureSuite({commit:'a'.repeat(40),runId:'protocol-test'});const html=createSplitCapturePage(suite);assert.match(html,/map-edit-worker-client\.js/);assert.match(html,/new Worker\(/);assert.match(html,/__splitCaptureReport/);assert.match(html,/territorial-cut/);assert.doesNotMatch(html,/review-fuzz-expected|Node discovery/);
});
test('explicit shared controller fixtures preserve original cases and record every changed input',()=>{
 requireApi();assert.equal(typeof api.splitControllerDefinitions,'function','separate controller fixture declaration required');const variant=api.splitControllerDefinitions();assert.equal(variant.adaptations.length,8);assert.equal(variant.definitions.length,27);assert.equal(variant.originalUnrepresentableCaseIds.length,8);
 const shifted=variant.definitions.find(c=>c.id==='common-root-no-cut');assert.deepEqual(shifted.coords,[[-3,-10],[13,-10]]);assert.deepEqual(shifted.source.coordinates[0],[[0,-5],[0,5],[10,5],[10,-5],[0,-5]]);
 const child=variant.definitions.find(c=>c.id==='common-child-two-crossing');assert.deepEqual(child.coords,[[-1,5],[11,5]]);assert.deepEqual(child.parentGeometry.coordinates[0],[[-10,-30],[-10,30],[30,30],[30,-30],[-10,-30]]);
 assert.ok(variant.definitions.find(c=>c.id==='root-date-line'),'dateline source is not transformed away');
});
test('approved dateline delta is explicit while original browser baseline stays immutable',async()=>{
 const {createSplitCaptureSuite}=requireApi(),suite=await createSplitCaptureSuite({commit:'a'.repeat(40),runId:'correction-test'});
 assert.equal(suite.controllerBundle.behavioralCommit,'ad78780f79f4f38fcd7c2a3c2fb1fe0ba8e37c32');assert.equal(suite.bundle.behavioralCommit,'6c3f930b8573fa09991885b661879ea36725472e');assert.equal(suite.controllerBundle.cases.length,39);assert.equal(suite.controllerBundle.sourceChain.at(-1).behavioralCommit,suite.controllerBundle.behavioralCommit);assert.equal(suite.corrections[0].changes.length,10);assert.equal(suite.corrections[1].changes.length,2);assert.equal(suite.controllerBundle.sourceChain.at(-2).behavioralCommit,'07d3e2053c71573e11c5cf89151f5f6686038511');assert.equal(suite.controllerBundle.testOnlyCandidate,undefined);
});
test('two explicit dateline descendant coordinate variants preserve actual web outcome and ancestry',async()=>{
 requireApi();assert.equal(typeof api.splitDatelineControllerDefinitions,'function','bounded dateline controller variants required');
 const {readFileSync}=await import('node:fs'),{fileURLToPath}=await import('node:url'),{loadSplitCandidateModules}=await import('./web-split-candidate.mjs'),{runSplitLifecycleCase}=await import('./web-split.mjs');const root=fileURLToPath(new URL('../../tests/fixtures/web-m973-split/corrections/dateline/',import.meta.url)),manifest=JSON.parse(readFileSync(root+'manifest.json')),definitions=JSON.parse(readFileSync(root+'case-definitions.json')),variant=api.splitDatelineControllerDefinitions(definitions);assert.equal(variant.adaptations.length,2);
 const loaded=await loadSplitCandidateModules({candidateRoot:root,candidateChanges:manifest.changes});
 try{for(const change of variant.adaptations){assert.deepEqual(change.controller.source,change.original.source);assert.deepEqual(change.controller.coords,change.original.coords);assert.deepEqual(change.controller.dependentFeatures.map(f=>f.parentId),change.original.dependentFeatures.map(f=>f.parentId));
  const a=await runSplitLifecycleCase(loaded,change.original),b=await runSplitLifecycleCase(loaded,change.controller);assert.equal(a.assessment.split.candidates.length,b.assessment.split.candidates.length);for(const stage of ['archive','review','confirm','undo','redo'])assert.equal(a.stages[stage].outcome,b.stages[stage].outcome);assert.equal(a.referenceEffects.createdIds.length,b.referenceEffects.createdIds.length);assert.deepEqual(a.referenceEffects.deletedIds,b.referenceEffects.deletedIds);assert.deepEqual(a.referenceEffects.reparentedIds,b.referenceEffects.reparentedIds);
 }}finally{loaded.cleanup();}
});
test('self-consistent rehashing cannot replace any pinned production module',async()=>{
 const {createSplitCaptureSuite,verifySplitCaptureSuite}=requireApi(),suite=await createSplitCaptureSuite({commit:'a'.repeat(40),runId:'source-binding-test'}),hash=value=>createHash('sha256').update(typeof value==='string'?value:JSON.stringify(value)).digest('hex');
 for(const [key,identityKey,sourceKey]of [['bundle','bundleSha256','sourceHashes'],['controllerBundle','controllerBundleSha256','controllerSourceHashes']]){const changed=structuredClone(suite),name='assets/js/modules/app-territory-selection-workflow.js';changed[key].sources[name].source+='\n// unapproved bytes';changed[key].sources[name].sha256=hash(changed[key].sources[name].source);changed.identity[identityKey]=hash(changed[key]);changed.identity[sourceKey][name]=changed[key].sources[name].sha256;assert.throws(()=>verifySplitCaptureSuite(changed),/pin|source|approved/i);}
});
test('the browser harness is bound to the real current entrypoint recorder',async()=>{
 const {createSplitCaptureSuite,verifySplitCaptureSuite}=requireApi(),suite=await createSplitCaptureSuite({commit:'a'.repeat(40),runId:'harness-binding-test'}),hash=value=>createHash('sha256').update(typeof value==='string'?value:JSON.stringify(value)).digest('hex');suite.controllerBundle.runtime.source+='\n// replaced recorder';suite.controllerBundle.runtime.sha256=hash(suite.controllerBundle.runtime.source);suite.identity.controllerBundleSha256=hash(suite.controllerBundle);assert.throws(()=>verifySplitCaptureSuite(suite),/harness|runtime/i);
});
test('detached artifacts cannot rehash unequal native or original geographic inputs',async()=>{
 const {createSplitCaptureSuite,verifySplitCaptureSuite}=requireApi(),suite=await createSplitCaptureSuite({commit:'a'.repeat(40),runId:'exact-input-binding'}),hash=value=>createHash('sha256').update(JSON.stringify(value)).digest('hex');
 for(const mutate of [s=>{s.nativeCases.find(c=>c.case==='common-root-no-cut').definition.coords=[[-30,-20],[130,-20]];s.identity.nativeCasesSha256=hash(s.nativeCases);},s=>{s.originalNativeCases[0].definition.coords=[[-30,-20],[130,-20]];s.identity.originalNativeCasesSha256=hash(s.originalNativeCases);},s=>{s.bundle.cases[0].coords[0][0]-=1;s.kernelCases.find(c=>c.id==='original-input-'+s.bundle.cases[0].id).payload.coords=s.bundle.cases[0].coords;s.identity.bundleSha256=hash(s.bundle);s.identity.kernelCasesSha256=hash(s.kernelCases);}]){const changed=JSON.parse(JSON.stringify(suite));mutate(changed);assert.throws(()=>verifySplitCaptureSuite(changed),/input|definition|geographic|case/i);}
});
