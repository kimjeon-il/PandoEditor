// In-memory protocol test data only: Node discovery is never persisted or called a browser golden.
import test from 'node:test';
import assert from 'node:assert/strict';
import {loadNodeSources} from './sources.mjs';
import {createCaptureSuite,verifyBrowserReport,runtimePin} from './protocol.mjs';
test('complete report protocol rejects missing reached stages, worker/draft evidence and unrelated numeric outputs',async()=>{
 const suite=await createCaptureSuite({commit:'a'.repeat(40),runId:'protocol-fixture-only'}),loaded=await loadNodeSources();
 try{
  const {runSnapCase}=(0,eval)(suite.runtime.snap.source),{runBoundaryCase}=(0,eval)(suite.runtime.boundary.source),{runMathDiagnostic}=(0,eval)(suite.runtime.math.source);
  const report={schema:'pando-m974-actual-chromium-workflows',version:1,state:'complete',identity:suite.identity,sourceHashes:Object.fromEntries(suite.manifest.sources.map(row=>[row.path,row.sha256])),runtime:{playwright:runtimePin.playwright,browserVersion:runtimePin.chromium,chromiumRevision:runtimePin.revision,cdp:{jsVersion:runtimePin.v8,product:'HeadlessChrome/'+runtimePin.chromium}},math:runMathDiagnostic(loaded.api,suite.mathInputs),snap:[],boundary:[]};
  for(const definition of suite.inputs.snap)report.snap.push(await runSnapCase(loaded,definition));
  for(const definition of suite.inputs.boundary)report.boundary.push(await runBoundaryCase(loaded,definition));
  assert.equal(verifyBrowserReport(suite,report),report);
  const fullChrome=structuredClone(report);fullChrome.runtime.cdp.product='Chrome/'+runtimePin.chromium;assert.equal(verifyBrowserReport(suite,fullChrome),fullChrome);
  for(const [name,mutate]of [
   ['wrong Chromium version',r=>{r.runtime.cdp.product='HeadlessChrome/150.0.0.0';}],
   ['wrong product',r=>{r.runtime.cdp.product='OtherAgent/'+runtimePin.chromium;}],
   ['positive boundary all unobserved',r=>{for(const key of Object.keys(r.boundary[0].stages))r.boundary[0].stages[key]={observed:false,reason:'missing'};}],
   ['positive boundary commit omitted',r=>{r.boundary[0].stages.confirm={observed:false,reason:'missing'};}],
   ['snap ready draft omitted',r=>{delete r.snap[0].queries[0].ready.draft;}],
   ['snap undo omitted',r=>{delete r.snap[0].queries[0].undo;}],
   ['snap worker response omitted',r=>{delete r.snap[0].requests[0].response;}],
   ['snap lifecycle omitted',r=>{r.snap.find(row=>row.id==='snap-lifecycle-cancel').lifecycle=null;}],
   ['math unrelated zero',r=>{r.math[0].hypot=0;r.math[0].result.distancePx=0;}],
   ['math result coordinate altered',r=>{r.math[0].result.coordinate=[0,0];}],
  ]){const invalid=structuredClone(report);mutate(invalid);assert.throws(()=>verifyBrowserReport(suite,invalid),undefined,name);}
 }finally{loaded.cleanup();}
});
