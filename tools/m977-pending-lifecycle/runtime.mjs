import {installExchangeInput,exchangeCheckpoint,wireExchangeHydro} from '../m975-model-exchange/runtime.mjs';
// Public-handler lifecycle extension. Production algorithms, draft ownership,
// preview, serialization and project history come from the verified source closure.
export async function runPendingLifecycleCase(loaded,definition,corpus){
 const {api}=loaded,clone=structuredClone,require=(v,m)=>{if(!v)throw Error(definition.id+': '+m);};
 const life=loaded.runtime.createRuntime(api);installExchangeInput(api,life,JSON.parse(loaded.bundle.fixtureRaw));
 wireExchangeHydro(loaded,life);life.state.spacePanActive=false;
 const stages={},inputs=[],transport=[];let phase='before',workerId=0,hold=false;const held=[];
 const event=(kind,value={})=>transport.push({sequence:transport.length,phase,kind,...clone(value)});
 const createWorker=()=>{
  const actual=loaded.createWorker(),id=++workerId,operations=new Map();let terminated=false;
  const adapter={onmessage:null,onerror:null,postMessage(message){if(message.type==='execute')operations.set(message.requestId,message.operation);event('request',{worker:id,message});actual.postMessage(message);},terminate(){terminated=true;event('terminate',{worker:id});return actual.terminate();}};
  actual.onmessage=e=>{const operation=operations.get(e.data.requestId)||null;event('response',{worker:id,operation,message:e.data});
   if(hold&&operation==='territory-components'&&e.data.type==='result'){held.push({id,adapter,e});event('held',{worker:id,requestId:e.data.requestId});}
   else{event('delivered',{worker:id,terminated,message:e.data});adapter.onmessage?.(e);}};
  actual.onerror=e=>adapter.onerror?.(e);return adapter;
 };
 // Reuse the source-history production spatial wiring: revision changes and
 // worker synchronization are real, not the older lifecycle wrapper's no-op.
 const noop=()=>{};Object.assign(life.state,{pendingCountryRenderIds:new Set(),spatialIndex:[],boundaryEditEntityIds:[],coastEditCountryId:null});
 life.geometryPreview.initializeBoundarySelectionAnalysisCache();
 Object.assign(life.ports,{geometryPreview:life.geometryPreview,labels:{countryOutlineCache:new WeakMap()},territoryGeometry:{ringHitTester:{invalidate:noop}},countryCommands:{bumpLandRevision:()=>{life.ports.countries.countryLandRevision++;}}});
 life.ports.countries.countryLandRevision=0;life.ports.rendering.gpuMapRenderer.applyCountryPatch=noop;
 const spatial=api.createSpatialIndex();spatial.connect({...life.ports,spatialFactories:{createMapEditWorkerClient:options=>api.createMapEditWorkerClient({...options,createWorker})},platform:{...life.ports.platform,runtimeAssetUrl:x=>x},cutGeometry:{coordinateBounds:api.coordinateBounds}});
 spatial.initializeGeometryBoundsCache();spatial.initializeApplyingMapEditWorkerResult();const client=spatial.mapEditClient;
 life.ports.spatialQuery.mapEditClient=client;life.ports.spatialQuery.markCountryGeometriesChanged=spatial.markCountryGeometriesChanged;
 const h=loaded.selectionRuntime.createSelectionRuntime(api,{lifecycle:life,features:life.entityRepository.list(),cutView:definition.view});h.ports.spatialQuery.mapEditClient=client;h.ports.geometryMutation.setApplyingWorkerResult=value=>{spatial.applyingMapEditWorkerResult=value;};
 const view=definition.view,projection=api.d3.geo.equirectangular().scale(view.scale).translate(view.translate).rotate(view.rotate).center(view.center);
 const editing=api.createEditingDomain({projectDomain:h.ports.domains.projectDomain,toolController:{applyToolPresentation:t=>{h.state.tool=t;}},draftServices:{
  getToolConfig:t=>{if(h.state.geometryPreview.session)return null;const config=api.toolDraftDefinition(t,h.state);return config?{...config,minimumPoints:config.shape==='polygon'?3:2}:null;},
  screenToCoordinate:p=>projection.invert(p),projectCoordinate:projection,snapCandidates:()=>[]}});
 h.ports.domains.editingDomain=editing;life.domains.editingDomain=editing;
 h.ports.countryEditingA.editingDraftCoordinates=()=>editing.snapshot().draft.coords;h.ports.draftPresentation={editingDraftSnapshot:()=>editing.snapshot().draft};
 h.ports.mapView={screenToGeo:p=>projection.invert(p),activeProjection:()=>projection};h.ports.platform.d3={...api.d3,event:{pointerType:definition.profile.pointerType}};
 h.ports.territorySelectionB={territorySelectionCountryPickingActive:h.workflow.countryPickingActive,territorySelectionCountryInstruction:h.workflow.sourceCountryInstruction};
 const picking=api.createObjectPicking();picking.connect(h.ports);
 const record=(name,outcome=null)=>{
  phase=name;require(definition.stages.includes(name)&&!Object.hasOwn(stages,name),'declared unique stage');const s=h.workflow.activeSession(),draft=editing.snapshot().draft,checkpoint=exchangeCheckpoint(api,life);
  stages[name]={observed:true,outcome,canonical:checkpoint.saveRaw,history:checkpoint.history,revision:life.state.stateRevision,stage:s?.stage||null,phase:s?.activePhase||null,method:s?.activeMethod||null,
   pending:!!s?.preparation||(s?.stage==='selection'&&s?.activePhase==='preparing'),preparationActive:!!s?.preparation,draftInputActive:editing.draftInputActive(),coordinates:clone(draft.coords),draftUndo:draft.historyCount,draftRedo:draft.futureCount,
   previewReady:h.workflow.previewReady(),preview:clone(h.state.geometryPreview.session),session:s?clone(Object.fromEntries(['id','stage','activePhase','activeMethod','parts','candidates','selectedCandidateIds','computationPending','previewPending','workerRequests','computationEpoch','sourceRevision','selectionRevision'].filter(k=>s[k]!==undefined).map(k=>[k,s[k]]))):null};
 };
 const until=async predicate=>{const end=Date.now()+15000;while(!predicate()){require(!h.errors.length,JSON.stringify(h.errors));require(Date.now()<end,'real lifecycle operation timed out');await new Promise(r=>setTimeout(r,1));}};
 const settle=async()=>{await until(()=>{const s=h.workflow.activeSession();return !s||(!s.computationPending&&!s.previewPending&&!s.preparation&&!s.workerRequests&&!s.setupSourceCache?.pending);});};
 const release=()=>{hold=false;for(const x of held.splice(0)){event('released',{worker:x.id,requestId:x.e.data.requestId});event('delivered',{worker:x.id,terminated:false,message:x.e.data});x.adapter.onmessage?.(x.e);}};
 const tap=async(point,name)=>{phase=name;const screen=projection(point),s=h.workflow.activeSession();inputs.push({stage:name,coordinate:clone(point),screen:clone(screen),roundTrip:clone(projection.invert(screen)),pointerType:definition.profile.pointerType,prePending:!!s?.preparation||(s?.stage==='selection'&&s?.activePhase==='preparing')});const result=await picking.handleMapClick(screen);record(name,result??null);};
 const cycle=async prefix=>{
  const name=s=>prefix?prefix+s[0].toUpperCase()+s.slice(1):s;
  require(h.workflow.start('annex',{targetCountryId:'target',sourceCountryIds:['donor']}),'start annex');require(await h.workflow.advance(),'advance');
  hold=true;phase=name('activation');const activation=h.workflow.selectMethod('polygon');record(name('activation'));
  await tap(corpus.pendingPoints[0],name('firstPending'));await tap(corpus.pendingPoints[1],name('secondPending'));
  await until(()=>held.length>0);record(name('held'));release();require(await activation===true,'ready activation');await settle();record(name('ready'));
  for(const [i,p]of corpus.readyPoints.entries())await tap(p,name(['firstReady','secondReady','thirdReady','fourthReady'][i]));
  phase=name('finished');require(h.workflow.finishDraft(),'actual Finish');await settle();record(name('finished'),true);
  phase=name('preview');require(h.workflow.addPart(),'archive actual candidate');await settle();record(name('preview'),true);require(h.workflow.previewReady(),'actual ready preview');
  require(await h.workflow.advance(),'actual review');record(name('review'),true);
 };
 try{
  record('before');await cycle('');phase='cancel';h.workflow.clear();await settle();record('cancel');
  await cycle('retry');phase='applied';require(await h.workflow.apply(),'actual Apply '+JSON.stringify(h.errors));await settle();record('applied',true);
  phase='undo';require(life.snapshots.historyService.undo(),'actual project Undo');record('undo',true);
  phase='redo';require(life.snapshots.historyService.redo(),'actual project Redo');record('redo',true);
  return {case:definition.id,input:clone(definition),stages,inputs,transport,errors:clone(h.errors)};
 }finally{phase='cleanup';h.workflow.clear();release();client.stop();editing.dispose();}
}
