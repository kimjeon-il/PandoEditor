import test from 'node:test';
import assert from 'node:assert/strict';
import {loadNodeSources} from './sources.mjs';
import {runBoundaryCase} from './boundary-runtime.mjs';
const mod=await import('./supplemental.mjs').catch(error=>{if(error.code!=='ERR_MODULE_NOT_FOUND')throw error;return {};});
test('separate supplemental corpus covers exact threshold neighbors and unequal shared-node coordinates',async()=>{
 assert.equal(typeof mod.supplementalCases,'function','Supplemental input builder must exist');const loaded=await loadNodeSources();try{
  const cases=mod.supplementalCases(loaded.api);assert.equal(cases.length,12);assert.equal(new Set(cases.map(c=>c.id)).size,12);assert.ok(cases.every(c=>c.id.startsWith('supp-v1-')));
  loaded.runBoundaryCase=runBoundaryCase;
  for(const id of ['supp-v1-owner-already-destination-4e-10','supp-v1-owner-already-destination-4e-8']){
   const definition=cases.find(c=>c.id===id),result=await mod.runSupplementalCase(loaded,definition);
   assert.equal(result.directPreparation.observed,true);assert.equal(result.directMove.observed,true);assert.equal(result.directMove.ok,true);
   assert.deepEqual(result.directMove.result.affectedIds.sort(),['A','B']);assert.deepEqual(result.changedOwnerIds,['A']);
   if(id.endsWith('4e-10'))assert.equal(result.workflow.stages.preview.observed,false);
  }
 }finally{loaded.cleanup();}
});
test('supplemental portable runtime records all12 outcomes and rejects rewritten request states',async()=>{
 const suite=await mod.createSupplementalSuite({commit:'a'.repeat(40),runId:'unit-supplement-only'}),loaded=await loadNodeSources();try{
  loaded.runBoundaryCase=runBoundaryCase;const runtime=(0,eval)(suite.runtime.source),cases=[];
  for(const definition of suite.cases)cases.push(await runtime.runSupplementalCase(loaded,definition));
  const {runtimePin}=await import('./protocol.mjs'),report={schema:'pando-m974-actual-chromium-supplemental',version:1,state:'complete',identity:suite.identity,sourceHashes:Object.fromEntries(suite.base.manifest.sources.map(row=>[row.path,row.sha256])),runtime:{playwright:runtimePin.playwright,browserVersion:runtimePin.chromium,chromiumRevision:runtimePin.revision,cdp:{product:'HeadlessChrome/'+runtimePin.chromium,jsVersion:runtimePin.v8}},cases,observationLimits:{rawParity:false,geographicOnly:true,originalCorpusUnchanged:true,UIAndDirectWorkerAreSeparate:true}};
  assert.equal(mod.verifySupplementalReport(suite,report),report);
  const changed=structuredClone(report);changed.cases[0].directPreparation={observed:true,ok:false,error:{message:'invented'}};changed.cases[0].directMove={observed:false,reason:'invented'};
  assert.throws(()=>mod.verifySupplementalReport(suite,changed));
  const omitted=structuredClone(report);delete omitted.cases[0].topology.nodes[0].ownerIds;assert.throws(()=>mod.verifySupplementalReport(suite,omitted));
  const missing=structuredClone(report);missing.cases.pop();assert.throws(()=>mod.verifySupplementalReport(suite,missing));
  for(const [name,mutate]of [
   ['workflow stage deletion',r=>{for(const key of ['prepared','drag','preview','cancel','confirm','undo','redo'])delete r.cases[0].workflow.stages[key];}],
   ['changed-owner tampering',r=>{r.cases.at(-1).changedOwnerIds=['A','B'];}],
   ['request envelopes omitted',r=>{r.cases[0].transport=r.cases[0].transport.filter(event=>event.direction!=='request');}],
   ['invented preparation rejection',r=>{const row=r.cases[0];row.requests=[{operation:'boundary-prepare',payload:{targetIds:row.input.selectedIds,mode:'border'},status:'rejected',error:{message:'invented'}}];row.directPreparation={observed:true,ok:false,error:{message:'invented'}};row.directMove={observed:false,reason:'invented'};row.transport=[];}],
   ['separate-path label false',r=>{r.cases[0].observationLimits.UIAndDirectWorkerAreSeparate=false;}],
  ]){const invalid=structuredClone(report);mutate(invalid);assert.throws(()=>mod.verifySupplementalReport(suite,invalid),undefined,name);}

 }finally{loaded.cleanup();}
});
test('supplemental suite rejects changed inputs and provides a browser-only capture page',async()=>{
 const suite=await mod.createSupplementalSuite({commit:'a'.repeat(40),runId:'unit-supplement-only'});
 const {createSupplementalPage}=await import('./supplemental-browser-runner.mjs');const html=createSupplementalPage(suite);
 assert.ok(html.includes('DecompressionStream'));assert.ok(html.includes('__m974Report'));assert.ok(!html.includes('node:'));
 const changed=structuredClone(suite);changed.cases[0].move.coordinate[0]+=1;assert.throws(()=>mod.verifySupplementalSuite(changed));
 const missing=structuredClone(suite);delete missing.base.sources['assets/js/modules/boundary-topology.js'];assert.throws(()=>mod.verifySupplementalSuite(missing));
});
