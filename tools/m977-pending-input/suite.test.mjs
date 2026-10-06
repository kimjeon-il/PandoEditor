import assert from 'node:assert/strict';
import test from 'node:test';
import {loadPendingNodeSources,sha256} from './sources.mjs';
import {runPendingInputCase} from './runtime.mjs';
const mod=await import('./suite.mjs').catch(error=>{if(error.code!=='ERR_MODULE_NOT_FOUND')throw error;return {};});
const options={commit:'a'.repeat(40),runId:'123456789'};
const pin={playwright:'1.62.1',chromium:'151.0.7922.34',revision:'1234',v8:'15.1.206.8',v8Commit:'f479186c16abdb6fa05539fe957bb84deee830df',qt:'6.8.3'};
// Metadata below is a verifier fixture only. No test launches or captures Chromium.
const chromiumFixture=()=>({collector:'actual-chromium',playwright:pin.playwright,browserVersion:pin.chromium,chromiumRevision:pin.revision,cdp:{product:'HeadlessChrome/'+pin.chromium,jsVersion:pin.v8},ci:{commit:options.commit,runId:options.runId}});
const contextFixture=definition=>({case:definition.id,viewport:{width:definition.profile.width,height:definition.profile.height},userAgent:'Mozilla/5.0 HeadlessChrome/'+pin.chromium+' Safari/537.36',devicePixelRatio:1,hasTouch:definition.profile.pointerType==='touch',node:false,document:true,worker:true});
let fixture;
async function fixtureReport(){
 if(fixture)return structuredClone(fixture);
 const suite=await mod.createPendingSuite(options),loaded=await loadPendingNodeSources();
 try{
  const cases=[];for(const definition of suite.corpus.cases)cases.push(await runPendingInputCase(loaded,definition,suite.corpus));
  fixture={suite,report:{schema:'pando-m977-actual-chromium-pending-input',version:1,state:'complete',identity:suite.identity,sourceHashes:Object.fromEntries(Object.entries(suite.sources).map(([name,source])=>[name,sha256(source)])),runtime:chromiumFixture(),contexts:suite.corpus.cases.map(contextFixture),cases,observationLimits:mod.pendingObservationLimits}};
  return structuredClone(fixture);
 }finally{loaded.cleanup();}
}
test('pending suite binds the exact source closure, corpus, runtime and harness bytes',async()=>{
 assert.equal(typeof mod.createPendingSuite,'function','Separate pending-input suite required');
 const suite=await mod.createPendingSuite(options);assert.equal(mod.verifyPendingSuite(suite),suite);assert.deepEqual(mod.runtimePin,pin);
 assert.equal(suite.corpus.cases.length,14);assert.equal(suite.identity.stageCount,86);assert.equal(Object.keys(suite.sources).length,107);
 assert.equal(suite.identity.commit,options.commit);assert.equal(suite.identity.runId,options.runId);
 assert.equal(suite.addendum.supplementary.blob,'e841223c2c6ba6232476726039eed02f955a5f1c');
 const runtime=Function('return '+suite.runtime.source)();assert.equal(typeof runtime.createRuntime,'function');assert.equal(typeof runtime.selectionRuntime.createSelectionRuntime,'function');
 assert.equal(typeof Function('return '+suite.pendingRuntime.source)(),'function');
 assert.ok(suite.harnessFiles['browser-runner.mjs']);assert.ok(suite.harnessFiles['runtime.mjs']);
 assert.equal(Object.keys(suite.harnessFiles).some(name=>name==='node-runner.mjs'||name==='compare.mjs'),false,'Concurrent collectors do not race this binding');
});
test('suite fails closed on mutated source, source identity, corpus, runtime or harness',async()=>{
 assert.equal(typeof mod.verifyPendingSuite,'function');const suite=await mod.createPendingSuite(options);
 for(const mutate of [s=>s.sources[s.addendum.supplementary.path]+='\n',s=>s.sources[Object.keys(s.sources)[0]]+='\n',s=>s.addendum.supplementary.blob='0'.repeat(40),s=>s.corpus.cases.pop(),s=>s.runtime.source+=' ',s=>s.pendingRuntime.source+=' ',s=>s.runtimePin.v8='untrusted',s=>s.identity.commit='b'.repeat(40),s=>s.harnessFiles['runtime.mjs']='0'.repeat(64)]){
  const bad=structuredClone(suite);mutate(bad);assert.throws(()=>mod.verifyPendingSuite(bad));
 }
 await assert.rejects(()=>mod.createPendingSuite({...options,runId:'local-diagnostic'}));
});
test('mutable suite metadata cannot rewrite the approved runtime pin',async()=>{
 const suite=await mod.createPendingSuite(options),original=mod.runtimePin.v8;
 try{
  suite.runtimePin.v8='untrusted';
  assert.equal(mod.runtimePin.v8,pin.v8,'Suite runtime metadata must not alias the trusted pin');
  assert.throws(()=>mod.verifyPendingSuite(suite));
 }finally{suite.runtimePin.v8=original;}
 assert.deepEqual((await mod.createPendingSuite(options)).runtimePin,pin);
});
test('strict report verification rejects missing evidence, Node labels and mismatched runtime identity',async()=>{
 assert.equal(typeof mod.verifyPendingReport,'function');const {suite,report}=await fixtureReport();assert.equal(mod.verifyPendingReport(suite,report),report);
 const mutations=[
  ['missing case',r=>r.cases.pop()],['missing stage',r=>delete r.cases[0].stages.activation],['changed canonical',r=>r.cases[0].stages.ready.canonical+=' '],
  ['missing actual Worker events',r=>r.cases[0].transport=[]],['wrong source hash',r=>r.sourceHashes[Object.keys(r.sourceHashes)[0]]='0'.repeat(64)],
  ['wrong commit',r=>r.identity.commit='b'.repeat(40)],['wrong run',r=>r.runtime.ci.runId='1'],['wrong pin',r=>r.runtime.playwright='1.62.2'],['wrong V8',r=>r.runtime.cdp.jsVersion='fake'],
  ['wrong product',r=>r.runtime.cdp.product='Node/'+pin.chromium],['missing runtime',r=>delete r.runtime],['Node collector',r=>r.runtime.collector='node'],
  ['Node browser label',r=>r.contexts[0].userAgent='unit-verifier-not-browser'],['Node exposed',r=>r.contexts[0].node=true],
  ['missing context',r=>r.contexts.pop()],['wrong viewport',r=>r.contexts[7].viewport.width=1024],['missing real Worker capability',r=>r.contexts[0].worker=false],
  ['touch profile lost',r=>r.contexts[7].hasTouch=false],['false DOM scope',r=>r.observationLimits.fullDOM=true],['unknown raw report field',r=>r.nodeVersion=process.version],
 ];
 for(const [name,mutate]of mutations){const bad=structuredClone(report);mutate(bad);assert.throws(()=>mod.verifyPendingReport(suite,bad),undefined,name);}
});
test('capture binding refuses changed raw bytes, detached suite hashes or incomplete summaries',async()=>{
 assert.equal(typeof mod.createPendingCaptureBinding,'function');const {suite,report}=await fixtureReport(),suiteText=JSON.stringify(suite),reportText=JSON.stringify(report);
 const binding=mod.createPendingCaptureBinding(suiteText,reportText);assert.equal(mod.verifyPendingCapture(suiteText,reportText,binding).cases.length,14);
 assert.equal(binding.suiteDecompressedSha256,sha256(suiteText));assert.equal(binding.reportDecompressedSha256,sha256(reportText));assert.equal(binding.stages,86);assert.equal(binding.rawParity,false);
 assert.throws(()=>mod.verifyPendingCapture(suiteText+' ',reportText,binding));assert.throws(()=>mod.verifyPendingCapture(suiteText,reportText+' ',binding));
 for(const mutate of [b=>b.identity.runId='123',b=>b.passed=false,b=>b.cases=13,b=>b.stages=85,b=>delete b.reportDecompressedBytes,b=>b.rawParity=true]){const bad=structuredClone(binding);mutate(bad);assert.throws(()=>mod.verifyPendingCapture(suiteText,reportText,bad));}
});
