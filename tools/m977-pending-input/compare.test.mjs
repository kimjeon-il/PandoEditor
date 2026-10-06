import assert from 'node:assert/strict';
import test from 'node:test';
import {spawnSync} from 'node:child_process';
import {loadPendingNodeSources,readPendingSources} from './sources.mjs';
import {runPendingInputCase} from './runtime.mjs';
const mod=await import('./compare.mjs').catch(error=>{if(error.code!=='ERR_MODULE_NOT_FOUND')throw error;return {};});
const probe=process.env.PANDO_M977_NATIVE_PROBE;
test('comparison has separate fail-closed native verification and explicitly diagnostic mode',()=>{
 assert.equal(typeof mod.verifyNativeReport,'function');assert.equal(typeof mod.comparePendingCollections,'function');assert.equal(typeof mod.parseArguments,'function');
 assert.throws(()=>mod.parseArguments(['--capture','a','b','c','d','e']));
 assert.throws(()=>mod.parseArguments(['--diagnostic','a','b','c','--native-binary','missing','--native-commit','a'.repeat(40)]));
});
test('matched real native and Node runs expose differences without inventing absent decision stages',{skip:!probe},async()=>{
 assert.equal(typeof mod.comparePendingCollections,'function');
 const loaded=await loadPendingNodeSources();
 try{
  const web=[];for(const definition of loaded.bundle.corpus.cases)web.push(await runPendingInputCase(loaded,definition,loaded.bundle.corpus));
  const run=spawnSync(probe,[new URL('../../tests/fixtures/web-m977-pending-input/corpus.json',import.meta.url).pathname],{encoding:'utf8',timeout:120000,maxBuffer:8*1024*1024,env:{...process.env,QT_QPA_PLATFORM:'offscreen',QT_QUICK_BACKEND:'software'}});
  assert.ifError(run.error);assert.equal(run.status,0,run.stderr);const native=JSON.parse(run.stdout);
  const result=mod.comparePendingCollections(loaded.bundle,{cases:web},native,{mode:'node-diagnostic'});
  assert.equal(result.integrityPassed,true);assert.equal(result.rawParity,false);assert.equal(result.gateAcceptance,false);assert.equal(result.pairedCases,14);assert.equal(result.requestedStages,86);assert.equal(result.pairedStages,80);assert.equal(result.unsupportedStages.length,6);assert.equal(result.actualChromium,false);
  for(const id of ['desktop','mobile-360']){
   const row=result.cases.find(c=>c.case==='two-pending-empty-switch-'+id);
   assert.ok(row.differences.some(d=>d.stage==='switch'&&d.field==='confirmationMethod'&&d.web==='line'&&d.native===null));
   assert.equal(row.unsupportedStages.length,3);assert.equal(row.pendingTaps.every(t=>t.webPointCount===0&&t.nativePointCount===0),true);
  }
  for(const badMode of [undefined,'actual-chromium','browser'])assert.throws(()=>mod.comparePendingCollections(loaded.bundle,{cases:web},native,{mode:badMode}));
  const bad=structuredClone(native);bad.cases[1].inputObservations[1].intended=[2,2];assert.throws(()=>mod.comparePendingCollections(loaded.bundle,{cases:web},bad,{mode:'node-diagnostic'}));
  const corrupt=structuredClone(web);corrupt[0].stages.ready.canonical+=' ';assert.throws(()=>mod.comparePendingCollections(loaded.bundle,{cases:corrupt},native,{mode:'node-diagnostic'}));
 }finally{loaded.cleanup();}
});
