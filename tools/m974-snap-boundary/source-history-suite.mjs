import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fixtureRoot,sha256,lifecycleRuntimeSource} from './sources.mjs';
import {readSourceHistorySources,verifySourceHistorySources} from './source-history-sources.mjs';
import {runSourceHistoryCase} from './source-history-runtime.mjs';
import {sourceHistorySelectionRuntimeSource} from './source-history-selection.mjs';
import {createSelectionRuntime,selectionCutView,seedSelectionFeatures,square} from '../m97/web-selection.mjs';
import {runtimePin} from '../m97/river/suite.mjs';
export {runtimePin};
export const pairedNativeCases=Object.freeze(['no-query-retains','ready-boundary-worker-observes-deletion','pending-boundary-cancel-rebases-on-next-query','stopped-worker-ignores-root-delete-undo','settled-boundary-error-retains-history','split-setup-only','annex-setup-only','split-components-ready','annex-components-ready','component-timer-only-cancel','component-request-pending-cancel']);
export const sourceHistoryObservationLimits=Object.freeze({rawParity:false,queryContextEquivalent:false,selectedIndicatorOnly:true,fullCanonicalProject:false,stimulusLayer:'actual-production-controller-modules-client-worker',webApplicationHistoryUndo:true,fullDOM:false,pointerPixels:false,gpuRendering:false,heldResultsAreWorkerComplete:true,workerCPUExecutionPhase:false,renderAdoptionPending:false,displayRollbackLibraryPaths:false,projectReplacementStimulus:'canonical-store-install-plus-explicit-client-rebase',supersedeStimulus:'production-rebuild-with-fixture-selection',originalCorporaUnchanged:true,cacheAndPendingNativeStimuliMayDiffer:true});
const inputPin='021f788afea6cfddb786b2a2521e867ae7a6a3f17e05ff750ce12900a6c9d219';
export function readSourceHistoryInputs(){
 const manifest=JSON.parse(fs.readFileSync(path.join(fixtureRoot,'source-history-input-manifest.json'),'utf8'));
 assert.equal(sha256(JSON.stringify(manifest)),inputPin,'immutable source-history input manifest');
 const bytes=fs.readFileSync(path.join(fixtureRoot,manifest.file));assert.equal(bytes.length,manifest.bytes);assert.equal(sha256(bytes),manifest.sha256);
 for(const [name,pin]of Object.entries(manifest.protectedFixtureSha256))assert.equal(sha256(fs.readFileSync(path.join(fixtureRoot,name))),pin,'unchanged original fixture '+name);
 const input=JSON.parse(bytes);assert.equal(input.schema,'pando-m974-source-history-inputs');assert.equal(input.version,1);assert.deepEqual(input.cases.map(row=>row.id),manifest.orderedCaseIds);assert.deepEqual(manifest.pairedNativeCases,pairedNativeCases);assert.deepEqual(input.cases.map(row=>row.nativeCase).filter(Boolean),pairedNativeCases);return {manifest,cases:input.cases};
}
export function sourceHistoryRuntimeSource(){
 const base=`(()=>{const clone=value=>structuredClone(value),noop=()=>{};const square=${square.toString()},selectionCutView=${selectionCutView.toString()},seedSelectionFeatures=${seedSelectionFeatures.toString()},createSelectionRuntime=${createSelectionRuntime.toString()};return {createSelectionRuntime};})()`;
 return `(()=>{const lifecycle=${lifecycleRuntimeSource()},selectionBase=${base},selectionHelpers=${sourceHistorySelectionRuntimeSource()};return {...lifecycle,runSourceHistoryCase:${runSourceHistoryCase.toString()},selectionRuntime:{...selectionBase,...selectionHelpers}};})()`;
}
const harnessHashes=()=>Object.fromEntries(['source-history-suite.mjs','source-history-runtime.mjs','source-history-selection.mjs','source-history-sources.mjs','source-history-browser-runner.mjs','source-history-node-runner.mjs','sources.mjs','worker-host.mjs','../m97/web-lifecycle.mjs','../m97/web-selection.mjs','../m97/river/suite.mjs','../m97/river/browser/report-transfer.mjs'].map(name=>[name,sha256(fs.readFileSync(new URL(name,import.meta.url)))]));
const identity=s=>({schema:s.schema,version:s.version,commit:s.commit,runId:s.runId,runtimePin:s.runtimePin,sourceManifestSha256:sha256(JSON.stringify(s.manifest)),sourceSha256:sha256(JSON.stringify(s.sources)),inputManifestSha256:sha256(JSON.stringify(s.inputManifest)),casesSha256:sha256(JSON.stringify(s.cases)),runtimeSha256:s.runtime.sha256,harnessFiles:s.harnessFiles,orderedCaseIds:s.cases.map(row=>row.id),pairedNativeCases,observationLimits:sourceHistoryObservationLimits});
export async function createSourceHistorySuite({commit,runId}={}){
 assert.match(commit||'',/^[a-f0-9]{40}$/,'Exact application commit required');assert.equal(typeof runId,'string');assert.ok(runId.trim());
 const {manifest,sources}=readSourceHistorySources(),input=readSourceHistoryInputs(),source=sourceHistoryRuntimeSource();
 const s={schema:'pando-m974-source-history-suite',version:1,commit,runId,runtimePin,manifest,sources,inputManifest:input.manifest,cases:input.cases,runtime:{source,sha256:sha256(source)},harnessFiles:harnessHashes()};s.identity=identity(s);return s;
}
export function verifySourceHistorySuite(s){
 assert.equal(s.schema,'pando-m974-source-history-suite');assert.equal(s.version,1);assert.match(s.commit||'',/^[a-f0-9]{40}$/);assert.equal(typeof s.runId,'string');assert.ok(s.runId.trim());assert.deepEqual(s.runtimePin,runtimePin);
 verifySourceHistorySources({manifest:s.manifest,sources:s.sources});const inputs=readSourceHistoryInputs();assert.deepEqual(s.inputManifest,inputs.manifest);assert.deepEqual(s.cases,inputs.cases);
 const source=sourceHistoryRuntimeSource();assert.deepEqual(s.runtime,{source,sha256:sha256(source)});assert.deepEqual(s.harnessFiles,harnessHashes());assert.deepEqual(s.identity,identity(s));return s;
}
const exact=(object,keys,label)=>{assert.ok(object&&typeof object==='object'&&!Array.isArray(object),label);assert.deepEqual(Object.keys(object).sort(),[...keys].sort(),label+' exact fields');};
const same=(a,b,label)=>assert.deepEqual(a,b,label);
export function verifySourceHistoryCase(definition,row){
 exact(row,['case','input','stages','transport','requests','errors','entrypoint','observationLimits'],'case');assert.equal(row.case,definition.id);same(row.input,definition,'exact input');assert.equal(row.observationLimits.rawParity,false);assert.equal(row.observationLimits.queryContextEquivalent,false);assert.equal(row.observationLimits.selectedIndicatorOnly,true);assert.equal(row.observationLimits.fullCanonicalProject,false);assert.equal(row.observationLimits.heldResultIsWorkerComplete,true);assert.equal(row.observationLimits.workerExecutionPhase,false);assert.equal(row.errors.length,0,JSON.stringify(row.errors));
 for(const name of ['warm','removed','restored','final'])assert.ok(row.stages[name]?.observed,'required stage '+name);
 for(const [name,stage]of Object.entries(row.stages)){
  assert.equal(stage.observed,true);assert.equal(typeof stage.canonical,'string');const canonical=JSON.parse(stage.canonical);assert.equal(JSON.stringify(canonical),stage.canonical,'exact canonical bytes '+name);
  same(stage.sourceRows,[...canonical.entities.map(f=>'territorial:'+f.id),...canonical.genericFeatures.map(f=>'generic:'+f.id),...canonical.hydroEdits.map(f=>'hydro:'+f.id)],'actual ordered canonical source rows '+name);
  assert.ok(Number.isInteger(stage.stateRevision)&&stage.stateRevision>=0);assert.equal(typeof stage.tool,'string');assert.equal(typeof stage.worker.ready,'boolean');
  if(Object.hasOwn(stage,'canonicalEqual'))assert.equal(stage.canonicalEqual,stage.canonical===row.stages.warm.canonical,'canonical equality evidence');
 }
 assert.equal(row.stages.restored.canonicalEqual,true,'Undo restores canonical bytes');
 const baseline=JSON.parse(row.stages.warm.canonical),removed=structuredClone(baseline);removed.genericFeatures=removed.genericFeatures.filter(f=>f.id!=='ga');
 assert.equal(row.stages.removed.canonical,JSON.stringify(removed),'public generic deletion changes only ga membership');
 assert.equal(row.stages.restored.canonical,row.stages.warm.canonical,'public Undo restores exact canonical bytes');
 if(definition.territories){
  same(baseline.entities.map(f=>f.id),definition.territories.map(r=>r.id),'fixture entity identity/order');
  definition.territories.forEach((r,i)=>{const [x0,y0,x1,y1]=r.bounds,coordinates=[[[x0,y0],[x0,y1],[x1,y1],[x1,y0],[x0,y0]]];same(baseline.entities[i].geometry,{type:r.geometryType,coordinates:r.geometryType==='MultiPolygon'?[coordinates]:coordinates},'input fixture geometry/type '+r.id);});
 }
 if(definition.genericBounds){const [x0,y0,x1,y1]=definition.genericBounds;for(const f of baseline.genericFeatures)same(f.geometry,{type:'Polygon',coordinates:[[[x0,y0],[x0,y1],[x1,y1],[x1,y0],[x0,y0]]]},'generic input fixture geometry');}
 if(definition.scenario==='stopped-root-delete-undo'){const rootRemoved=structuredClone(removed);rootRemoved.entities=rootRemoved.entities.filter(f=>f.id!==definition.rootDeletionId);assert.equal(row.stages.rootRemoved.canonical,JSON.stringify(rootRemoved));assert.equal(row.stages.rootRestored.canonical,JSON.stringify(removed));}
 const baselineStages=new Set(['warm','restored','final','reordered','cached','replacement','selectionCachePrepared']);
 const removedStages=new Set(['removed','rootRestored','observeRemoval','superseded','selectionCancelled','controlledStop','resultSettled']);
 const operationBaseline=['preview-stop-ready-snap-hit','replacement-after-stop','settled-success-observes-late-deletion','cancelled-ticket-skips-post-sync'].includes(definition.scenario);
 for(const [name,stage]of Object.entries(row.stages)){
  if(name==='rootRemoved'){assert.equal(definition.scenario,'stopped-root-delete-undo');continue;}
  assert.ok(baselineStages.has(name)||removedStages.has(name)||['operation','cancelled'].includes(name),'known canonical transition stage '+name);
  const expected=baselineStages.has(name)||(['operation','cancelled'].includes(name)&&operationBaseline)?baseline:removed;
  assert.equal(stage.canonical,JSON.stringify(expected),'canonical stage follows exact deletion/Undo progression '+name);
 }
 const canonicalPhase=phase=>{
  const stage=row.stages[phase]||row.stages[{lateRequest:'warm',releaseResult:'removed',supersede:'removed',cleanup:'final',setup:'warm'}[phase]];
  assert.ok(stage,'known canonical phase '+phase);return JSON.parse(stage.canonical);
 };
 const canonicalSources=canonical=>new Map([...canonical.entities.map(feature=>['territorial:'+feature.id,{kind:'territorial',feature}]),...canonical.genericFeatures.map(feature=>['generic:'+feature.id,{kind:'generic',feature}]),...canonical.hydroEdits.map(feature=>['hydro:'+feature.id,{kind:'hydro',feature}])]);
 const validatePatch=(patch,canonical)=>{const expected=canonicalSources(canonical);assert.ok(Number.isInteger(patch.sourceRevision)&&patch.sourceRevision>0);assert.equal(new Set(patch.patches.map(p=>p.key)).size,patch.patches.length);assert.equal(new Set(patch.removedKeys).size,patch.removedKeys.length);
  for(const p of patch.patches){const source=expected.get(p.key);assert.ok(source,'patch source exists canonically '+p.key);assert.equal(p.kind,source.kind);const {geometry,...metadata}=source.feature;same(p.metadata,metadata,'patch exact canonical metadata '+p.key);if(Object.hasOwn(p,'geometry'))same(p.geometry,geometry,'patch exact canonical geometry '+p.key);exact(p,Object.hasOwn(p,'geometry')?['key','kind','metadata','geometry']:['key','kind','metadata'],'source patch');}
  for(const key of patch.removedKeys)assert.ok(!expected.has(key),'removal is absent canonically '+key);return expected;
 };

 const directions=new Set(['request','response','delivered','held','released','terminate','error','client-execute','client-resolved','client-rejected','client-stop','client-cancel','client-rebase','client-sync-patch']);
 const sent=new Map(),incoming=new Map(),delivered=new Map(),rebases=new Map(),orders=new Map(),snapOrders=new Map(),terminations=new Set(),clientCalls=new Map(),readyIncoming=new Map(),readyDelivered=new Map(),sourceRevisions=new Map(),dataRevisions=new Map();
 for(const [index,event]of row.transport.entries()){
  assert.equal(event.sequence,index,'complete sequential trace');assert.ok(directions.has(event.direction),'known trace event');assert.equal(typeof event.phase,'string');
  if(event.direction==='client-execute'){assert.ok(!clientCalls.has(event.invocation));clientCalls.set(event.invocation,event.operation);}
  if(['client-resolved','client-rejected'].includes(event.direction))assert.equal(clientCalls.get(event.invocation),event.operation,'known client settlement');
  if(event.direction==='terminate'){assert.ok(Number.isInteger(event.worker));terminations.add(event.worker);}
  if(!['request','response','delivered'].includes(event.direction))continue;
  const m=event.message;assert.ok(m&&typeof m==='object');assert.ok(Number.isInteger(event.worker)&&event.worker>0);const key=event.worker+':'+m.requestId;
  if(event.direction==='request'){
   assert.ok(!terminations.has(event.worker),'no outgoing transport after Worker termination');assert.ok(['rebase','execute','edit-sync','sync-patch','boundary-sync','cancel','discard','boundary-invalidate','commit'].includes(m.type),'known outgoing protocol type');assert.notEqual(m.type,'commit','read-only corpus must never Apply or commit');
   if(m.type==='rebase'){readyIncoming.delete(event.worker);readyDelivered.delete(event.worker);sourceRevisions.set(event.worker,m.editSources.sourceRevision);dataRevisions.set(event.worker,m.dataRevision);assert.equal(m.geometryRevision,m.dataRevision);const expected=validatePatch(m.editSources,canonicalPhase(event.phase));same(m.editSources.patches.map(p=>p.key),[...expected.keys()],'complete canonical rebase order');for(const p of m.editSources.patches)assert.ok(Object.hasOwn(p,'geometry'),'rebase carries every geometry');const map=new Map();for(const patch of m.editSources.patches)map.set(patch.key,patch);orders.set(event.worker,map);rebases.set(event.worker,m);same(m.editSources.removedKeys,[]);if(row.stages[event.phase])same([...map.keys()],row.stages[event.phase].sourceRows,'rebase source rows preserve current order');}
   if(m.type==='edit-sync'||m.type==='sync-patch'){
    const patch=m.type==='edit-sync'?m:m.editSources;sourceRevisions.set(event.worker,patch.sourceRevision);if(m.type==='sync-patch'){dataRevisions.set(event.worker,m.dataRevision);assert.equal(m.geometryRevision,m.dataRevision);}const expected=validatePatch(patch,canonicalPhase(event.phase));assert.ok(patch&&Array.isArray(patch.patches)&&Array.isArray(patch.removedKeys));const map=orders.get(event.worker);assert.ok(map,'source synchronization follows rebase');for(const id of patch.removedKeys)map.delete(id);for(const value of patch.patches){const prior=map.get(value.key);if(!prior)assert.ok(Object.hasOwn(value,'geometry'),'newly inserted source must carry geometry');map.set(value.key,{...value,geometry:Object.hasOwn(value,'geometry')?value.geometry:prior.geometry});}same([...map.keys()].sort(),[...expected.keys()].sort(),'synchronization covers complete canonical membership');if(m.type==='sync-patch'){const canonical=canonicalPhase(event.phase);for(const f of m.features)same(f,canonical.entities.find(v=>v.id===f.id),'root sync exact canonical feature');for(const id of m.removedIds)assert.ok(!canonical.entities.some(f=>f.id===id));}
   }
   if(m.type==='boundary-sync'){
    exact(m,['type','features','removedIds'],'boundary-sync envelope');const map=orders.get(event.worker);assert.ok(map);const canonical=canonicalPhase(event.phase);
    for(const id of m.removedIds){assert.ok(!canonical.entities.some(f=>f.id===id),'boundary removal absent canonically');assert.ok(map.has('territorial:'+id),'boundary removal previously known');map.delete('territorial:'+id);}
    for(const f of m.features){const current=canonical.entities.find(v=>v.id===f.id);assert.ok(current,'boundary feature exists canonically');const root=current.properties.entityKind==='general'&&!current.properties.parentId;assert.equal(current.properties.entityKind,'general','actual boundary provider excludes regional entities');const expected=root?{...current,boundaryLocked:current.properties.locked===true}:current;same(f,expected,'boundary-sync exact canonical geometry and metadata');const {geometry,...metadata}=f;map.set('territorial:'+f.id,{key:'territorial:'+f.id,kind:'territorial',metadata,geometry});}
   }
   if(m.type==='execute'){
    const fixed=['type','operation','requestId','jobKey','dataRevision','geometryRevision','targetRevision','priority','sourceRevision'];for(const field of fixed)assert.ok(Object.hasOwn(m,field),'required execute envelope '+field);const payload=Object.fromEntries(Object.entries(m).filter(([k])=>!fixed.includes(k)));assert.ok(row.requests.some(r=>r.operation===m.operation&&JSON.stringify(r.message)===JSON.stringify(payload)),'transport payload matches actual client invocation');assert.ok(rebases.has(event.worker));assert.ok(readyDelivered.has(event.worker),'execute requires actual delivered Worker ready');assert.equal(m.sourceRevision,sourceRevisions.get(event.worker),'execute source revision matches transported state');assert.equal(m.dataRevision,dataRevisions.get(event.worker),'execute data revision matches transported state');assert.equal(m.geometryRevision,m.dataRevision);assert.ok(!sent.has(key),'unique worker request');sent.set(key,m);if(m.operation==='territorial-snap')snapOrders.set(key,[...orders.get(event.worker).keys()].filter(k=>k.startsWith('generic:')).map(k=>k.slice(8)));
   }
  } else if(event.direction==='response'){
   if(m.type==='ready'){const rebase=rebases.get(event.worker);assert.ok(rebase,'ready follows actual rebase');same(m,{type:'ready',dataRevision:rebase.dataRevision,geometryRevision:rebase.geometryRevision,targetRevision:rebase.targetRevision});readyIncoming.set(event.worker,m);}
   else {assert.equal(m.type,'result');const request=sent.get(key);assert.ok(request,'result must match an actual request');assert.ok(!incoming.has(key),'one incoming result per request');assert.equal(m.jobKey,request.jobKey);for(const field of ['dataRevision','geometryRevision','targetRevision'])assert.equal(m[field],request[field]);assert.equal(typeof m.ok,'boolean');incoming.set(key,m);}
  } else {
   if(m.type==='result'){same(m,incoming.get(key),'delivered result is unmodified actual incoming envelope');assert.ok(!delivered.has(key),'one delivery per result');delivered.set(key,m);}
   else {assert.equal(m.type,'ready');same(m,readyIncoming.get(event.worker),'ready delivery matches actual incoming ready');readyDelivered.set(event.worker,m);}
  }
 }
 assert.ok(sent.size>0,'actual Worker requests');
 assert.equal(clientCalls.size,row.requests.length,'all client calls recorded');for(const [requestIndex,request]of row.requests.entries()){assert.equal(request.invocation,requestIndex+1);assert.equal(clientCalls.get(request.invocation),request.operation,'client invocation evidence');const settlement=row.transport.filter(e=>e.invocation===request.invocation&&e.direction===(request.status==='resolved'?'client-resolved':'client-rejected'));assert.equal(settlement.length,1,'mandatory matching client settlement');if(request.status==='resolved')assert.equal(settlement[0].requestId,request.response.requestId);assert.ok(['resolved','rejected'].includes(request.status));if(request.status==='resolved'){
  const matches=[...sent.entries()].filter(([,m])=>m.requestId===request.response.requestId&&m.operation===request.operation);assert.equal(matches.length,1,'unique client result transport');const [key,m]=matches[0],reply=delivered.get(key);assert.equal(reply?.ok,true);same(reply.result,request.response.result,'client received exact Worker result');assert.equal(request.response.sourceRevision,m.sourceRevision);
 }}
 for(const [name,stage]of Object.entries(row.stages))if(Object.hasOwn(stage,'winner')){
  assert.ok(Array.isArray(stage.candidates)&&stage.candidates.length);assert.ok(stage.result&&stage.indicator,'actual snap resolver and indicator evidence');assert.equal(stage.pointerType,'mouse');assert.ok(Array.isArray(stage.screenPoint)&&stage.screenPoint.every(Number.isFinite));assert.ok(Number.isFinite(stage.result.distancePx)&&stage.result.distancePx>=0&&stage.result.distancePx<=10);assert.equal(stage.winner,stage.indicator.ownerIds[0]);same(stage.indicator,{kind:stage.result.kind,coordinate:[...stage.result.coordinate],segmentEndpoints:stage.result.segmentEndpoints?stage.result.segmentEndpoints.map(p=>[...p]):null,ownerIds:[...stage.result.ownerIds].map(String),nodeKey:stage.result.nodeKey||null,segmentKey:stage.result.segmentKey||null},'indicator preserves actual selected result');const selected=stage.candidates.find(c=>c.kind===stage.result.kind&&JSON.stringify(c.ownerIds)===JSON.stringify(stage.result.ownerIds)&&(c.nodeKey||null)===(stage.result.nodeKey||null)&&(c.segmentKey||null)===(stage.result.segmentKey||null));assert.ok(selected,'selected snap originates in actual candidates');
  if(['vertex','intersection'].includes(selected.kind)){same(stage.result.coordinate,selected.coordinate,'selected vertex coordinate comes from matching actual candidate');assert.equal(stage.indicator.segmentEndpoints,null);}
  else {same(stage.result.segmentEndpoints,[selected.a,selected.b],'selected segment endpoints come from matching actual candidate');assert.ok(Number.isFinite(stage.result.segmentT)&&stage.result.segmentT>=0&&stage.result.segmentT<=1);for(let axis=0;axis<2;axis++)assert.ok(Math.abs(stage.result.coordinate[axis]-(selected.a[axis]+(selected.b[axis]-selected.a[axis])*stage.result.segmentT))<=1e-10,'selected segment coordinate is on its actual endpoints');}
  const canonical=JSON.parse(stage.canonical),generic=new Map(canonical.genericFeatures.map(f=>[f.id,f]));
  for(const candidate of stage.candidates){assert.ok(['vertex','edge'].includes(candidate.kind),'bounded fixture snap candidate kinds');assert.equal(candidate.ownerIds.length,1);const owner=generic.get(candidate.ownerIds[0]);assert.ok(owner,'candidate owner belongs to current generic fixture');const ring=owner.geometry.coordinates[0];if(candidate.kind==='vertex'){assert.ok(ring.some(p=>JSON.stringify(p)===JSON.stringify(candidate.coordinate)),'candidate vertex belongs to exact source geometry');assert.equal(candidate.nodeKey,candidate.coordinate.map(v=>Number(v).toFixed(7)).join(','));}else {const index=ring.slice(0,-1).findIndex((p,i)=>JSON.stringify(p)===JSON.stringify(candidate.a)&&JSON.stringify(ring[i+1])===JSON.stringify(candidate.b));assert.ok(index>=0,'candidate edge belongs to exact source geometry');assert.equal(candidate.segmentKey,`${owner.id}:0:0:${index}`);}}
  for(let axis=0;axis<2;axis++)assert.ok(Math.abs(stage.result.coordinate[axis]-stage.coordinate[axis])<=1e-10,'bounded fixture selected snap lies at requested boundary coordinate');assert.ok(Number.isInteger(stage.requestCount)&&stage.requestCount>=0);
  const queries=row.requests.filter(r=>r.phase===name&&r.operation==='territorial-snap'&&r.status==='resolved'),query=queries.at(-1);if(['warm','final'].includes(name)){assert.equal(queries.length,1,'mandatory actual '+name+' snap');assert.equal(stage.requestCount,1);same(stage.coordinate,name==='final'&&definition.finalQueryCoordinate?definition.finalQueryCoordinate:definition.queryCoordinate||[-20,-20],'exact query coordinate');}assert.equal(stage.requestCount,row.requests.filter(r=>r.phase===name&&r.operation==='territorial-snap').length,'query request counter matches invocations');
  if(query){same(query.message.payload.coordinate,stage.coordinate,'actual query coordinate equals observed stage');same(stage.candidates,query.response.result.candidates,'verbatim candidate order');const key=[...sent].find(([,m])=>m.operation==='territorial-snap'&&m.requestId===query.response.requestId)[0];assert.equal(stage.winner,snapOrders.get(key)[0],'candidate winner follows transported generic insertion history');}
 }
 assert.equal(row.stages.warm.winner,'ga');
 const retain=new Set(['generic-unsynced','boundary-held-result-stop','preview-stop-ready-snap-hit','stopped-root-delete-undo','replacement-after-stop']);
 if(retain.has(definition.scenario))assert.equal(row.stages.final.winner,'ga');
 if(['boundary-ready-cancel','boundary-error-cancel','superseded-result'].includes(definition.scenario))assert.equal(row.stages.final.winner,'gb');
 if(definition.scenario==='boundary-ready-cancel'||definition.scenario==='boundary-error-cancel')assert.equal(row.transport.filter(e=>e.direction==='terminate'&&e.phase!=='cleanup').length,0);
 if(definition.scenario==='boundary-held-result-stop'){assert.equal(row.stages.operation.preparation.workerPending,true);assert.equal(row.stages.cancelled.worker.workerActive,false);assert.ok(row.transport.some(e=>e.direction==='held'&&e.operation==='boundary-prepare'));assert.ok(row.transport.some(e=>e.direction==='terminate'&&e.phase!=='cleanup'));}
 if(definition.scenario==='preview-stop-ready-snap-hit'){assert.equal(row.stages.reordered.winner,'gb');assert.equal(row.stages.cached.winner,'gb');assert.equal(row.stages.cached.requestCount,0);assert.equal(row.stages.cached.worker.workerActive,false);assert.equal(row.stages.final.requestCount,1);same(row.stages.cached.candidates,row.stages.reordered.candidates,'READY cache survives stop unchanged');}
 if(definition.scenario==='stopped-root-delete-undo')same(row.transport.filter(e=>e.direction==='client-sync-patch').map(e=>e.result),[false,false],'stopped root notifications rejected');
 if(definition.scenario==='boundary-error-cancel'){assert.equal(row.stages.operation.preparation.status,'error');assert.equal(row.stages.operation.preparation.workerPending,false);}
 if(definition.scenario==='boundary-ready-cancel'){assert.equal(row.stages.operation.preparation.status,'ready');assert.equal(row.stages.operation.preparation.workerPending,false);}
 if(['settled-success-observes-late-deletion','cancelled-ticket-skips-post-sync'].includes(definition.scenario)){
  const accepted=definition.scenario==='settled-success-observes-late-deletion';assert.equal(row.stages.operation.heldOperation,'territorial-snap');assert.equal(row.stages.operation.heldResultIsWorkerComplete,true);assert.equal(row.stages.resultSettled.clientStatus,'rejected');
  const late=row.requests.find(r=>r.phase==='lateRequest');assert.equal(late?.operation,'territorial-snap');assert.equal(late.status,'rejected');assert.equal(late.error.cancelled,true);
  const observed=row.transport.filter(e=>e.direction==='request'&&e.phase==='releaseResult'&&e.message.type==='edit-sync');
  if(accepted){assert.equal(observed.length,1);same(observed[0].message.removedKeys,['generic:ga']);assert.equal(row.stages.final.winner,'gb');assert.ok(row.transport.some(e=>e.direction==='delivered'&&e.phase==='releaseResult'&&e.message.type==='result'&&e.message.ok===true));}
  else {assert.equal(observed.length,0);assert.equal(row.stages.final.winner,'ga');assert.ok(row.transport.some(e=>e.direction==='client-cancel'));}
  assert.equal(row.transport.filter(e=>e.direction==='terminate'&&e.phase!=='cleanup').length,0);
 }
 if(definition.scenario==='selection'){
  const noRequest=['split-setup-only','annex-setup-only','component-timer-only-cancel'],ready=['split-components-ready','annex-components-ready'],pending=definition.id==='component-request-pending-cancel',cache=definition.id==='annex-components-cache-hit';
  const selection=row.stages.operation?.selection;assert.ok(selection,'actual selection phase');assert.ok(row.stages.selectionCancelled?.observed);assert.equal(row.stages.selectionCancelled.selection,null);
  if(definition.id==='pending-selection-deselect-clear'){
   assert.equal(definition.nativeCase,null);const operation=row.stages.operation;assert.equal(operation.nativeCase,null);assert.equal(operation.ownerMicrotaskDrains,2);assert.equal(operation.heldOperation,'territory-selection');assert.equal(operation.heldResultIsWorkerComplete,true);
   assert.equal(operation.beforeDeselect.workerRequests,1);assert.equal(operation.beforeDeselect.selectedComponentKeys.length,1);for(const value of [operation.afterDeselect,operation.afterMicrotasks]){assert.equal(value.workerRequests,1);same(value.selectedComponentKeys,[]);assert.equal(value.computationPending,false);}same(operation.selection,operation.afterMicrotasks);
   assert.equal(row.stages.selectionCancelled.workerStopped,true);assert.equal(row.stages.final.winner,'ga');same(row.requests.map(r=>r.operation),['territorial-snap','territory-components','territory-selection','territorial-snap']);const selected=row.requests.find(r=>r.operation==='territory-selection');assert.equal(selected.status,'rejected');assert.equal(selected.error.cancelled,true);assert.ok(row.transport.some(e=>e.direction==='held'&&e.operation==='territory-selection'));assert.equal(row.transport.filter(e=>e.direction==='terminate'&&e.phase!=='cleanup').length,1);assert.equal(row.transport.filter(e=>e.direction==='request'&&e.message.type==='rebase').length,2);
  }
  if(definition.controlledStop){
   assert.equal(definition.nativeCase,null);const stop=row.stages.controlledStop?.controlledStop;assert.ok(stop);assert.equal(stop.injected,true);assert.equal(stop.workerBefore.ready,true);assert.equal(stop.workerBefore.workerActive,true);assert.equal(stop.workerAfter.workerActive,false);same(stop.sourceRows,row.stages.controlledStop.sourceRows);same(row.stages.operation.controlledStop,stop);assert.equal(row.stages.operation.previewReady,true);assert.equal(row.stages.selectionCancelled.workerStopped,false);assert.equal(row.stages.final.winner,'gb');
   const restart=row.stages.operation.restartEvidence,event=row.transport[restart.transportIndex];assert.equal(event?.direction,'request');assert.equal(event.message.type,'rebase');assert.ok(restart.transportIndex>=stop.transportEnd);assert.equal(restart.worker,event.worker);same(restart.message,event.message);const next=row.transport.slice(stop.transportEnd).find(e=>e.direction==='request'&&e.message.type==='execute');assert.equal(next?.message.operation,stop.operation);assert.equal(row.transport.filter(e=>e.direction==='terminate'&&e.phase!=='cleanup').length,1);
  }
  if(noRequest.includes(definition.id)){same(row.requests.map(r=>r.operation),['territorial-snap','territorial-snap'],'setup or pre-execute cancel must not fabricate Worker work');assert.equal(selection.workerRequests,0);assert.equal(row.stages.final.winner,'ga');assert.equal(row.stages.selectionCancelled.workerStopped,false);}
  if(definition.id==='component-timer-only-cancel'){assert.equal(selection.activePhase,'preparing');assert.match(row.stages.operation.cancellationStage,/no browser timer or client execute/);}
  if(ready.includes(definition.id)){same(row.requests.map(r=>r.operation),['territorial-snap','territory-components','territorial-snap']);assert.equal(selection.activePhase,'components');assert.equal(selection.workerRequests,0);assert.equal(row.stages.operation.previewReady,false);assert.equal(row.stages.selectionCancelled.workerStopped,false);assert.equal(row.stages.final.winner,'gb');}
  if(pending){assert.ok(selection.workerRequests>0);assert.equal(row.stages.operation.heldOperation,'territory-components');assert.equal(row.stages.operation.heldResultIsWorkerComplete,true);assert.equal(row.stages.selectionCancelled.workerStopped,true);assert.ok(row.transport.some(e=>e.direction==='terminate'&&e.phase!=='cleanup'));assert.equal(row.stages.final.winner,'ga');}
  if(cache){assert.equal(row.stages.operation.cacheHit,true);assert.equal(selection.componentKey,row.stages.selectionCachePrepared.selection.componentKey);assert.equal(row.stages.final.winner,'ga');assert.equal(row.stages.selectionCancelled.workerStopped,false);assert.ok(!row.requests.some(r=>r.phase==='removed'),'cache stimulus emits no post-delete client execute');}
  if(['root-line-cut-preview','child-line-cut-preview','entity-polygon-preview'].includes(definition.id)){
   assert.equal(row.stages.operation.previewReady,true);assert.equal(row.stages.final.winner,'gb');assert.equal(row.stages.selectionCancelled.workerStopped,false);
   const required=['territory-components','territory-selection',...(definition.id==='entity-polygon-preview'?['territorial-drawn','territorial-edit']:['territorial-cut',definition.id==='root-line-cut-preview'?'new-country':'territorial-edit'])];
   for(const operation of required)assert.ok(row.requests.some(r=>r.operation===operation&&r.status==='resolved'),'actual requested stage '+operation);
   if(definition.id!=='entity-polygon-preview'){const ex=row.stages.operation.cutExtraction;assert.ok(ex&&ex.source);assert.equal(sha256(ex.source),ex.sha256,'exact extracted cut source');assert.equal(ex.sourcePath,'assets/js/modules/app-domain-assembly.js');}
  }
 }

 return row;
}
export function verifySourceHistoryReport(suite,report){
 verifySourceHistorySuite(suite);exact(report,['schema','version','state','identity','sourceHashes','runtime','cases','observationLimits'],'browser report');assert.equal(report.schema,'pando-m974-actual-chromium-source-history');assert.equal(report.version,1);assert.equal(report.state,'complete');same(report.identity,suite.identity);same(report.observationLimits,sourceHistoryObservationLimits);
 same(report.sourceHashes,Object.fromEntries(suite.manifest.sources.map(row=>[row.path,row.sha256])));assert.equal(report.runtime.playwright,runtimePin.playwright);assert.equal(report.runtime.browserVersion,runtimePin.chromium);assert.equal(report.runtime.chromiumRevision,runtimePin.revision);assert.equal(report.runtime.cdp.jsVersion,runtimePin.v8);assert.ok(['Chrome/','HeadlessChrome/'].some(p=>report.runtime.cdp.product===p+runtimePin.chromium));assert.equal(typeof report.runtime.userAgent,'string');
 same(report.cases.map(r=>r.case),suite.cases.map(r=>r.id));for(const [index,row]of report.cases.entries())verifySourceHistoryCase(suite.cases[index],row);return report;
}
