import assert from 'node:assert/strict';
import {extractBoundaryCallbacks} from '../m974-snap-boundary/boundary-runtime.mjs';

export function boundarySessionReplacementCases(){
 const features=[{id:'A',ring:[[0,0],[1,0],[1,1],[1,2],[0,2],[0,0]]},{id:'B',ring:[[1,0],[2,0],[2,1],[1,1],[1,0]]},{id:'C',ring:[[1,1],[2,1],[2,2],[1,2],[1,1]]}];
 return ['preparation','preview'].map(phase=>({id:`${phase}-completed-reenter`,phase,features:structuredClone(features),selectedIds:['A','B','C'],seedId:'A',replacementIds:['A','B'],replacementPrimary:'A',move:{nodeKey:'1,1',coordinate:[1,1.25]}}));
}
export function sessionReplacementStageNames(definition){return ['before','entered',...(definition.phase==='preview'?['prepared','drag']:[]),'oldCompleted','afterCancel','replacementSelected','replacementEntered','settled'];}
export const sessionReplacementLimits=Object.freeze({rawParity:false,fullDOM:false,pointerPixels:false,gpuRendering:false,fullCanonicalProject:false,workerCPUExecutionPhase:false,nativeBoundaryMoveWorkerInterval:false,heldResultsAreWorkerComplete:true,projectGenerationChange:false,ownerLockCoverage:false});

// This observes unmodified production modules and transports. Only delivery of
// one identified OLD completed result is delayed; new requests are never gated.
export async function runBoundarySessionReplacementCase(loaded,definition){
 const {api}=loaded,clone=x=>structuredClone(x),noop=()=>{},require=(c,m)=>{if(!c)throw Error(m);};
 const runtime=loaded.runtime.createRuntime(api),{state,entityStore,entityRepository,ports,domains,geometryPreview}=runtime;
 const features=definition.features.map(row=>api.createTerritorialFeature({id:row.id,name:row.id,entityKind:'general',coverageMode:'explicit',geometry:{type:'Polygon',coordinates:[clone(row.ring)]}}));
 entityStore.restoreProject(api.createStaticTerritorialSnapshot(features));
 Object.assign(state,{distributionLayers:[],distributionEntries:[],labels:[],labelSettings:{},itemVisibility:{},layerPresentation:{schemaVersion:4,styles:{},objectStyles:{},objectOrder:[]}});state.historyDirtyEntityIds.clear();
 const errors=[],transport=[],stages={},visualEvents=[],requests=[],invocations=[];let phase='setup',workerId=0,invocation=0,held=null,heldRequest=null;
 const event=(kind,details={})=>transport.push({sequence:transport.length,phase,kind,...clone(details)});
 const selection=api.createSelectionDomain({projectDomain:domains.projectDomain,onSelectionChanged:snapshot=>{state.selected=snapshot.selection.items.find(ref=>ref.key===snapshot.selection.primaryKey)||null;}});
 domains.selectionDomain=selection;domains.selectionUiController=api.createSelectionUiController({selectionDomain:selection,resolveRef:api.normalizeObjectRef});
 domains.editingDomain=api.createEditingDomain({projectDomain:domains.projectDomain,selectionDomain:selection,toolController:{getGeometryPreviewSession:()=>state.geometryPreview.session,discardGeometryPreview:geometryPreview.discardActiveGeometryPreview},onEditingStateChanged:snapshot=>{state.tool=snapshot.activeTool;}});
 const ref=id=>({domain:'territorial',type:'entity',id}),refs=definition.selectedIds.map(ref);selection.setMany(refs,{primary:ref(definition.seedId)});
 ports.feedback.reportOperationError=(error,message,code)=>{errors.push({message:error.message,code});return message;};
 const gatedOperation=definition.phase==='preparation'?'boundary-prepare':'territorial-edit';
 const createWorker=()=>{
  const actual=loaded.createWorker(),id=++workerId,operations=new Map();
  const adapter={onmessage:null,onerror:null,postMessage(message){
   if(message.type==='execute'){
    operations.set(message.requestId,message.operation);
    const call=invocations.find(row=>row.operation===message.operation&&!row.request);require(call,'An actual client invocation owns every execute');
    call.request={worker:id,requestId:message.requestId};
    if(message.operation===gatedOperation&&!heldRequest)heldRequest={worker:id,requestId:message.requestId,operation:message.operation,invocation:call.id};
    event('request',{worker:id,invocation:call.id,message});
   }else event('request',{worker:id,message});
   event('posted',{worker:id,message});actual.postMessage(message);
  },terminate(){event('terminate',{worker:id});return actual.terminate();}};
  actual.onmessage=e=>{const operation=operations.get(e.data.requestId)||null;event('response',{worker:id,operation,message:e.data});
   if(e.data.type==='result'&&id===heldRequest?.worker&&e.data.requestId===heldRequest?.requestId){require(!held,'Only the exact old completed reply is held once');held={worker:id,event:e,adapter};event('held-result',{worker:id,operation,message:e.data});}
   else {event('delivered',{worker:id,message:e.data});adapter.onmessage?.(e);}
  };
  actual.onerror=e=>{event('error',{worker:id,message:String(e)});adapter.onerror?.(e);};return adapter;
 };
 const client={...api.createMapEditWorkerClient({createWorker,getEntities:entityRepository.list,getFeatureById:entityRepository.get,getTargetRevision:()=>state.stateRevision})};
 const execute=client.execute.bind(client);client.execute=(operation,...args)=>{
  const id=++invocation;invocations.push({id,operation});event('client-execute',{invocation:id,operation});const promise=execute(operation,...args);requests.push(promise);
  promise.then(()=>event('client-resolved',{invocation:id,operation}),error=>event('client-rejected',{invocation:id,operation,message:String(error.message),cancelled:!!error.cancelled}));return promise;
 };
 for(const name of ['stop','cancel']){const original=client[name].bind(client);client[name]=(...args)=>{event('client-'+name);return original(...args);};}
 ports.spatialQuery.mapEditClient=client;ports.geometryPreview=geometryPreview;
 const objectPresentation=api.createObjectPresentation();objectPresentation.connect({...ports,objectCatalog:api});objectPresentation.initializeObjectPresentationModel();ports.objectPresentation=objectPresentation;
 const objectCommands=api.createObjectCommands();objectCommands.connect({...ports,selectionServices:api});ports.objectOperationsA=objectCommands;
 const drafts=api.createTerritorialDrafts();drafts.connect({...ports,snapshots:runtime.snapshots,geometryOperations:geometryPreview,applicationServicesB:{sphericalGeometryAreaKm2:api.geometryAreaKm2},projectRestore:{openConfirmModal:()=>{throw Error('Unexpected Apply confirmation');}},countryValidation:{refreshCountryCentroids:noop}});
 const modes=api.createCountryModes();modes.connect({...ports,selectionServices:api,objectOperationsB:objectCommands,readinessUi:{clearNotification:noop},geometryOperations:geometryPreview,territorialEditingA:drafts,territorialEditingB:drafts});ports.countryEditingB=modes;
 const extraction=await extractBoundaryCallbacks(loaded.sourceTexts['assets/js/modules/app-domain-assembly.js']);
 const dependencies={...ports,objectOperationsB:objectCommands,territorialEditingB:drafts,gpuRenderingA:{beginActiveEditPreview:value=>visualEvents.push({kind:'begin',value:clone(value)}),clearActiveEditPreview:reason=>visualEvents.push({kind:'clear',reason})},gpuRenderingB:{updateActiveEditPreview:value=>visualEvents.push({kind:'move',value:clone(value)})}};
 const callbacks=Function('dependencies','territorialEntityRepository','boundaryTouchesGeometry',`return ({${extraction.source}});`)(dependencies,entityRepository,api.boundaryTouchesGeometry);
 const canonical=()=>JSON.stringify(loaded.runtime.observe(runtime).document),beforeCanonical=canonical();
 const record=name=>{phase=name;const p=state.boundaryPreparation;stages[name]={observed:true,canonical:canonical(),canonicalUnchanged:canonical()===beforeCanonical,history:{undo:state.history.length,redo:state.future.length},revision:state.stateRevision,selection:clone(selection.snapshot().selection),tool:state.tool,draft:clone(domains.editingDomain.snapshot().draft),preview:!!state.geometryPreview.session,previewStatus:state.geometryPreview.session?.status||null,
  preparation:p?{status:p.status,workerPending:p.workerPending,current:p.current(),hasResult:!!p.result,hasPacket:!!p.packet,valid:p.result?.valid??null,ownerIds:clone(p.result?.selectedIds||[]),handles:clone(p.result?.handles||[])}:null,transportSequence:transport.length,visualEvents:clone(visualEvents)};};
 const publicAction=(action,details={})=>{phase=action;event('public-action',{action,...details});};
 const release=()=>{if(!held)return;const h=held;held=null;event('released-result',{worker:h.worker,operation:heldRequest.operation,message:h.event.data});event('delivered',{worker:h.worker,message:h.event.data});h.adapter.onmessage?.(h.event);};
 const until=async predicate=>{const deadline=Date.now()+20000;while(!predicate()){require(Date.now()<deadline,'Actual old Worker completion not received');await new Promise(resolve=>setTimeout(resolve,1));}};
 try{
  record('before');publicAction('enter-original');require(modes.enterTerritorialBorderEditFromSelection(),'Public original entry rejected');const preparing=state.boundaryPreparation.promise;record('entered');let committing=null;
  if(definition.phase==='preview'){
   await preparing;require(state.boundaryPreparation?.status==='ready','Original preparation READY required');record('prepared');
   publicAction('begin-move',{nodeKey:definition.move.nodeKey,coordinate:definition.move.coordinate});const gesture=callbacks.beginBoundaryGesture({vertexKey:definition.move.nodeKey,targetRef:ref(definition.seedId)});require(gesture,'Public original gesture rejected');callbacks.moveBoundaryGesture(gesture,definition.move.coordinate);record('drag');phase='commit-old';committing=callbacks.commitBoundaryGesture(gesture);
  }
  await until(()=>!!held);record('oldCompleted');
  publicAction('cancel');modes.cancelActiveMode(false);record('afterCancel');
  publicAction('replace-selection',{ids:definition.replacementIds,primary:definition.replacementPrimary});domains.selectionUiController.replaceMany(definition.replacementIds.map(ref),{primary:ref(definition.replacementPrimary),scope:'countries',reason:'paired-session-replacement',present:false});record('replacementSelected');
  publicAction('enter-replacement');require(modes.enterTerritorialBorderEditFromSelection(),'Public replacement entry rejected');const replacementPreparing=state.boundaryPreparation.promise;record('replacementEntered');
  phase='release-old';release();await Promise.allSettled([preparing,...(committing?[committing]:[]),replacementPreparing,...requests]);await Promise.resolve();record('settled');
  return {case:definition.id,input:clone(definition),beforeCanonical,stages,transport,errors,heldRequest:clone(heldRequest),extractionEvidence:Object.fromEntries(Object.entries(extraction).filter(([key])=>key!=='source')),limits:{rawParity:false,fullDOM:false,pointerPixels:false,gpuRendering:false,fullCanonicalProject:false,workerCPUExecutionPhase:false,nativeBoundaryMoveWorkerInterval:false,heldResultsAreWorkerComplete:true,projectGenerationChange:false,ownerLockCoverage:false}};
 }finally{phase='cleanup';release();client.stop();await Promise.allSettled(requests);domains.selectionUiController.dispose();domains.editingDomain.dispose();}
}

export function verifyBoundarySessionReplacementCase(definition,row){
 const req=(c,m)=>assert.ok(c,m),same=(a,b,m)=>assert.deepEqual(a,b,m),s=row.stages,preview=definition.phase==='preview';
 same(definition,boundarySessionReplacementCases().find(d=>d.id===definition.id),'fixed exact input definition');same(row.input,definition);assert.equal(row.case,definition.id);same(row.errors,[]);same(row.limits,sessionReplacementLimits);
 same(Object.keys(s),sessionReplacementStageNames(definition),'exact ordered stages');
 const baseline=JSON.parse(row.beforeCanonical);assert.equal(JSON.stringify(baseline),row.beforeCanonical,'canonical compact bytes');
 same(baseline.entities.map(({id,geometry})=>({id,geometry})),definition.features.map(f=>({id:f.id,geometry:{type:'Polygon',coordinates:[f.ring]}})),'exact original geometry');
 let cursor=0;
 for(const [name,stage]of Object.entries(s)){
  assert.equal(stage.observed,true,name);assert.equal(stage.canonical,row.beforeCanonical,'unchanged canonical '+name);assert.equal(stage.canonicalUnchanged,true);same(stage.history,{undo:0,redo:0},'unchanged history '+name);assert.equal(stage.revision,0);
  same(stage.draft.coords,[]);assert.equal(stage.draft.historyCount,0);assert.equal(stage.draft.futureCount,0);assert.equal(stage.preview,false,'old preview never publishes '+name);assert.equal(stage.previewStatus,null);
  const replacement=['replacementSelected','replacementEntered','settled'].includes(name),ids=replacement?definition.replacementIds:definition.selectedIds;
  same(stage.selection.items.map(({domain,type,id})=>({domain,type,id})),ids.map(id=>({domain:'territorial',type:'entity',id})),'exact selected domains/order '+name);
  same(stage.selection.keys,ids.map(id=>'territorial:entity:'+id),'exact selection keys '+name);same(stage.selection.items.map(r=>r.key),stage.selection.keys,'item keys agree with references');
  const primary=['before','replacementSelected'].includes(name)?'A':['replacementEntered','settled'].includes(name)?'B':'C';assert.equal(stage.selection.primaryKey,'territorial:entity:'+primary,'actual public primary '+name);
  req(Number.isSafeInteger(stage.transportSequence)&&stage.transportSequence>=cursor&&stage.transportSequence<=row.transport.length,'ordered bounded cursor '+name);cursor=stage.transportSequence;
  const active=!['before','afterCancel','replacementSelected'].includes(name);assert.equal(stage.tool,active?'territorial-border':'select');
  if(!active){assert.equal(stage.preparation,null);continue;}
  assert.equal(stage.preparation?.current,true,'current live preparation '+name);
  const ready=name==='settled'||(preview&&['prepared','drag','oldCompleted'].includes(name));const p=stage.preparation;
  assert.equal(p.status,ready?'ready':'pending');assert.equal(p.workerPending,!ready);assert.equal(p.hasResult,ready);assert.equal(p.hasPacket,ready);assert.equal(p.valid,ready?true:null);
  same(p.ownerIds,ready?(name==='settled'?definition.replacementIds:definition.selectedIds):[]);
  if(ready){const junction=p.handles.filter(h=>h.nodeKey==='1,1');assert.equal(junction.length,1);same(junction[0].coordinate,[1,1]);same(junction[0].ownerIds,['A','B','C']);assert.equal(junction[0].fixed,name==='settled','replacement junction becomes fixed');}
  else same(p.handles,[]);
 }
 const t=row.transport,events=kind=>t.filter(e=>e.kind===kind),one=kind=>{const a=events(kind);assert.equal(a.length,1,'exact '+kind);return a[0];};
 t.forEach((e,i)=>assert.equal(e.sequence,i,'complete transport ordering'));
 const actions=events('public-action');same(actions.map(e=>e.action),['enter-original',...(preview?['begin-move']:[]),'cancel','replace-selection','enter-replacement']);
 const actionStages=[['enter-original','before','entered'],...(preview?[['begin-move','prepared','drag']]:[]),['cancel','oldCompleted','afterCancel'],['replace-selection','afterCancel','replacementSelected'],['enter-replacement','replacementSelected','replacementEntered']];
 for(const [action,before,after]of actionStages){const e=actions.find(e=>e.action===action);req(e.sequence>=s[before].transportSequence&&e.sequence<s[after].transportSequence,'public action in exact interval '+action);}
 const select=actions.find(e=>e.action==='replace-selection');same(select.ids,definition.replacementIds);assert.equal(select.primary,definition.replacementPrimary);
 if(preview){const move=actions.find(e=>e.action==='begin-move');assert.equal(move.nodeKey,definition.move.nodeKey);same(move.coordinate,definition.move.coordinate);}
 const calls=events('client-execute'),requests=events('request').filter(e=>e.message.type==='execute'),responses=events('response').filter(e=>e.message.type==='result'),deliveries=events('delivered').filter(e=>e.message.type==='result'),settlements=t.filter(e=>['client-resolved','client-rejected'].includes(e.kind));
 const operations=['boundary-prepare',...(preview?['boundary-move','territorial-edit']:[]),'boundary-prepare'];same(calls.map(e=>e.operation),operations);same(requests.map(e=>e.message.operation),operations);same(calls.map(e=>e.invocation),operations.map((_,i)=>i+1));
 assert.equal(responses.length,operations.length);assert.equal(deliveries.length,operations.length);assert.equal(settlements.length,operations.length);
 const held=one('held-result'),released=one('released-result'),gatedOperation=preview?'territorial-edit':'boundary-prepare',old=requests[preview?2:0];
 same(row.heldRequest,{worker:old.worker,requestId:old.message.requestId,operation:gatedOperation,invocation:old.invocation},'exact original held identity');
 req(held.sequence<s.oldCompleted.transportSequence,'old actual completion precedes snapshot');req(released.sequence>=s.replacementEntered.transportSequence&&released.sequence<s.settled.transportSequence,'old released only after replacement entry snapshot');
 for(const e of [held,released]){assert.equal(e.worker,old.worker);assert.equal(e.operation,gatedOperation);same(e.message,responses.find(r=>r.worker===old.worker&&r.message.requestId===old.message.requestId)?.message,'unaltered exact old completed response');}
 const key=e=>e.worker+':'+e.message.requestId;assert.equal(new Set(requests.map(key)).size,operations.length);assert.equal(new Set(responses.map(key)).size,operations.length);assert.equal(new Set(deliveries.map(key)).size,operations.length);
 const rebases=events('request').filter(e=>e.message.type==='rebase');assert.equal(rebases.length,preview?1:2);const sourceForWorker=new Map();
 for(const e of t){
  req(['public-action','client-execute','client-resolved','client-rejected','client-cancel','client-stop','request','posted','response','delivered','held-result','released-result','terminate'].includes(e.kind),'known transport event');
  if(['request','posted','response','delivered','held-result','released-result','terminate'].includes(e.kind))req(Number.isSafeInteger(e.worker)&&e.worker>0,'actual worker identity');
  if(e.kind==='request'){
   const next=t[e.sequence+1];assert.equal(next?.kind,'posted','every exact envelope posted once');assert.equal(next.worker,e.worker);same(next.message,e.message,'unmodified post');
   req(['rebase','execute','cancel'].includes(e.message.type),'known request type');
   if(['rebase','execute'].includes(e.message.type))for(const field of ['dataRevision','geometryRevision','targetRevision'])req(Number.isSafeInteger(e.message[field])&&e.message[field]>=(field==='targetRevision'?0:1),'required actual source revision '+field);
   if(e.message.type==='rebase'){
    sourceForWorker.set(e.worker,e.message);same(e.message.boundaryIds,definition.selectedIds);assert.equal(e.message.targetRevision,0);
    const patch=e.message.editSources;same(patch.removedKeys,[]);req(Number.isSafeInteger(patch.sourceRevision)&&patch.sourceRevision>0);
    same(patch.patches.map(p=>p.key),definition.selectedIds.map(id=>'territorial:'+id));
    for(const [i,p]of patch.patches.entries()){const f=baseline.entities[i];assert.equal(p.kind,'territorial');same(p.geometry,f.geometry);same(p.metadata,{type:'Feature',id:f.id,properties:f.properties},'full original metadata in source rebase');}
   }
   if(e.message.type==='execute'){
    assert.equal(e.message.jobKey,e.message.operation==='territorial-edit'?'map-edit:territorial-edit':e.message.operation,'required exact client job identity');
    const rebase=sourceForWorker.get(e.worker);req(rebase,'execute follows authenticated actual source rebase');for(const field of ['dataRevision','geometryRevision','targetRevision'])assert.equal(e.message[field],rebase[field],'execute source identity '+field);assert.equal(e.message.sourceRevision,rebase.editSources.sourceRevision);
   }
   if(e.message.type==='cancel'){same(e.message,{type:'cancel',requestId:old.message.requestId,targetRevision:0,reason:'superseded'});assert.equal(e.worker,old.worker);req(e.sequence>=s.oldCompleted.transportSequence&&e.sequence<s.afterCancel.transportSequence);}
  }
  if(e.kind==='posted'){const prior=t[e.sequence-1];assert.equal(prior?.kind,'request');same(prior.message,e.message);assert.equal(prior.worker,e.worker);}
  if(e.kind==='response'&&e.message.type!=='result'){assert.equal(e.message.type,'ready');req(sourceForWorker.has(e.worker));const delivery=t[e.sequence+1];assert.equal(delivery?.kind,'delivered');same(delivery.message,e.message);assert.equal(delivery.worker,e.worker);}
  if(e.kind==='delivered'&&e.message.type!=='result'){const prior=t[e.sequence-1];assert.equal(prior?.kind,'response');same(prior.message,e.message);assert.equal(prior.worker,e.worker);}
 }
 assert.equal(events('request').filter(e=>e.message.type==='cancel').length,1);
 for(const [i,request]of requests.entries()){
  assert.equal(request.invocation,i+1);assert.equal(request.message.requestId,i+1,'exact request identity sequence');req(request.sequence>calls[i].sequence);
  const response=responses.find(e=>key(e)===key(request)),delivery=deliveries.find(e=>key(e)===key(request)),settlement=settlements.find(e=>e.invocation===request.invocation);
  req(response&&delivery&&settlement,'each exact request has one real result/delivery/settlement');req(response.sequence>request.sequence);assert.equal(response.operation,request.message.operation);assert.equal(response.message.ok,true);for(const field of ['jobKey','dataRevision','geometryRevision','targetRevision'])assert.equal(response.message[field],request.message[field],'response belongs to exact source and job '+field);same(delivery.message,response.message);req(delivery.sequence>response.sequence);assert.equal(settlement.operation,request.message.operation);
  if(request===old){assert.equal(settlement.kind,'client-rejected');assert.equal(settlement.cancelled,true);req(settlement.sequence>=s.afterCancel.transportSequence);req(delivery.sequence>released.sequence);}
  else {assert.equal(settlement.kind,'client-resolved');req(settlement.sequence>delivery.sequence);}
  const p=request.message.payload;
  if(request.message.operation==='boundary-prepare'){
   same(p.targetIds,i===0?definition.selectedIds:definition.replacementIds);assert.equal(p.mode,'border');assert.equal(p.autoSeedId,null);assert.equal(p.neighborsOnly,false);
   same(response.message.result.selectedIds,p.targetIds,'actual preparation ownership');
   if(i===requests.length-1){same(s.settled.preparation.handles,response.message.result.handles,'actual returned replacement handles');req(delivery.sequence>s.replacementEntered.transportSequence);req(settlement.sequence<s.settled.transportSequence);}
  }else if(request.message.operation==='boundary-move'){
   assert.equal(p.nodeKey,definition.move.nodeKey);same(p.coordinate,definition.move.coordinate);assert.equal(p.preparationId,responses[0].message.result.preparationId,'move uses exact original preparation');
  }else{
   assert.equal(p.operation,'country-boundary');assert.equal(p.targetId,'A');same(p.featurePatches.map(f=>f.id),definition.selectedIds);
   for(const [j,f]of p.featurePatches.entries())same(f.geometry,{type:'Polygon',coordinates:[definition.features[j].ring.map(xy=>xy[0]===1&&xy[1]===1?definition.move.coordinate:xy)]},'real moved geometry fanout');
  }
 }
 const cancelled=actions.find(e=>e.action==='cancel');assert.equal(events('client-cancel').length,1);req(one('client-cancel').sequence>cancelled.sequence&&one('client-cancel').sequence<s.afterCancel.transportSequence);
 const stops=events('client-stop');assert.equal(stops.length,preview?1:2);assert.equal(stops.at(-1).phase,'cleanup');assert.equal(events('terminate').length,preview?1:2);
 if(!preview){assert.equal(stops[0].phase,'cancel');req(stops[0].sequence<s.afterCancel.transportSequence);assert.notEqual(requests.at(-1).worker,old.worker);}else assert.equal(requests.at(-1).worker,old.worker);
 return row;
}
