import {extractBoundaryCallbacks} from '../m974-snap-boundary/boundary-runtime.mjs';

export function boundaryTimingCases(){
 const features=[
  {id:'A',ring:[[0,0],[1,0],[1,1],[1,2],[0,2],[0,0]]},
  {id:'B',ring:[[1,0],[2,0],[2,1],[1,1],[1,0]]},
  {id:'C',ring:[[1,1],[2,1],[2,2],[1,2],[1,1]]},
 ];
 const dense=features.map(row=>({...row,ring:row.ring.flatMap((a,i)=>i===row.ring.length-1?[a]:Array.from({length:32},(_,j)=>{const b=row.ring[i+1];return [a[0]+(b[0]-a[0])*j/32,a[1]+(b[1]-a[1])*j/32];}))}));
 return ['prepare-queued','render-adoption','move-queued'].flatMap(phase=>['none','cancel','select'].map(action=>({
  id:`${phase}-${action}`,phase,action,features:phase==='render-adoption'?dense:features,selectedIds:['A','B','C'],seedId:'A',selectionAfter:['C'],move:{nodeKey:'1,1',coordinate:[1,1.25]},
 })));
}

// Pure wiring and observation. Pinned production modules execute unchanged.
export async function runBoundaryTimingCase(loaded,definition){
 const {api}=loaded,clone=x=>structuredClone(x),noop=()=>{},require=(c,m)=>{if(!c)throw Error(m);};
 const runtime=loaded.runtime.createRuntime(api),{state,entityStore,entityRepository,ports,domains,geometryPreview}=runtime;
 const features=definition.features.map(row=>api.createTerritorialFeature({id:row.id,name:row.id,entityKind:'general',coverageMode:'explicit',geometry:{type:'Polygon',coordinates:[clone(row.ring)]}}));
 entityStore.restoreProject(api.createStaticTerritorialSnapshot(features));
 Object.assign(state,{distributionLayers:[],distributionEntries:[],labels:[],labelSettings:{},itemVisibility:{},layerPresentation:{schemaVersion:4,styles:{},objectStyles:{},objectOrder:[]}});state.historyDirtyEntityIds.clear();
 const errors=[],transport=[],stages={},visualEvents=[],requests=[];let phase='setup',workerId=0,invocation=0,hold=true,adoptionAction=null,adoptionResult=null;
 const event=(kind,details={})=>transport.push({sequence:transport.length,phase,kind,...clone(details)}),queued=[];
 const selection=api.createSelectionDomain({projectDomain:domains.projectDomain,onSelectionChanged:snapshot=>{state.selected=snapshot.selection.items.find(ref=>ref.key===snapshot.selection.primaryKey)||null;}});
 domains.selectionDomain=selection;domains.selectionUiController=api.createSelectionUiController({selectionDomain:selection,resolveRef:api.normalizeObjectRef});
 domains.editingDomain=api.createEditingDomain({projectDomain:domains.projectDomain,selectionDomain:selection,toolController:{getGeometryPreviewSession:()=>state.geometryPreview.session,discardGeometryPreview:geometryPreview.discardActiveGeometryPreview},onEditingStateChanged:snapshot=>{state.tool=snapshot.activeTool;}});
 const refs=definition.selectedIds.map(id=>({domain:'territorial',type:'entity',id}));selection.setMany(refs,{primary:refs[0]});
 ports.feedback.reportOperationError=(error,message,code)=>{errors.push({message:error.message,code});return message;};
 const operationForGate=definition.phase==='prepare-queued'?'boundary-prepare':definition.phase==='move-queued'?'boundary-move':null;
 const createWorker=()=>{
  const actual=loaded.createWorker(),id=++workerId,operations=new Map();let terminated=false;
  const adapter={onmessage:null,onerror:null,
   postMessage(message){
    if(message.type==='execute')operations.set(message.requestId,message.operation);
    event('request',{worker:id,message});
    if(hold&&message.type==='execute'&&message.operation===operationForGate){queued.push({id,message,actual,terminated:()=>terminated});event('queued-before-worker-post',{worker:id,message});}
    else {event('posted',{worker:id,message});actual.postMessage(message);}
   },terminate(){terminated=true;event('terminate',{worker:id});return actual.terminate();}};
  actual.onmessage=e=>{event('response',{worker:id,operation:operations.get(e.data.requestId)||null,message:e.data});event('delivered',{worker:id,message:e.data});adapter.onmessage?.(e);};
  actual.onerror=e=>{event('error',{worker:id,message:String(e)});adapter.onerror?.(e);};return adapter;
 };
 const client={...api.createMapEditWorkerClient({createWorker,getEntities:entityRepository.list,getFeatureById:entityRepository.get,getTargetRevision:()=>state.stateRevision})};
 const execute=client.execute.bind(client);
 client.execute=(operation,...args)=>{
  const id=++invocation;event('client-execute',{invocation:id,operation});const promise=execute(operation,...args);requests.push(promise);
  promise.then(response=>{
   event('client-resolved',{invocation:id,operation});
   if(operation==='boundary-prepare'&&definition.phase==='render-adoption'){adoptionResult=response.result;queueMicrotask(()=>{
    // This microtask runs after the application's .then starts but before its
    // await adoptBoundaryRenderPacketAsync checkpoint completes its freezing.
    try {const p=state.boundaryPreparation;require(p?.workerPending===false&&p.status==='pending'&&!p.packet,'Genuine client-settled/adoption-unpublished interval required');record('interval');performAction();adoptionAction.resolve();}
    catch(error){adoptionAction.reject(error);}
   });}
  },error=>event('client-rejected',{invocation:id,operation,message:String(error.message),cancelled:!!error.cancelled}));return promise;
 };
 for(const name of ['stop','cancel']){const original=client[name].bind(client);client[name]=(...args)=>{event('client-'+name);return original(...args);};}
 ports.spatialQuery.mapEditClient=client;ports.geometryPreview=geometryPreview;
 const objectPresentation=api.createObjectPresentation();objectPresentation.connect({...ports,objectCatalog:api});objectPresentation.initializeObjectPresentationModel();ports.objectPresentation=objectPresentation;
 const objectCommands=api.createObjectCommands();objectCommands.connect({...ports,selectionServices:api});ports.objectOperationsA=objectCommands;
 const drafts=api.createTerritorialDrafts();drafts.connect({...ports,snapshots:runtime.snapshots,geometryOperations:geometryPreview,applicationServicesB:{sphericalGeometryAreaKm2:api.geometryAreaKm2},projectRestore:{openConfirmModal:()=>{throw Error('Unexpected Apply confirmation in read-only diagnostic');}},countryValidation:{refreshCountryCentroids:noop}});
 const modes=api.createCountryModes();modes.connect({...ports,selectionServices:api,objectOperationsB:objectCommands,readinessUi:{clearNotification:noop},geometryOperations:geometryPreview,territorialEditingA:drafts,territorialEditingB:drafts});ports.countryEditingB=modes;
 const extraction=await extractBoundaryCallbacks(loaded.sourceTexts['assets/js/modules/app-domain-assembly.js']);
 const dependencies={...ports,objectOperationsB:objectCommands,territorialEditingB:drafts,
  gpuRenderingA:{beginActiveEditPreview:value=>visualEvents.push({kind:'begin',value:clone(value)}),clearActiveEditPreview:reason=>visualEvents.push({kind:'clear',reason})},
  gpuRenderingB:{updateActiveEditPreview:value=>visualEvents.push({kind:'move',value:clone(value)})}};
 const callbacks=Function('dependencies','territorialEntityRepository','boundaryTouchesGeometry',`return ({${extraction.source}});`)(dependencies,entityRepository,api.boundaryTouchesGeometry);
 let gesture=null;
 const freezeEvidence=()=>{if(!adoptionResult)return null;const seen=new Set(),stack=[adoptionResult];let frozen=0,unfrozen=0;while(stack.length){const value=stack.pop();if(!value||typeof value!=='object'||seen.has(value))continue;seen.add(value);if(Object.isFrozen(value))frozen++;else unfrozen++;for(const child of Object.values(value))if(child&&typeof child==='object')stack.push(child);}return {frozen,unfrozen};};
 const canonical=()=>JSON.stringify(loaded.runtime.observe(runtime).document),beforeCanonical=canonical();
 const record=(name,outcome=null)=>{
  phase=name;const p=state.boundaryPreparation,sel=selection.snapshot().selection,edit=domains.editingDomain.snapshot();
  stages[name]={observed:true,outcome,canonical:canonical(),canonicalUnchanged:canonical()===beforeCanonical,history:{undo:state.history.length,redo:state.future.length},revision:state.stateRevision,
   selection:clone(sel),tool:state.tool,draft:clone(edit.draft),preview:!!state.geometryPreview.session,previewStatus:state.geometryPreview.session?.status||null,
   preparation:p?{status:p.status,workerPending:p.workerPending,current:p.current(),hasResult:!!p.result,hasPacket:!!p.packet,valid:p.result?.valid??null,ownerIds:clone(p.result?.selectedIds||[])}:null,
   gesture:gesture?{changed:gesture.changed,coordinate:clone(gesture.coordinate),ownerIds:[...gesture.affectedIds],features:gesture.features?clone([...gesture.features.values()]):null}:null,
   transportSequence:transport.length,visualEvents:clone(visualEvents),adoptionFreezeEvidence:freezeEvidence()};return stages[name];
 };
 const performAction=()=>{
  phase='action';event('public-action',{action:definition.action,selection:definition.action==='select'?definition.selectionAfter:null});
  if(definition.action==='cancel')modes.cancelActiveMode(false);
  else if(definition.action==='select')domains.selectionUiController.replaceMany(definition.selectionAfter.map(id=>({domain:'territorial',type:'entity',id})),{primary:{domain:'territorial',type:'entity',id:definition.selectionAfter.at(-1)},scope:'countries',reason:'diagnostic-public-selection',present:false});
  record('afterAction');
 };
 const release=()=>{hold=false;for(const q of queued.splice(0)){event('released-request',{worker:q.id,message:q.message,terminated:q.terminated()});if(!q.terminated()){event('posted',{worker:q.id,message:q.message});q.actual.postMessage(q.message);}}};
 const until=async predicate=>{const deadline=Date.now()+20000;while(!predicate()){require(Date.now()<deadline,'Actual production request did not reach declared interval');await new Promise(resolve=>setTimeout(resolve,1));}};
 let preparing,committing;
 try {
  record('before');
  let adoptionPromise;if(definition.phase==='render-adoption')adoptionPromise=new Promise((resolve,reject)=>{adoptionAction={resolve,reject};});
  phase='entry';require(modes.enterTerritorialBorderEditFromSelection(),'Actual boundary entry failed');preparing=state.boundaryPreparation.promise;record('entered');
  if(definition.phase==='prepare-queued'){await until(()=>queued.length>0);record('interval');performAction();release();await preparing;}
  else if(definition.phase==='render-adoption'){await adoptionPromise;await preparing;}
  else {
   await preparing;require(state.boundaryPreparation?.status==='ready','Preparation READY required');record('prepared');
   gesture=callbacks.beginBoundaryGesture({vertexKey:definition.move.nodeKey,targetRef:refs[0]});require(gesture,'Actual boundary gesture failed');callbacks.moveBoundaryGesture(gesture,definition.move.coordinate);record('drag');
   phase='commit';committing=callbacks.commitBoundaryGesture(gesture);await until(()=>queued.length>0);record('interval');performAction();release();await committing;
  }
  await Promise.allSettled(requests);await Promise.resolve();record('settled');
  return {case:definition.id,input:clone(definition),stages,transport,errors,beforeCanonical,
   extractionEvidence:Object.fromEntries(Object.entries(extraction).filter(([key])=>key!=='source')),
   limits:{rawParity:false,fullDOM:false,pointerPixels:false,gpuRendering:false,fullCanonicalProject:false,workerCPUExecutionPhase:false,heldResultsAreWorkerComplete:false,
    preCompletionMechanism:operationForGate?'actual execute envelope queued before Worker postMessage':null,renderAdoptionPending:definition.phase==='render-adoption',renderAdoptionMeaning:'production async adoption has frozen some but not all actual result objects; not a GPU frame',publicSelectionStimulus:'selectionUiController.replaceMany with existing C',publicCancelStimulus:'countryModes.cancelActiveMode(false)'}};
 } finally {phase='cleanup';release();client.stop();await Promise.allSettled(requests);domains.selectionUiController.dispose();domains.editingDomain.dispose();}
}

export function verifyBoundaryTimingCase(definition,row){
 const req=(c,m)=>{if(!c)throw Error(m);},same=(a,b,m)=>req(JSON.stringify(a)===JSON.stringify(b),m);
 req(row.case===definition.id,'case identity');same(row.input,definition,'exact input');same(row.errors,[],'no unexpected errors');
 const expected=['before','entered',...(definition.phase==='move-queued'?['prepared','drag']:[]),'interval','afterAction','settled'];same(Object.keys(row.stages),expected,'exact ordered stages');
 for(const [name,s]of Object.entries(row.stages)){req(s.observed,'observed '+name);req(s.canonical===row.beforeCanonical&&s.canonicalUnchanged,'canonical immutability '+name);same(s.history,{undo:0,redo:0},'no project history '+name);req(s.revision===0,'no revision '+name);same(s.draft.coords,[],'no free-draft coordinates in boundary tool '+name);req(s.draft.historyCount===0&&s.draft.futureCount===0,'no free-draft history '+name);}
 const interval=row.stages.interval,p=interval.preparation;req(p&&p.current,'current preparation at interval');
 if(definition.phase==='render-adoption'){req(p.workerPending===false&&p.status==='pending'&&!p.hasPacket&&!p.hasResult,'client settled before adoption publication');req(interval.adoptionFreezeEvidence?.frozen===512&&interval.adoptionFreezeEvidence?.unfrozen>0,'actual async packet freezing remains incomplete');req(row.transport.some(e=>e.kind==='client-resolved'&&e.operation==='boundary-prepare'&&e.sequence<interval.transportSequence),'actual earlier client settlement');}
 else {const operation=definition.phase==='prepare-queued'?'boundary-prepare':'boundary-move';req(p.status===(definition.phase==='prepare-queued'?'pending':'moving'),'correct actual pending status');const q=row.transport.find(e=>e.kind==='queued-before-worker-post'&&e.message.operation===operation);req(q&&q.sequence<interval.transportSequence,'actual outgoing request queued before action');req(!row.transport.some(e=>e.kind==='posted'&&e.worker===q.worker&&e.message.requestId===q.message.requestId&&e.message.type==='execute'&&e.sequence<row.stages.afterAction.transportSequence),'worker execute not posted before action');}
 if(definition.phase==='render-adoption'){const f=row.stages.settled.adoptionFreezeEvidence;req(f?.frozen>=512,'actual adoption result observed through settlement');req((f.unfrozen>0)===(definition.action==='cancel'),'cancel interrupts adoption and live preparation fully adopts');}
 const action=row.transport.filter(e=>e.kind==='public-action');req(action.length===1&&action[0].action===definition.action,'one real public action');
 let cursor=0;for(const name of expected){const sequence=row.stages[name].transportSequence;req(Number.isInteger(sequence)&&sequence>=cursor&&sequence<=row.transport.length,'ordered bounded stage transport cursor '+name);cursor=sequence;}
 req(interval.transportSequence<=action[0].sequence&&action[0].sequence<row.stages.afterAction.transportSequence,'public action occurs inside the observed interval before its afterAction snapshot');
 same(action[0].selection,definition.action==='select'?definition.selectionAfter:null,'actual declared public selection action');
 const afterAction=row.stages.afterAction;
 if(definition.action==='cancel')req(afterAction.tool==='select'&&!afterAction.preparation&&!afterAction.preview,'immediate public Cancel state');
 else if(definition.action==='select')same(afterAction.selection.items.map(ref=>ref.id),definition.selectionAfter,'immediate public selection replacement');
 else same(afterAction.selection,interval.selection,'no-op selection unchanged');
 const settled=row.stages.settled;
 if(definition.action==='cancel'){req(settled.tool==='select'&&!settled.preview&&!settled.preparation,'cancel cannot resurrect old preparation or preview');same(settled.selection.items.map(x=>x.id),definition.selectedIds,'cancel restores selected owners');req(settled.selection.primaryKey.endsWith(':C'),'pinned root cancel primary fallback');}
 else {
  req(settled.preparation?.status==='ready','web remains READY after no-op or public object selection');
  if(definition.action==='select')same(settled.selection.items.map(x=>x.id),definition.selectionAfter,'real new selection survives');
  req(settled.preview===(definition.phase==='move-queued'),'only actual move creates preview');
 }
 const stops=row.transport.filter(e=>e.kind==='client-stop'&&e.phase!=='cleanup');
 req(stops.length===(definition.action==='cancel'&&definition.phase!=='render-adoption'?1:0),'stop distinguishes pending worker/move from client-settled adoption');
 row.transport.forEach((e,i)=>req(e.sequence===i,'complete transport sequence'));
 const sent=new Map(),posted=new Map(),incoming=new Map(),delivered=new Set(),invocations=new Map(),settledCalls=new Set(),queuedCalls=new Map();
 const key=e=>e.worker+':'+e.message.requestId;
 for(const e of row.transport){
  req(['client-execute','client-resolved','client-rejected','client-stop','client-cancel','request','queued-before-worker-post','posted','response','delivered','terminate','error','released-request','public-action'].includes(e.kind),'known evidence event');
  if(e.kind==='request'&&e.message.type==='execute'){req(!sent.has(key(e)),'one outgoing execute envelope');sent.set(key(e),e.message);}
  if(e.kind==='client-execute'){req(!invocations.has(e.invocation),'unique actual client invocation');invocations.set(e.invocation,e.operation);}
  if(['client-resolved','client-rejected'].includes(e.kind)){req(invocations.get(e.invocation)===e.operation&&!settledCalls.has(e.invocation),'one matching client settlement');settledCalls.add(e.invocation);}
  if(e.kind==='queued-before-worker-post'){req(e.message.type==='execute','only actual execute is queued');req(!queuedCalls.has(key(e)),'one queue event per execute');queuedCalls.set(key(e),e.message);}
  if(e.kind==='posted'&&e.message.type==='execute'){req(!posted.has(key(e)),'one actual Worker post');same(e.message,sent.get(key(e)),'posted envelope is the real requested execute');posted.set(key(e),e.message);}
  if(e.kind==='response'&&e.message.type==='result'){req(posted.has(key(e)),'result follows an actual execute post');req(posted.get(key(e)).operation===e.operation,'result operation matches actual posted request');req(!incoming.has(key(e)),'one actual result per posted request');incoming.set(key(e),e.message);}
  if(e.kind==='delivered'&&e.message.type==='result'){req(incoming.has(key(e))&&!delivered.has(key(e)),'delivery follows exactly one real Worker result');same(e.message,incoming.get(key(e)),'unaltered Worker result delivered');delivered.add(key(e));}
 }
 const calls=row.transport.filter(e=>e.kind==='client-execute'),resolved=row.transport.filter(e=>e.kind==='client-resolved');
 same([...sent.values()].map(m=>m.operation),calls.map(e=>e.operation),'every client invocation reaches its actual execute envelope');
 same([...posted.values()].map(m=>m.operation),resolved.map(e=>e.operation),'every resolved client has exactly one actual posted execute');
 req(incoming.size===resolved.length&&delivered.size===resolved.length,'every resolved client has incoming and delivered actual Worker result');
 for(const resolution of resolved){const requestEntry=[...posted].find(([,m])=>m.operation===resolution.operation);req(!!requestEntry,'resolved operation was posted');const [requestKey]=requestEntry;req(incoming.get(requestKey)?.ok===true,'resolved client has successful actual Worker result');const delivery=row.transport.find(e=>e.kind==='delivered'&&e.message.type==='result'&&key(e)===requestKey);req(delivery.sequence<resolution.sequence,'actual delivery precedes client settlement');}
 const releaseEvents=row.transport.filter(e=>e.kind==='released-request');req(queuedCalls.size===(definition.phase==='render-adoption'?0:1),'exact pre-execution gate count');req(releaseEvents.length===queuedCalls.size,'every queued execute was explicitly released or discarded after termination');
 for(const e of releaseEvents){req(e.sequence>=row.stages.afterAction.transportSequence&&e.sequence<row.stages.settled.transportSequence,'queued execute release follows action and precedes settlement');same(e.message,queuedCalls.get(key(e)),'release belongs to original queued execute');req(e.terminated===(definition.action==='cancel'),'only cancelled queued execute is discarded after termination');}
 req(settledCalls.size===invocations.size,'all real client calls settled');req(delivered.size===incoming.size,'every actual result delivered');
 const expectedOperations=['boundary-prepare',...(definition.phase==='move-queued'?['boundary-move',...(definition.action==='cancel'?[]:['territorial-edit'])]:[])];same([...invocations.values()],expectedOperations,'exact production request boundary sequence');
 for(const [id,operation]of invocations){const kind=row.transport.find(e=>e.invocation===id&&['client-resolved','client-rejected'].includes(e.kind)).kind;req(kind===(definition.action==='cancel'&&operation===operationForDefinition(definition)?'client-rejected':'client-resolved'),'actual cancellation settlement');}
 function operationForDefinition(d){return d.phase==='prepare-queued'?'boundary-prepare':d.phase==='move-queued'?'boundary-move':null;}
 req(row.limits.rawParity===false&&row.limits.workerCPUExecutionPhase===false&&row.limits.gpuRendering===false&&row.limits.fullDOM===false&&row.limits.pointerPixels===false&&row.limits.fullCanonicalProject===false,'honest limits');req(row.limits.renderAdoptionPending===(definition.phase==='render-adoption'),'exact adoption scope');req(row.limits.heldResultsAreWorkerComplete===false,'no held-completed-result substitution');return row;
}
