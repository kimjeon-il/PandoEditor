import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {isDeepStrictEqual} from 'node:util';
import {fixtureRoot,readPinnedSources,verifyPinnedSources,loadNodeSources,lifecycleRuntimeSource,sha256} from './sources.mjs';
import {runSnapCase,snapCases} from './snap-runtime.mjs';
import {boundaryCases,boundaryRuntimeSource,extractBoundaryCallbacks} from './boundary-runtime.mjs';
import {runtimePin} from '../m97/river/suite.mjs';
export {runtimePin};
const inputManifestPin='55692789797ecc6667b1766e0793368638e53c2eddeb0005722a0bf7e535c01d';
const hash=value=>sha256(JSON.stringify(value));
export async function writeInputs(){
 const loaded=await loadNodeSources();try{
  const files={};for(const [kind,cases]of [['snap',snapCases(loaded.api)],['boundary',boundaryCases(loaded.api)]]){
   const bytes=Buffer.from(JSON.stringify({schema:`pando-m974-${kind}-inputs`,version:1,cases},null,2)+'\n');
   fs.writeFileSync(path.join(fixtureRoot,kind+'-cases.json'),bytes);files[kind]={file:kind+'-cases.json',sha256:sha256(bytes),bytes:bytes.length,orderedCaseIds:cases.map(c=>c.id)};
  }
  const manifest={schema:'pando-m974-workflow-input-pin',version:1,role:'Synthetic inputs only. No calculated browser golden values.',files};
  fs.writeFileSync(path.join(fixtureRoot,'input-manifest.json'),JSON.stringify(manifest,null,2)+'\n');return {inputManifestSha256:hash(manifest),counts:Object.fromEntries(Object.entries(files).map(([k,v])=>[k,v.orderedCaseIds.length]))};
 }finally{loaded.cleanup();}
}
export function readInputs(){
 const manifest=JSON.parse(fs.readFileSync(path.join(fixtureRoot,'input-manifest.json'),'utf8'));
 assert.equal(hash(manifest),inputManifestPin,'immutable synthetic input manifest pin');
 const inputs={};for(const [kind,item]of Object.entries(manifest.files)){
  const bytes=fs.readFileSync(path.join(fixtureRoot,item.file));assert.equal(bytes.length,item.bytes);assert.equal(sha256(bytes),item.sha256,'synthetic input hash');
  const value=JSON.parse(bytes);assert.equal(value.schema,`pando-m974-${kind}-inputs`);inputs[kind]=value.cases;assert.deepEqual(value.cases.map(c=>c.id),item.orderedCaseIds);assert.equal(new Set(item.orderedCaseIds).size,item.orderedCaseIds.length);
 }return {manifest,inputs};
}
export function mathInputs(){let seed=974;const random=()=>((seed=Math.imul(seed,1664525)+1013904223|0)>>>0)/2**32;return [{id:'reported-hypot-rounding',coordinate:[1.75916951848194,2.823091015452519]},...Array.from({length:100},(_,index)=>({id:'hypot-seeded-'+index,coordinate:[(random()-0.5)*9,(random()-0.5)*9]}))];}
export function runMathDiagnostic(api,inputs){return inputs.map(input=>({id:input.id,coordinate:input.coordinate,hypot:Math.hypot(...input.coordinate),result:api.resolveSnap({coordinate:[0,0],screenPoint:[0,0],candidates:[{kind:'vertex',coordinate:input.coordinate,ownerIds:['diagnostic'],nodeKey:input.id}],project:coordinate=>coordinate,pointerType:'mouse'})}));}
function harnessFileHashes(){return Object.fromEntries(['sources.mjs','protocol.mjs','browser-runner.mjs','snap-runtime.mjs','boundary-runtime.mjs','worker-host.mjs'].map(name=>[name,sha256(fs.readFileSync(new URL(name,import.meta.url)))]));}
function currentRuntime(){return Object.fromEntries(Object.entries({lifecycle:lifecycleRuntimeSource(),snap:`({runSnapCase:${runSnapCase.toString()}})`,boundary:boundaryRuntimeSource(),math:`({runMathDiagnostic:${runMathDiagnostic.toString()}})`}).map(([name,source])=>[name,{source,sha256:sha256(source)}]));}
export function captureIdentity(suite){return {schema:suite.schema,version:suite.version,commit:suite.commit,runId:suite.runId,runtimePin:suite.runtimePin,sourceManifestSha256:hash(suite.manifest),sourceSha256:hash(suite.sources),harnessFiles:suite.harnessFiles,mathInputsSha256:hash(suite.mathInputs),inputManifestSha256:hash(suite.inputManifest),inputsSha256:hash(suite.inputs),runtimeHashes:Object.fromEntries(Object.entries(suite.runtime).map(([key,row])=>[key,row.sha256])),boundaryExtractionSha256:hash(suite.boundaryExtraction),orderedSnapIds:suite.inputs.snap.map(row=>row.id),orderedBoundaryIds:suite.inputs.boundary.map(row=>row.id)};}
export async function createCaptureSuite({commit,runId}={}){
 assert.match(commit||'',/^[a-f0-9]{40}$/,'Exact application commit required');assert.ok(runId,'Run identity required');
 const {manifest,sources}=readPinnedSources(),{manifest:inputManifest,inputs}=readInputs(),extraction=await extractBoundaryCallbacks(sources['assets/js/modules/app-domain-assembly.js']);
 const boundaryExtraction=Object.fromEntries(Object.entries(extraction).filter(([key])=>key!=='source'));
 const suite={schema:'pando-m974-capture-suite',version:1,commit,runId,runtimePin,manifest,sources,inputManifest,inputs,mathInputs:mathInputs(),harnessFiles:harnessFileHashes(),runtime:currentRuntime(),boundaryExtraction};suite.identity=captureIdentity(suite);return suite;
}
export function verifyCaptureSuite(suite){
 assert.equal(suite.schema,'pando-m974-capture-suite');assert.equal(suite.version,1);assert.match(suite.commit,/^[a-f0-9]{40}$/);assert.ok(suite.runId);
 assert.deepEqual(suite.runtimePin,runtimePin,'actual runtime pin');verifyPinnedSources({manifest:suite.manifest,sources:suite.sources});
 const pinned=readInputs();assert.deepEqual(suite.inputManifest,pinned.manifest);assert.deepEqual(suite.inputs,pinned.inputs,'exact input corpus');assert.deepEqual(suite.runtime,currentRuntime(),'exact harness runtime');
 const source=suite.sources['assets/js/modules/app-domain-assembly.js'],start=source.indexOf('        beginBoundaryGesture: event => {'),end=source.indexOf('        renderPacket: () => {');
 assert.ok(start>=0&&end>start,'unique production callback extraction seam');
 const expectedExtraction={sourcePath:'assets/js/modules/app-domain-assembly.js',names:['beginBoundaryGesture','moveBoundaryGesture','commitBoundaryGesture'],sourceSha256:sha256(source),sha256:sha256(source.slice(start,end)),byteStart:Buffer.byteLength(source.slice(0,start)),byteEnd:Buffer.byteLength(source.slice(0,end))};
 assert.deepEqual(suite.boundaryExtraction,expectedExtraction,'independent callback extraction provenance');
 assert.deepEqual(suite.mathInputs,mathInputs());assert.deepEqual(suite.harnessFiles,harnessFileHashes(),'full harness source hashes');
 assert.deepEqual(suite.identity,captureIdentity(suite),'capture identity');return suite;
}
export function verifyBrowserReport(suite,report){
 verifyCaptureSuite(suite);assert.equal(report.schema,'pando-m974-actual-chromium-workflows');assert.equal(report.version,1);assert.equal(report.state,'complete');assert.deepEqual(report.identity,suite.identity);
 assert.equal(report.runtime.playwright,runtimePin.playwright);assert.equal(report.runtime.browserVersion,runtimePin.chromium);assert.equal(report.runtime.chromiumRevision,runtimePin.revision);assert.equal(report.runtime.cdp.jsVersion,runtimePin.v8);assert.ok(['Chrome/','HeadlessChrome/'].some(prefix=>report.runtime.cdp.product===prefix+runtimePin.chromium),'exact pinned Chromium product/version');
 assert.deepEqual(report.math.map(row=>({id:row.id,coordinate:row.coordinate})),suite.mathInputs,'complete numeric diagnostics');
 for(const row of report.math){
  assert.ok(Number.isFinite(row.hypot));assert.ok(row.hypot>=Math.max(...row.coordinate.map(Math.abs))&&row.hypot<=row.coordinate.reduce((sum,v)=>sum+Math.abs(v),0),'hypot must correspond to its nonzero input');
  assert.equal(row.result.kind,'vertex');assert.deepEqual(row.result.coordinate,row.coordinate);assert.equal(row.result.nodeKey,row.id);assert.deepEqual(row.result.ownerIds,['diagnostic']);assert.equal(row.hypot,row.result.distancePx);
 }
 assert.deepEqual(report.sourceHashes,Object.fromEntries(suite.manifest.sources.map(row=>[row.path,row.sha256])),'browser observed source closure');
 assert.deepEqual(report.snap.map(row=>row.id),suite.inputs.snap.map(row=>row.id),'complete ordered snap observations');
 assert.deepEqual(report.boundary.map(row=>row.case),suite.inputs.boundary.map(row=>row.id),'complete ordered boundary observations');
 const requireDraft=(draft,message)=>{assert.ok(draft&&Array.isArray(draft.coords),message);assert.equal(typeof draft.historyCount,'number');assert.equal(typeof draft.futureCount,'number');assert.ok(Object.hasOwn(draft,'activeSnap'));};
 for(const [index,row]of report.snap.entries()){
  const definition=suite.inputs.snap[index];assert.deepEqual(row.input,definition,'exact snap fixture input');assert.equal(row.queries.length,definition.queries.length);assert.ok(row.requests.length);assert.ok(row.transport.some(t=>t.direction==='request'&&t.message.type==='execute'&&t.message.operation==='territorial-snap'),'actual worker snap request evidence');
  for(const request of row.requests){
   assert.equal(request.operation,'territorial-snap');assert.ok(request.message?.payload);assert.ok(['resolved','rejected'].includes(request.status),'terminal actual snap request required');
   if(request.status==='resolved'){
    assert.ok(Array.isArray(request.response?.result?.candidates),'actual resolved snap response');assert.ok(Number.isInteger(request.response.requestId));
    assert.ok(row.transport.some(t=>t.direction==='response'&&t.message.type==='result'&&t.message.ok===true&&t.message.requestId===request.response.requestId&&isDeepStrictEqual(t.message.result,request.response.result)),'matching real worker response envelope');
   }else assert.equal(typeof request.error?.message,'string','explicit rejected worker request');
  }
  if(definition.scenario){assert.equal(row.lifecycle?.scenario,definition.scenario,'required interrupted lifecycle');assert.ok(row.lifecycle.before?.draft);assert.ok(row.lifecycle.after?.draft);assert.ok(row.requests.some(r=>r.status==='rejected'));if(['cancel','project-replacement'].includes(definition.scenario))assert.deepEqual(row.lifecycle.after.draft.coords,[]);}
  for(const [qi,query]of row.queries.entries()){
   assert.ok(Array.isArray(query.input?.coordinate)&&Array.isArray(query.input?.screenPoint));assert.ok(Array.isArray(query.cold.candidates));requireDraft(query.cold.draft,'cold actual draft');assert.equal(typeof query.cold.outcome,'boolean');assert.equal(query.pending.sameArray,true);assert.ok(Number.isInteger(query.pending.requestCount));requireDraft(query.afterSettlementDraft,'post-settlement actual draft');
   if(query.ready.observed===false){assert.ok(query.ready.reason);assert.ok(definition.scenario||definition.queries[qi].expectWorkerError,'unexpected missing snap observation');if(definition.queries[qi].expectWorkerError)assert.ok(row.requests.some(r=>r.status==='rejected'));}
   else {
    for(const name of ['candidates','result','indicator','projectedPoints','synchronizedSourceOrder','draft','outcome'])assert.ok(Object.hasOwn(query.ready,name),'missing snap '+name);
    requireDraft(query.ready.draft,'ready actual draft');assert.equal(typeof query.ready.outcome,'boolean');assert.deepEqual(query.afterSettlementDraft,query.cold.draft,'no retroactive draft mutation');assert.ok(query.candidateRequest?.status==='resolved');
    assert.deepEqual(query.ready.candidates,query.candidateRequest.response.result.candidates,'ready candidates must be actual response');assert.ok(row.requests.some(request=>isDeepStrictEqual(request,query.candidateRequest)),'retained request must belong to actual trace');
    assert.deepEqual(query.ready.draft.activeSnap,query.ready.indicator,'actual editing-domain indicator');
    for(const name of ['undo','redo']){assert.equal(typeof query[name]?.outcome,'boolean','actual draft '+name+' outcome');requireDraft(query[name].draft,'actual draft '+name);}
    assert.deepEqual(query.redo.draft.coords,query.ready.draft.coords,'draft redo restoration');
    for(const candidate of query.ready.candidates)for(const coordinate of [candidate.coordinate,candidate.a,candidate.b].filter(Boolean))assert.ok(query.ready.projectedPoints.some(row=>isDeepStrictEqual(row.coordinate,coordinate)&&(row.screen===null||Array.isArray(row.screen))),'observed candidate projection');
   }
  }
 }
 for(const [index,row]of report.boundary.entries()){
  const definition=suite.inputs.boundary[index];assert.deepEqual(row.input,definition,'exact boundary input');assert.deepEqual(row.extractionEvidence,suite.boundaryExtraction,'actual unmodified callback extraction');
  for(const name of ['before','cold','pending','prepared','drag','preview','cancel','confirm','undo','redo','settled']){const stage=row.stages[name];assert.equal(typeof stage?.observed,'boolean','explicit boundary observation '+name);if(stage.observed){assert.ok(stage.state?.document);assert.ok(stage.state.history);}else assert.ok(stage.reason);}
  for(const name of ['before','cold','pending','settled'])assert.equal(row.stages[name].observed,true,'mandatory actual boundary stage '+name);
  assert.equal(row.entry?.ok,definition.expected.entry,'actual boundary entry outcome');
  if(Object.hasOwn(definition.expected,'prepared')){assert.equal(row.stages.prepared.observed,true);assert.equal(row.stages.prepared.outcome.ok,definition.expected.prepared);}
  if(Object.hasOwn(definition.expected,'gesture'))assert.equal(row.gesture?.ok,definition.expected.gesture);
  if(definition.expected.confirm){
   for(const name of ['drag','preview','cancel','confirm','undo','redo'])assert.equal(row.stages[name].observed,true,'successful fixture missing '+name);
   for(const name of ['preview','cancel','confirm','undo','redo'])assert.equal(row.stages[name].outcome.ok,true,'successful fixture failed '+name);
   for(const name of ['drag','preview','cancel'])assert.deepEqual(row.stages[name].state.document,row.stages.before.state.document,'uncommitted stage mutated canonical model '+name);
   assert.deepEqual(row.stages.undo.state.document,row.stages.before.state.document,'canonical undo');assert.deepEqual(row.stages.redo.state.document,row.stages.confirm.state.document,'canonical redo');
   assert.equal(row.stages.confirm.state.history.undo,row.stages.before.state.history.undo+1,'one atomic history step');
   for(const operation of ['boundary-prepare','boundary-move','territorial-edit','territorial-validation'])assert.ok(row.workerTrace.some(event=>event.direction==='request'&&event.operation===operation),'actual boundary worker operation '+operation);
  }else assert.ok(!row.stages.confirm.observed||row.stages.confirm.outcome.ok===false,'rejected fixture unexpectedly committed');
  if(definition.impactCancel){
   assert.equal(row.stages.impactCancel?.observed,true);assert.equal(row.stages.impactCancel.outcome.ok,false);assert.deepEqual(row.stages.impactCancel.state.document,row.stages.before.state.document);
   assert.deepEqual(row.modalDecisions.map(item=>item.decision),['cancel','confirm']);for(const decision of row.modalDecisions){assert.deepEqual(decision.stateWhenOpened.document,row.stages.before.state.document);assert.deepEqual(decision.stateBeforeDecision.document,row.stages.before.state.document);}
  }
  if(definition.scenario)assert.ok(row.workerTrace.some(event=>event.direction==='held-result'),'actual pending result interception required');
  assert.equal(row.observationLimits.browserPointerProjection,false);assert.equal(row.observationLimits.draftUndoRedo,false);
 }
 return report;
}
export function snapProbeRows(report){
 const rows=[];for(const row of report.snap)for(const [queryIndex,query]of row.queries.entries()){
  if(query.ready.observed===false)continue;const input=query.input,snapshot=query.sourceSnapshot,payload=structuredClone(query.candidateRequest.message.payload);
  if(payload.sourceKey)payload.source=structuredClone(snapshot.sourceGeometry);
  rows.push({case:row.id,queryIndex,features:snapshot.features,genericFeatures:snapshot.genericFeatures,payload,actualRequestPayload:query.candidateRequest.message.payload,sourceRevision:snapshot.sourceRevision,coordinate:input.coordinate,screenPoint:input.screenPoint,view:input.view,pointerType:input.pointerType||'mouse',projectedPoints:query.ready.projectedPoints,synchronizedSourceOrder:query.ready.synchronizedSourceOrder,candidates:query.ready.candidates,result:query.ready.result,indicator:query.ready.indicator,scope:'helper-only',projection:'browser-observed'});
 }return rows;
}
export function compareSnapProbe(expected,actual){
 const key=row=>`${row.case}:${row.queryIndex}`,failures=[];const byKey=new Map();
 for(const row of actual){if(byKey.has(key(row)))failures.push({case:key(row),reason:'duplicate native observation'});else byKey.set(key(row),row);}
 const keys=new Set(expected.map(key));for(const row of actual)if(!keys.has(key(row)))failures.push({case:key(row),reason:'unexpected native observation'});
 for(const row of expected){const observed=byKey.get(key(row));if(!observed){failures.push({case:key(row),reason:'missing native observation'});continue;}
  for(const field of ['candidates','result','indicator'])if(!Object.hasOwn(observed,field)||!isDeepStrictEqual(observed[field],row[field]))failures.push({case:key(row),field,reason:Object.hasOwn(observed,field)?'exact value differs':'missing native field'});
 }
 return {schema:'pando-m974-snap-helper-comparison',version:1,scope:'helper-only',projection:'browser-observed',rawParity:false,expected:expected.length,actual:actual.length,passed:failures.length===0,failures};
}
if(process.argv[2]==='--write-inputs')console.log(JSON.stringify(await writeInputs()));
