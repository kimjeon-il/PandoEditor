import test from 'node:test';
import assert from 'node:assert/strict';
const module=await import('./protocol.mjs').catch(error=>{if(error.code!=='ERR_MODULE_NOT_FOUND')throw error;return {};});
test('capture protocol requires exact commit, source, input and runtime identity',async()=>{
 assert.equal(typeof module.createCaptureSuite,'function','Strict capture protocol must exist');
 const suite=await module.createCaptureSuite({commit:'a'.repeat(40),runId:'unit-test-only'});
 assert.equal(module.verifyCaptureSuite(suite),suite);
 for(const mutate of [s=>s.commit='b'.repeat(40),s=>s.inputs.snap.pop(),s=>s.sources['assets/js/modules/geometry-snap.js']+='\n',s=>s.runtimePin.chromium='0',s=>s.runtime.snap.source+='\n']){
  const invalid=structuredClone(suite);mutate(invalid);assert.throws(()=>module.verifyCaptureSuite(invalid));
 }
});
test('missing native observations cannot pass as equality and report scopes stay explicit',()=>{
 assert.equal(typeof module.compareSnapProbe,'function','Exact helper comparison must exist');
 const expected={case:'protocol-only',queryIndex:0,candidates:[{kind:'vertex',coordinate:[1,2],ownerIds:['a'],nodeKey:'1,2'}],result:null,indicator:null};
 assert.equal(module.compareSnapProbe([expected],[]).passed,false);
 assert.equal(module.compareSnapProbe([expected],[{case:'protocol-only',queryIndex:0,candidates:expected.candidates}]).passed,false);
 assert.equal(module.compareSnapProbe([expected],[structuredClone(expected)]).passed,true);
 const altered=structuredClone(expected);altered.candidates[0].ownerIds=['b'];
 assert.equal(module.compareSnapProbe([expected],[altered]).passed,false);
 assert.equal(module.compareSnapProbe([expected],[expected,expected]).passed,false);
});
test('numeric diagnostics retain the exact reported hypot pair and real resolver call',async()=>{
 const suite=await module.createCaptureSuite({commit:'a'.repeat(40),runId:'protocol-test-only'});
 assert.ok(Array.isArray(suite.mathInputs),'Browser numerical diagnostic inputs must exist');
 assert.equal(suite.mathInputs.length,101);assert.deepEqual(suite.mathInputs[0].coordinate,[1.75916951848194,2.823091015452519]);
 const runtime=(0,eval)(suite.runtime.math.source);
 const {loadNodeSources}=await import('./sources.mjs');const loaded=await loadNodeSources();try{
  const output=runtime.runMathDiagnostic(loaded.api,suite.mathInputs);
  assert.equal(output.length,101);assert.equal(output[0].hypot,output[0].result.distancePx);
 }finally{loaded.cleanup();}
});
test('rehashed suite cannot replace callback extraction provenance',async()=>{
 const suite=await module.createCaptureSuite({commit:'a'.repeat(40),runId:'protocol-test-only'});
 suite.boundaryExtraction.byteStart=0;suite.boundaryExtraction.sha256='0'.repeat(64);suite.identity=module.captureIdentity(suite);
 assert.throws(()=>module.verifyCaptureSuite(suite),/extraction/);
});
test('official native probe input never includes expected candidate or resolver outputs',async()=>{
 const {nativeProbeInput}=await import('./compare-browser-native.mjs');
 const input=nativeProbeInput({case:'protocol',queryIndex:0,features:[],payload:{coordinate:[0,0]},projectedPoints:[],candidates:[{kind:'vertex'}],result:{coordinate:[1,2]},indicator:{kind:'vertex'}});
 for(const name of ['candidates','result','indicator'])assert.equal(Object.hasOwn(input,name),false);
 assert.deepEqual(input.payload,{coordinate:[0,0]});
});
