// Browser-portable actual production orchestration. No geometry or rank implementation lives here.
export async function runSourceHistoryCase(loaded, definition) {
 const {api}=loaded, clone=value=>structuredClone(value), noop=()=>{}, require=(value,message)=>{if(!value)throw Error(message);};
 const square=(x0,y0,x1,y1)=>({type:'Polygon',coordinates:[[[x0,y0],[x0,y1],[x1,y1],[x1,y0],[x0,y0]]]}),
  make=(id,geometry,parentId='')=>api.createTerritorialFeature({id,name:id,entityKind:'general',parentId,coverageMode:parentId?'partition':'explicit',geometry});
 const queryCoordinate=definition.queryCoordinate||[-20,-20];
 const input=clone(definition), runtime=loaded.runtime.createRuntime(api), {state,entityStore,entityRepository,snapshots,ports,domains,geometryPreview}=runtime;
 const features=definition.territories?definition.territories.map(row=>{const polygon=square(...row.bounds),geometry=row.geometryType==='MultiPolygon'?{type:'MultiPolygon',coordinates:[polygon.coordinates]}:polygon;return make(row.id,geometry,row.parentId||'');}):definition.features||[make('target',square(-5,0,0,10)),make('donor',square(0,0,10,10)),make('parent',square(30,0,40,10)),make('child-left',square(30,0,35,10),'parent'),make('child-right',square(35,0,40,10),'parent')];
 entityStore.restoreProject(api.createStaticTerritorialSnapshot(clone(features)));
 Object.assign(state,{selected:null,tool:'select',distributionEntries:[],labels:[],labelSettings:{},itemVisibility:{},layerVisibility:{},layerPresentation:{schemaVersion:4,styles:{},objectStyles:{},objectOrder:[]},pendingCountryRenderIds:new Set(),spatialIndex:[],boundaryEditEntityIds:[],coastEditCountryId:null,territorySelectionSession:null});
 state.genericFeatures=api.normalizeGenericFeatureCollection(definition.genericFeatures||['ga','gb'].map(id=>({type:'Feature',id,properties:{schemaVersion:1,name:id},geometry:square(...(definition.genericBounds||[-20,-20,-18,-18]))})));
 state.historyDirtyEntityIds.clear();geometryPreview.initializeBoundarySelectionAnalysisCache();
 let generation=1,phase='setup',workerSequence=0;const transport=[],requests=[],stages={},errors=[],pending=new Set(),held=[],holds=new Set();
 const event=(direction,extra={})=>transport.push({sequence:transport.length,phase,direction,...clone(extra)});
 const createWorker=()=>{
  const actual=loaded.createWorker(),worker=++workerSequence,operations=new Map();let terminated=false;
  const adapter={onmessage:null,onerror:null,postMessage(message){if(message.type==='execute')operations.set(message.requestId,message.operation);event('request',{worker,message});actual.postMessage(message);},terminate(){terminated=true;event('terminate',{worker});return actual.terminate();}};
  actual.onmessage=e=>{const operation=operations.get(e.data.requestId)||null;event('response',{worker,message:e.data});
   if(e.data.type==='result'&&holds.has(operation)){held.push({worker,operation,event:e,adapter,isTerminated:()=>terminated});event('held',{worker,operation,requestId:e.data.requestId});}
   else {event('delivered',{worker,message:e.data,terminated});adapter.onmessage?.(e);}};
  actual.onerror=error=>{event('error',{worker,message:{message:error.message||String(error)}});adapter.onerror?.(error);};return adapter;
 };
 const until=async(predicate,message='Expected source-history lifecycle state did not arrive')=>{const limit=Date.now()+15000;while(!predicate()){if(Date.now()>limit)throw Error(message);await new Promise(resolve=>setTimeout(resolve,1));}};
 const hold=operation=>holds.add(operation),waitHeld=operation=>until(()=>held.some(row=>row.operation===operation),'No real Worker result to hold for '+operation),release=operation=>{
  holds.delete(operation);for(let index=0;index<held.length;){const row=held[index];if(row.operation!==operation){index++;continue;}held.splice(index,1);event('released',{worker:row.worker,operation,requestId:row.event.data.requestId});event('delivered',{worker:row.worker,message:row.event.data,terminated:row.isTerminated()});row.adapter.onmessage?.(row.event);}
 };
 let client;
 const makeClient=options=>{
  const raw=api.createMapEditWorkerClient({...options,createWorker});
  const recording={...raw,execute(operation,message,options){const row={invocation:requests.length+1,phase,operation,message:clone(message),status:'pending'};requests.push(row);event('client-execute',{invocation:row.invocation,operation});
   const p=raw.execute(operation,message,options).then(response=>{row.status='resolved';row.response=clone(response);event('client-resolved',{invocation:row.invocation,operation,requestId:response.requestId});return response;},error=>{row.status='rejected';row.error={message:error.message,code:error.code||null,cancelled:error.cancelled===true};event('client-rejected',{invocation:row.invocation,operation,error:row.error});throw error;});pending.add(p);p.then(()=>pending.delete(p),()=>pending.delete(p));return p;},
   stop(){event('client-stop');return raw.stop();},cancel(){event('client-cancel');return raw.cancel();},rebase(...args){event('client-rebase');return raw.rebase(...args);},
   syncPatch(ids){const values=[...ids],result=raw.syncPatch(ids);event('client-sync-patch',{ids:values,result});return result;}};
  return recording;
 };
 domains.projectDomain.getGeneration=()=>generation;
 const selection=api.createSelectionDomain({projectDomain:domains.projectDomain,onSelectionChanged:snapshot=>{state.selected=snapshot.selection.items.find(ref=>ref.key===snapshot.selection.primaryKey)||null;}});
 domains.selectionDomain=selection;domains.selectionUiController=api.createSelectionUiController({selectionDomain:selection,resolveRef:api.normalizeObjectRef});
 domains.editingDomain=api.createEditingDomain({projectDomain:domains.projectDomain,selectionDomain:selection,toolController:{getGeometryPreviewSession:()=>state.geometryPreview.session,discardGeometryPreview:geometryPreview.discardActiveGeometryPreview},onEditingStateChanged:snapshot=>{state.tool=snapshot.activeTool;}});
 ports.feedback.reportOperationError=(error,message,code)=>{errors.push({message:error.message,code});return message;};
 Object.assign(ports,{snapshots,projectSnapshots:snapshots,geometryPreview,geometryOperations:geometryPreview,selectionServices:api,objectModelB:{territorialApplicationService:runtime.service},
  labels:{countryOutlineCache:new WeakMap()},territoryGeometry:{ringHitTester:{invalidate:noop}},countryCommands:{bumpLandRevision:()=>{ports.countries.countryLandRevision++;}},
  countryValidation:{refreshCountryCentroids:noop},propertyEditingA:{distributionLayerById:id=>state.distributionLayers.find(row=>row.id===id)},
  readinessUi:{clearNotification:noop},hydroPresentation:{hydroEditById:()=>null},surfaces:{...ports.surfaces,isMobile:()=>false}});
 ports.countries.countryLandRevision=0;ports.rendering.gpuMapRenderer.applyCountryPatch=noop;
 const spatial=api.createSpatialIndex();spatial.connect({...ports,spatialFactories:{createMapEditWorkerClient:makeClient},platform:{...ports.platform,runtimeAssetUrl:value=>value},cutGeometry:{coordinateBounds:api.coordinateBounds}});
 spatial.initializeGeometryBoundsCache();spatial.initializeApplyingMapEditWorkerResult();client=spatial.mapEditClient;
 ports.spatialQuery.mapEditClient=client;ports.spatialQuery.markCountryGeometriesChanged=spatial.markCountryGeometriesChanged;
 const objectPresentation=api.createObjectPresentation();objectPresentation.connect({...ports,objectCatalog:api});objectPresentation.initializeObjectPresentationModel();ports.objectPresentation=objectPresentation;
 const land=api.createLandRelations();land.connect(ports);ports.landRelations=land;
 let modalOutcome=null;ports.projectRestore={openConfirmModal:modal=>{modalOutcome=modal.onConfirm();}};
 const objects=api.createObjectCommands();objects.connect(ports);ports.objectOperationsB=objects;
 const modes=api.createCountryModes();modes.connect(ports);Object.assign(ports.countryEditingB,modes);
 const cut=api.createCutGeometry();cut.connect(ports);
 const projection=api.d3.geo.equirectangular().scale(1000).translate([512,384]);
 const pointer=api.createPointerTargets();pointer.connect({...ports,mapView:{activeProjection:()=>projection},platform:{...ports.platform,clamp:(v,min,max)=>Math.max(min,Math.min(max,v))},draftPresentation:cut});pointer.initializeSnapCandidateCache();
 const canonical=()=>JSON.stringify({entities:entityRepository.list(),genericFeatures:state.genericFeatures,hydroEdits:state.hydroEdits,labels:state.labels,distributionLayers:state.distributionLayers,distributionEntries:state.distributionEntries});
 const canonicalBefore=canonical(),sourceRows=()=>[...entityRepository.list().map(f=>'territorial:'+f.id),...state.genericFeatures.map(f=>'generic:'+f.id),...state.hydroEdits.map(f=>'hydro:'+f.id)];
 const record=(name,extra={})=>{phase=name;const row={observed:true,canonical:canonical(),sourceRows:sourceRows(),stateRevision:state.stateRevision,tool:state.tool,worker:clone(client.stats()),...clone(extra)};stages[name]=row;return row;};
 const settleRequests=async()=>{const limit=Date.now()+15000;while(pending.size){if(Date.now()>limit)throw Error('Source-history requests did not settle');await Promise.allSettled([...pending]);}await Promise.resolve();};
 const query=async(name,coordinate=queryCoordinate)=>{phase=name;const offset=requests.length,start=transport.length,first=pointer.localSnapCandidates(coordinate);await settleRequests();const candidates=pointer.localSnapCandidates(coordinate);await settleRequests();const screenPoint=projection(coordinate),result=api.resolveSnap({coordinate,screenPoint,candidates,project:projection,pointerType:'mouse'}),indicator=api.snapIndicator(result);return record(name,{coordinate,screenPoint,pointerType:'mouse',result:clone(result),indicator:clone(indicator),coldCandidates:clone(first),candidates:clone(candidates),winner:indicator?.ownerIds?.[0]||null,requestCount:requests.length-offset,transportStart:start,sameArray:first===candidates});};
 const remove=(id='ga',domain='generic',stageName='removed')=>{phase=stageName;modalOutcome=null;require(objects.requestObjectDeletion([{domain,type:domain==='generic'?'feature':'entity',id}]),'Production deletion refused '+id);require(modalOutcome===true,'Production deletion confirmation failed '+id);return record(stageName,{id,domain});};
 const undo=(name='restored')=>{phase=name;require(snapshots.historyService.undo(),'Production history Undo failed');return record(name,{canonicalEqual:canonical()===canonicalBefore});};
 const enterBoundary=()=>{selection.setMany((definition.selectedIds||['target','donor']).map(id=>({domain:'territorial',type:'entity',id})));require(modes.enterTerritorialBorderEditFromSelection(),'Production shared-boundary entry failed');return state.boundaryPreparation.promise;};
 const localPreview=()=>geometryPreview.beginLocalGeometryPreview({operation:'source-history-preview',beforeFeatures:[entityRepository.get(definition.rootDeletionId||'target')],afterFeatures:[entityRepository.get(definition.rootDeletionId||'target')],applyResult:noop});
 const context={api,loaded,runtime,client,transport,requests,hold,waitHeld,release,record,until,settleRequests,definition,remove,undo,query,canonical};
 try {
  await query('warm');
  if(definition.scenario==='generic-unsynced'){remove();undo();await query('final');}
  else if(['boundary-ready-cancel','boundary-held-result-stop','boundary-error-cancel'].includes(definition.scenario)){
   remove();phase='operation';if(definition.scenario==='boundary-held-result-stop')hold('boundary-prepare');const preparing=enterBoundary();
   if(definition.scenario==='boundary-held-result-stop')await waitHeld('boundary-prepare');else await preparing;
   record('operation',{preparation:{status:state.boundaryPreparation.status,workerPending:state.boundaryPreparation.workerPending}});
   modes.cancelActiveMode(false);record('cancelled');release('boundary-prepare');await preparing;await settleRequests();undo();await query('final');
  } else if(definition.scenario==='stopped-root-delete-undo'){
   remove();phase='operation';hold('boundary-prepare');const preparing=enterBoundary();await waitHeld('boundary-prepare');record('operation',{preparation:{status:state.boundaryPreparation.status,workerPending:state.boundaryPreparation.workerPending}});modes.cancelActiveMode(false);record('cancelled');release('boundary-prepare');await preparing;await settleRequests();remove(definition.rootDeletionId||'target','territorial','rootRemoved');undo('rootRestored');undo();await query('final');
  } else if(definition.scenario==='preview-stop-ready-snap-hit'||definition.scenario==='replacement-after-stop'){
   remove();await query('observeRemoval');undo();await query('reordered');
   hold('territorial-preview');phase='operation';const preparing=localPreview();await waitHeld('territorial-preview');record('operation');geometryPreview.discardActiveGeometryPreview({announce:false});record('cancelled');release('territorial-preview');await preparing;await settleRequests();
   if(definition.scenario==='preview-stop-ready-snap-hit'){
    await query('cached');await query('final',definition.finalQueryCoordinate||[queryCoordinate[0]+1.6,queryCoordinate[1]]);
   }else{
    phase='replacement';generation++;entityStore.restoreProject(api.createStaticTerritorialSnapshot(clone(features)));state.genericFeatures=api.normalizeGenericFeatureCollection(clone(state.genericFeatures));client.rebase();record('replacement');state.stateRevision++;await query('final');
   }
  } else if(definition.scenario==='superseded-result'){
   remove();phase='operation';hold('boundary-prepare');const first=enterBoundary();await waitHeld('boundary-prepare');record('operation');
   state.boundaryEditEntityIds=definition.supersedingIds||['target','donor','parent'];phase='supersede';const second=geometryPreview.rebuildBoundaryTopology(state.boundaryEditEntityIds);release('boundary-prepare');await Promise.all([first,second]);await settleRequests();record('superseded',{preparation:{status:state.boundaryPreparation?.status,workerPending:state.boundaryPreparation?.workerPending}});modes.cancelActiveMode(false);undo();await query('final');
  } else if(['settled-success-observes-late-deletion','cancelled-ticket-skips-post-sync'].includes(definition.scenario)){
   phase='lateRequest';hold('territorial-snap');pointer.localSnapCandidates([queryCoordinate[0]+1.6,queryCoordinate[1]]);await waitHeld('territorial-snap');record('operation',{heldOperation:'territorial-snap',heldResultIsWorkerComplete:true});remove();
   if(definition.scenario==='cancelled-ticket-skips-post-sync')client.cancel();
   phase='releaseResult';release('territorial-snap');await settleRequests();record('resultSettled',{clientStatus:requests.at(-1).status});undo();await query('final');
  } else if(definition.scenario==='selection'){
   if(loaded.selectionRuntime.prepareSourceHistorySelection)await loaded.selectionRuntime.prepareSourceHistorySelection(context,definition);
   remove();await loaded.selectionRuntime.runSourceHistorySelection(context,definition);await settleRequests();undo();await query('final');
  } else throw Error('Unknown source-history scenario '+definition.scenario);
  return {case:definition.id,input,stages,transport,requests,errors,entrypoint:'actual app-object-commands deletion → production workflow/client/Worker → app-project-snapshots Undo → app-pointer-targets',observationLimits:{rawParity:false,queryContextEquivalent:false,selectedIndicatorOnly:true,fullCanonicalProject:false,fullDOM:false,gpuRendering:false,workerExecutionPhase:false,heldResultIsWorkerComplete:true,publicControllerModules:true}};
 } finally {phase='cleanup';geometryPreview.discardActiveGeometryPreview({announce:false});client.stop();for(const operation of [...holds])release(operation);await settleRequests();domains.editingDomain.dispose?.();}
}
