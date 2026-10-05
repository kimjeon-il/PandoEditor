// Independent observation-only diagnostic. Never substitutes for browser capture or app/history Undo.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fixtureRoot,readPinnedSources,verifyPinnedSources,sha256} from './sources.mjs';
import {runtimePin} from '../m97/river/suite.mjs';
export {runtimePin};
const inputManifestPin='c9bfe9fd7c0badbd59cdcb97a82aff004dbc17e3648f6414053857a765a9a7d5';
export const sourceOrderObservationLimits=Object.freeze({
 rawParity:false,
 stimulusLayer:'actual-map-edit-client-worker-source-remove-restore',
 webApplicationHistoryUndo:false,
 nativeControllerDeleteUndoSeparate:true,
 original68WorkflowInputsUnchanged:true,
 supplemental12InputsUnchanged:true,
 browserPointerProjection:false,
 candidateAndOwnerOrder:'retained exactly as delivered by the actual Worker',
});

/** Portable runner: use actual production client and Worker, preserving returned order verbatim. */
export async function runSourceOrderCase(loaded,definition){
 const clone=value=>structuredClone(value),original=clone(definition.features),transport=[],stages={};
 let features=original,targetRevision=0,phase='warm';
 const createWorker=()=>{
  const worker=loaded.createWorker(),adapter={onmessage:null,onerror:null,
   postMessage(message){transport.push({sequence:transport.length,phase,direction:'request',message:clone(message)});worker.postMessage(message);},
   terminate:()=>worker.terminate()};
  worker.onmessage=event=>{transport.push({sequence:transport.length,phase,direction:'response',message:clone(event.data)});adapter.onmessage?.(event);};
  worker.onerror=error=>adapter.onerror?.(error);return adapter;
 };
 const client=loaded.api.createMapEditWorkerClient({createWorker,getEntities:()=>features,
  getFeatureById:id=>features.find(feature=>feature.id===id),
  getEditSources:()=>features.map(feature=>({kind:'territorial',feature})),getTargetRevision:()=>targetRevision});
 const query=async()=>{
  const sourceArrayOrder=features.map(feature=>feature.id),response=await client.execute('territorial-snap',
   {payload:clone(definition.query)},{jobKey:'territorial-snap',priority:50});
  const candidates=clone(response.result.candidates);
  return {sourceArrayOrder,targetRevision,response:clone(response),candidates,
   candidateOwnerOrder:candidates.map(candidate=>clone(candidate.ownerIds)),firstCandidateOwnerIds:clone(candidates[0]?.ownerIds??null)};
 };
 try{
  stages.warm=await query();
  // These two mutations and syncPatch calls are deliberately synchronous and consecutive.
  phase='removed';features=original.filter(feature=>!definition.patchIds.includes(feature.id));targetRevision++;
  const removed=client.syncPatch(definition.patchIds);
  stages.removed={sourceArrayOrder:features.map(feature=>feature.id),targetRevision,patchIds:clone(definition.patchIds),syncPatch:removed};
  phase='restored';features=original;targetRevision++;
  const restored=client.syncPatch(definition.patchIds);
  stages.restored={sourceArrayOrder:features.map(feature=>feature.id),targetRevision,patchIds:clone(definition.patchIds),syncPatch:restored};
  phase='afterRestore';stages.afterRestore=await query();
 }finally{client.stop();}
 return {case:definition.id,input:clone(definition),stages,transport,observationLimits:clone(sourceOrderObservationLimits)};
}

export function readSourceOrderInputs(){
 const manifest=JSON.parse(fs.readFileSync(path.join(fixtureRoot,'source-order-v2-manifest.json'),'utf8'));
 assert.equal(sha256(JSON.stringify(manifest)),inputManifestPin,'immutable separate source-order v2 manifest');
 const bytes=fs.readFileSync(path.join(fixtureRoot,manifest.file));assert.equal(bytes.length,manifest.bytes);assert.equal(sha256(bytes),manifest.sha256);
 for(const [file,pin]of Object.entries(manifest.protectedFixtureSha256))assert.equal(sha256(fs.readFileSync(path.join(fixtureRoot,file))),pin,'unchanged existing fixture '+file);
 const input=JSON.parse(bytes);assert.equal(input.schema,'pando-m974-source-order-inputs');assert.equal(input.version,2);
 assert.deepEqual(input.cases.map(row=>row.id),manifest.orderedCaseIds);return {manifest,cases:input.cases};
}
const runtimeSource=()=>`(()=>{const sourceOrderObservationLimits=${JSON.stringify(sourceOrderObservationLimits)};return {runSourceOrderCase:${runSourceOrderCase.toString()}};})()`;
const harnessHashes=()=>Object.fromEntries(['source-order-v2.mjs','source-order-v2-browser-runner.mjs','source-order-v2-node-runner.mjs','sources.mjs','worker-host.mjs','../m97/river/suite.mjs','../m97/river/browser/report-transfer.mjs'].map(name=>[name,sha256(fs.readFileSync(new URL(name,import.meta.url)))]));
const identity=suite=>({schema:suite.schema,version:suite.version,commit:suite.commit,runId:suite.runId,runtimePin:suite.runtimePin,
 sourceManifestSha256:sha256(JSON.stringify(suite.manifest)),sourceSha256:sha256(JSON.stringify(suite.sources)),
 inputManifestSha256:sha256(JSON.stringify(suite.inputManifest)),casesSha256:sha256(JSON.stringify(suite.cases)),runtimeSha256:suite.runtime.sha256,harnessFiles:suite.harnessFiles,
 orderedCaseIds:suite.cases.map(row=>row.id),observationLimits:sourceOrderObservationLimits});
export async function createSourceOrderSuite({commit,runId}={}){
 assert.match(commit||'',/^[a-f0-9]{40}$/,'Exact application commit required');assert.equal(typeof runId,'string','Run identity required');assert.ok(runId.trim(),'Run identity required');
 const {manifest,sources}=readPinnedSources(),{manifest:inputManifest,cases}=readSourceOrderInputs(),source=runtimeSource();
 const suite={schema:'pando-m974-source-order-suite',version:2,commit,runId,runtimePin,manifest,sources,inputManifest,cases,runtime:{source,sha256:sha256(source)},harnessFiles:harnessHashes()};
 suite.identity=identity(suite);return suite;
}
export function verifySourceOrderSuite(suite){
 assert.equal(suite.schema,'pando-m974-source-order-suite');assert.equal(suite.version,2);assert.match(suite.commit,/^[a-f0-9]{40}$/);assert.equal(typeof suite.runId,'string');assert.ok(suite.runId.trim());
 assert.deepEqual(suite.runtimePin,runtimePin);verifyPinnedSources({manifest:suite.manifest,sources:suite.sources});
 const pinned=readSourceOrderInputs();assert.deepEqual(suite.inputManifest,pinned.manifest);assert.deepEqual(suite.cases,pinned.cases,'exact unsorted source-order input');
 assert.deepEqual(suite.runtime,{source:runtimeSource(),sha256:sha256(runtimeSource())});assert.deepEqual(suite.harnessFiles,harnessHashes());assert.deepEqual(suite.identity,identity(suite));return suite;
}
const exactKeys=(object,keys,label)=>{assert.ok(object&&typeof object==='object'&&!Array.isArray(object),label);assert.deepEqual(Object.keys(object).sort(),[...keys].sort(),label+' exact fields');};
const coordinate=value=>{assert.ok(Array.isArray(value)&&value.length===2);for(const number of value)assert.ok(Number.isFinite(number));};
function verifyCandidates(candidates,definition){
 assert.ok(Array.isArray(candidates)&&candidates.length>0,'complete nonempty candidate observation');
 const ids=new Set(definition.features.map(feature=>feature.id));
 for(const candidate of candidates){
  assert.ok(['vertex','edge'].includes(candidate.kind));
  exactKeys(candidate,candidate.kind==='vertex'?['kind','coordinate','ownerIds','nodeKey']:['kind','a','b','ownerIds','segmentKey'],'candidate');
  assert.ok(Array.isArray(candidate.ownerIds)&&candidate.ownerIds.length>0&&candidate.ownerIds.every(id=>ids.has(id)),'retained valid owner order');
  if(candidate.kind==='vertex'){coordinate(candidate.coordinate);assert.equal(typeof candidate.nodeKey,'string');assert.ok(candidate.nodeKey);}
  else{coordinate(candidate.a);coordinate(candidate.b);assert.equal(typeof candidate.segmentKey,'string');assert.ok(candidate.segmentKey);}
 }
}
const sourcePatch=feature=>({key:'territorial:'+feature.id,kind:'territorial',metadata:Object.fromEntries(Object.entries(feature).filter(([key])=>key!=='geometry')),geometry:feature.geometry});
export function verifySourceOrderReport(suite,report){
 verifySourceOrderSuite(suite);
 exactKeys(report,['schema','version','state','identity','sourceHashes','runtime','cases','observationLimits'],'browser report');
 assert.equal(report.schema,'pando-m974-actual-chromium-source-order');assert.equal(report.version,2);assert.equal(report.state,'complete');assert.deepEqual(report.identity,suite.identity);
 assert.equal(report.runtime.playwright,runtimePin.playwright);assert.equal(report.runtime.browserVersion,runtimePin.chromium);assert.equal(report.runtime.chromiumRevision,runtimePin.revision);
 assert.equal(report.runtime.cdp.jsVersion,runtimePin.v8);assert.ok(['Chrome/','HeadlessChrome/'].some(prefix=>report.runtime.cdp.product===prefix+runtimePin.chromium));assert.equal(typeof report.runtime.userAgent,'string');
 assert.deepEqual(report.sourceHashes,Object.fromEntries(suite.manifest.sources.map(row=>[row.path,row.sha256])));assert.deepEqual(report.observationLimits,sourceOrderObservationLimits);
 assert.deepEqual(report.cases.map(row=>row.case),suite.cases.map(row=>row.id));
 for(const [index,row]of report.cases.entries()){
  const definition=suite.cases[index],ids=definition.features.map(feature=>feature.id),removedIds=definition.patchIds,
   remaining=ids.filter(id=>!removedIds.includes(id)),restoredFeatures=definition.features.filter(feature=>removedIds.includes(feature.id));
  exactKeys(row,['case','input','stages','transport','observationLimits'],'case');assert.deepEqual(row.input,definition);assert.deepEqual(row.observationLimits,sourceOrderObservationLimits);
  exactKeys(row.stages,['warm','removed','restored','afterRestore'],'stages');
  for(const [phase,sourceArrayOrder,targetRevision]of [['removed',remaining,1],['restored',ids,2]])assert.deepEqual(row.stages[phase],{sourceArrayOrder,targetRevision,patchIds:removedIds,syncPatch:true});
  assert.ok(Array.isArray(row.transport));assert.equal(row.transport.length,8,'complete exact envelope sequence');
  const shape=[['warm','request','rebase'],['warm','response','ready'],['warm','request','execute'],['warm','response','result'],['removed','request','sync-patch'],['restored','request','sync-patch'],['afterRestore','request','execute'],['afterRestore','response','result']];
  row.transport.forEach((event,i)=>{exactKeys(event,['sequence','phase','direction','message'],'transport event');assert.equal(event.sequence,i);assert.deepEqual([event.phase,event.direction,event.message.type],shape[i],'no intervening query/rebase or omitted envelope');});
  const messages=row.transport.map(event=>event.message);
  assert.deepEqual(messages[0],{type:'rebase',dataRevision:1,geometryRevision:1,targetRevision:0,boundaryIds:ids,editSources:{patches:definition.features.map(sourcePatch),removedKeys:[],sourceRevision:2}});
  assert.deepEqual(messages[1],{type:'ready',dataRevision:1,geometryRevision:1,targetRevision:0});
  assert.deepEqual(messages[4],{type:'sync-patch',dataRevision:2,geometryRevision:2,targetRevision:1,features:[],removedIds,editSources:{patches:[],removedKeys:removedIds.map(id=>'territorial:'+id),sourceRevision:3}});
  assert.deepEqual(messages[5],{type:'sync-patch',dataRevision:3,geometryRevision:3,targetRevision:2,features:restoredFeatures,removedIds:[],editSources:{patches:restoredFeatures.map(sourcePatch),removedKeys:[],sourceRevision:4}});
  for(const [phase,position,requestId,geometryRevision,targetRevision,sourceRevision]of [['warm',2,1,1,0,2],['afterRestore',6,2,3,2,4]]){
   const stage=row.stages[phase],request=messages[position],response=messages[position+1];
   exactKeys(stage,['sourceArrayOrder','targetRevision','response','candidates','candidateOwnerOrder','firstCandidateOwnerIds'],'query stage');
   assert.deepEqual(stage.sourceArrayOrder,ids,'original source array order retained');assert.equal(stage.targetRevision,targetRevision);
   assert.deepEqual(request,{type:'execute',operation:'territorial-snap',requestId,jobKey:'territorial-snap',dataRevision:geometryRevision,geometryRevision,targetRevision,priority:50,payload:definition.query,sourceRevision});
   exactKeys(response,['type','ok','requestId','jobKey','dataRevision','geometryRevision','targetRevision','result'],'full actual Worker result envelope');
   assert.deepEqual(response,{type:'result',ok:true,requestId,jobKey:'territorial-snap',dataRevision:geometryRevision,geometryRevision,targetRevision,result:response.result});
   exactKeys(response.result,['candidates'],'actual result');verifyCandidates(response.result.candidates,definition);
   assert.deepEqual(stage.response,{sourceRevision,requestId,jobKey:'territorial-snap',geometryRevision,targetRevision,result:response.result},'client receipt matches complete incoming result');
   assert.deepEqual(stage.candidates,response.result.candidates,'candidate order exactly matches actual incoming envelope');
   assert.deepEqual(stage.candidateOwnerOrder,response.result.candidates.map(candidate=>candidate.ownerIds),'owner order exactly matches actual incoming envelope');
   assert.deepEqual(stage.firstCandidateOwnerIds,response.result.candidates[0].ownerIds,'first-candidate owner is observed, never recomputed');
  }
 }
 return report;
}
