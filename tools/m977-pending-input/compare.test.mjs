import assert from 'node:assert/strict';
import test from 'node:test';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {loadPendingNodeSources,readPendingSources} from './sources.mjs';
import {runPendingInputCase} from './runtime.mjs';
const mod=await import('./compare.mjs').catch(error=>{if(error.code!=='ERR_MODULE_NOT_FOUND')throw error;return {};});
const probe=process.env.PANDO_M977_NATIVE_PROBE;
test('comparison has separate fail-closed native verification and explicitly diagnostic mode',()=>{
 assert.equal(typeof mod.verifyNativeReport,'function');assert.equal(typeof mod.comparePendingCollections,'function');assert.equal(typeof mod.parseArguments,'function');
 assert.throws(()=>mod.parseArguments(['--capture','a','b','c','d','e']));
 assert.throws(()=>mod.parseArguments(['--diagnostic','a','b','c','--native-binary','missing','--native-commit','a'.repeat(40)]));
});
test('matched real native and Node runs observe empty-ready decisions while retaining raw differences',{skip:!probe},async()=>{
 assert.equal(typeof mod.comparePendingCollections,'function');
 const loaded=await loadPendingNodeSources();
 try{
  const web=[];for(const definition of loaded.bundle.corpus.cases)web.push(await runPendingInputCase(loaded,definition,loaded.bundle.corpus));
  const run=spawnSync(probe,[fileURLToPath(new URL('../../tests/fixtures/web-m977-pending-input/corpus.json',import.meta.url))],{encoding:'utf8',timeout:120000,maxBuffer:8*1024*1024,env:{...process.env,QT_QPA_PLATFORM:'offscreen',QT_QUICK_BACKEND:'software'}});
  assert.ifError(run.error);assert.equal(run.status,0,run.stderr);const native=JSON.parse(run.stdout);
  const result=mod.comparePendingCollections(loaded.bundle,{cases:web},native,{mode:'node-diagnostic'});
  assert.equal(result.integrityPassed,true);assert.equal(result.rawParity,false);assert.equal(result.gateAcceptance,false);assert.equal(result.pairedCases,14);assert.equal(result.requestedStages,86);assert.equal(result.pairedStages,86);assert.equal(result.unsupportedStages.length,0);assert.equal(result.actualChromium,false);
  const assertEmptyReadyDecisions=(observations,comparison)=>{
   for(const id of ['desktop','mobile-360']){
    const name='two-pending-empty-switch-'+id,row=comparison.cases.find(c=>c.case===name),actual=observations.cases.find(c=>c.case===name);
    assert.equal(row.unsupportedStages.length,0,'all empty-ready decision stages are observed');
    for(const [stage,method,confirmation]of [['switch','polygon','line'],['cancelSwitch','polygon',null],['switchAgain','polygon','line'],['confirmSwitch','line',null]]){
     const state=actual.stages[stage];assert.equal(state.observed,true);assert.equal(state.method,method);
     assert.equal(state.confirmation?.requestedMethod??null,confirmation,'empty-ready '+stage+' confirmation');
     assert.deepEqual(state.coordinates,[]);assert.equal(state.draftUndoAvailable,false);assert.equal(state.draftRedoAvailable,false);
    }
    // These raw phase and return-time scheduling differences remain visible;
    // there is no remaining method/confirmation/coordinate/history difference.
    assert.deepEqual(row.differences,[
     ...['activation','firstPending','secondPending','held'].map(stage=>({stage,field:'phase',web:'preparing',native:'drawing'})),
     {stage:'confirmSwitch',field:'pending',web:false,native:true},
    ]);
    assert.equal(row.pendingTaps.every(t=>t.webPointCount===0&&t.nativePointCount===0),true);
   }
  };
  assertEmptyReadyDecisions(native,result);
  for(const id of ['desktop','mobile-360']){
   const missing=structuredClone(native),row=missing.cases.find(c=>c.case==='two-pending-empty-switch-'+id);
   // Reuse a real immediate line-activation observation to model the old
   // controller bug, with its causally unavailable decisions. Integrity must
   // still accept honest observations, but the fixed-behavior regression must not.
   row.stages.switch=structuredClone(row.stages.confirmSwitch);
   row.stages.switch.outcome={action:'geometrySelectTerritoryMethod',method:'line',accepted:true};
   for(const stage of ['cancelSwitch','switchAgain','confirmSwitch'])row.stages[stage]={observed:false,reason:'Mutation: empty-ready switch skipped the confirmation.'};
   const missingResult=mod.comparePendingCollections(loaded.bundle,{cases:web},missing,{mode:'node-diagnostic'});
   assert.equal(missingResult.integrityPassed,true);
   assert.throws(()=>assertEmptyReadyDecisions(missing,missingResult),/all empty-ready decision stages are observed/);
  }
  for(const badMode of [undefined,'actual-chromium','browser'])assert.throws(()=>mod.comparePendingCollections(loaded.bundle,{cases:web},native,{mode:badMode}));
  const bad=structuredClone(native);bad.cases[1].inputObservations[1].intended=[2,2];assert.throws(()=>mod.comparePendingCollections(loaded.bundle,{cases:web},bad,{mode:'node-diagnostic'}));
  const corrupt=structuredClone(web);corrupt[0].stages.ready.canonical+=' ';assert.throws(()=>mod.comparePendingCollections(loaded.bundle,{cases:corrupt},native,{mode:'node-diagnostic'}));
 }finally{loaded.cleanup();}
});
