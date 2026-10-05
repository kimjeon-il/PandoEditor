// Exchange-only wiring: unchanged archived production readers/serializer/edit services.
// Existing M972–4 observers and evidence are never modified or relabelled.
import {createSplitRuntime,settleSplit,seedSplitFeatures} from '../m97/web-split.mjs';
import {runBoundaryCase,extractBoundaryCallbacks} from '../m974-snap-boundary/boundary-runtime.mjs';
export async function exchangeRaw(raw){const bytes=new TextEncoder().encode(raw);return {raw,bytes:bytes.length,sha256:[...new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))].map(v=>v.toString(16).padStart(2,'0')).join('')};}
export function wireExchangeHydro(loaded,runtime){
 const source=loaded.evidenceSources['assets/js/modules/app-environment.js'];const begin='    (HYDRO_TOOL_CONFIG = Object.freeze({',end='\n\n    (HYDRO_LAYER_META';const first=source.indexOf(begin),last=source.indexOf(end,first);
 if(first<0||last<=first||source.indexOf(begin,first+1)>=0)throw Error('Hydro constant seam must be unique');
 const text=source.slice(first,last),config=Function('let HYDRO_TOOL_CONFIG;'+text+';return HYDRO_TOOL_CONFIG;')();
 const picker=loaded.api.createColorPicker();picker.connect({colorServices:loaded.api});const hydro=loaded.api.createHydroSettings();hydro.connect({...runtime.ports,objectPresentation:loaded.api,colorModel:picker,hydroPresentation:{HYDRO_TOOL_CONFIG:config},physicalConfig:{PHYSICAL_DATASET:'m975-fixture'}});runtime.ports.hydroModel=hydro;
 return {sourcePath:'assets/js/modules/app-environment.js',byteStart:new TextEncoder().encode(source.slice(0,first)).length,byteEnd:new TextEncoder().encode(source.slice(0,last)).length,source:text};
}
export function installExchangeInput(api,runtime,input){
 const prepared=api.prepareProjectForActivation(input);
 // Restore the exact validated persisted model before any edit entrypoint. Fields
 // are applied with the store's geometry instance retained until restoreProject.
 const {geometries,...fields}=prepared;api.applyProjectFields(runtime.state,{...fields,geometries:runtime.state.geometries},{normalizers:{geometries:value=>value}});
 runtime.entityStore.restoreProject(prepared);runtime.state.historyDirtyEntityIds.clear();runtime.state.history.length=0;runtime.state.future.length=0;
 runtime.exchangeInput=structuredClone(prepared);return prepared;
}
export function exchangeSerializer(api,runtime,{fullAutosave=true,fingerprint='',sourceContext=null}={}){
 const p=runtime.exchangeInput,physical=sourceContext||p.physicalSourceInfo||{};
 return api.createProjectSerializer({appVersion:p.version,baseDataset:p.baseDataset,distributionModes:p.distributionModel.sourceModes,terrainDataset:physical.terrain?.dataset,hydroDataset:physical.hydro?.dataset,now:()=>new Date(p.savedAt),readSnapshot:()=>({territorialEntities:runtime.entityStore.identities(),projectFields:api.pickProjectFields(runtime.state,{clone:value=>value}),entityDelta:runtime.snapshots.buildEntityDelta(),baseDatasetFingerprint:fingerprint,fullAutosave,terrainSourceInfo:physical.terrain,hydroManifest:physical.hydro})});
}
export function exchangeCheckpoint(api,runtime,outcome=null){
 const saved=exchangeSerializer(api,runtime).buildProject(),stored=api.prepareProjectForStorage(saved);api.prepareProjectForActivation(stored);
 return {observed:true,saveRaw:JSON.stringify(saved),reader:{storage:true,activation:true},history:{undo:runtime.state.history.length,redo:runtime.state.future.length},inputInstalled:!!runtime.exchangeInput,outcome};
}
export async function reopenExchange(loaded,raw,{storageOnly=false}={}){
 const {api}=loaded,input=JSON.parse(raw),stored=api.prepareProjectForStorage(input);
 if(storageOnly){let rejected=false;try{api.prepareProjectForActivation(stored);}catch(error){rejected=true;}if(!rejected)throw Error('Rich timeline unexpectedly activated');return {save:await exchangeRaw(JSON.stringify(stored)),reader:{storage:true,activation:false,activationRejected:true}};}
 const runtime=loaded.runtime.createRuntime(api);installExchangeInput(api,runtime,stored);const row=exchangeCheckpoint(api,runtime);return {save:await exchangeRaw(row.saveRaw),reader:row.reader};
}
export async function runExchangeCase(loaded,definition){
 const {api}=loaded,clone=structuredClone,require=(ok,message)=>{if(!ok)throw Error(definition.id+': '+message);},input=JSON.parse(definition.inputRaw),stages={},controls={},trace=[];
 const workerFactory=loaded.createWorker;loaded={...loaded,createWorker:()=>{const real=workerFactory(),adapter={onmessage:null,onerror:null,postMessage(message){trace.push({direction:'request',message:clone(message)});real.postMessage(message);},terminate(){return real.terminate();}};real.onmessage=event=>{trace.push({direction:'response',message:clone(event.data)});adapter.onmessage?.(event);};real.onerror=error=>adapter.onerror?.(error);return adapter;}};
 require((await exchangeRaw(definition.inputRaw)).sha256===definition.sha256,'exact input SHA');
 const finish=async()=>{for(const row of Object.values(stages)){row.save=await exchangeRaw(row.saveRaw);delete row.saveRaw;}return {case:definition.id,input:await exchangeRaw(definition.inputRaw),stages,controls,trace};};
 if(definition.operation==='storage'){
  const row=await reopenExchange(loaded,definition.inputRaw,{storageOnly:true});stages.storage={observed:true,saveRaw:row.save.raw,reader:row.reader};return finish();
 }
 if(definition.operation==='boundary'){
  // runBoundaryCase already has an injectable fixture factory and observer. The
  // first observer is before any edit. Install the new authoritative save there,
  // after old helper setup but before before/preview/commit; never repair a save.
  let installed=false;const base=loaded.runtime;
  const observed={...loaded,runtime:{...base,observe(runtime){if(!installed){controls.hydro=wireExchangeHydro(loaded,runtime);installExchangeInput(api,runtime,input);installed=true;}return {...base.observe(runtime),exchange:exchangeCheckpoint(api,runtime)};}}};
  const result=await runBoundaryCase(observed,clone(definition.definition));
  for(const name of definition.stages){require(result.stages[name]?.observed,'missing actual boundary stage '+name);stages[name]=result.stages[name].state.exchange;stages[name].outcome=result.stages[name].outcome;}
  require(stages.confirm.outcome.ok,'boundary confirmation');controls.boundary={extractionEvidence:result.extractionEvidence,movedOwnerIds:result.movedOwnerIds,referenceEffects:result.referenceEffects};return finish();
 }
 let runtime,h,client,split;
 if(definition.operation==='split'){
  // createSplitRuntime only wires fixture ports; exact persisted input replaces
  // its legacy seed before prepareWorker/start or any operation can execute.
  split=createSplitRuntime(loaded,clone(definition.definition));({runtime,h,client}=split);installExchangeInput(api,runtime,input);
 }else{
  runtime=loaded.runtime.createRuntime(api);installExchangeInput(api,runtime,input);
  client=api.createMapEditWorkerClient({createWorker:loaded.createWorker,getEntities:runtime.entityRepository.list,getFeatureById:runtime.entityRepository.get,getTargetRevision:()=>runtime.state.stateRevision});runtime.ports.spatialQuery.mapEditClient=client;
  if(definition.operation==='annex'){h=loaded.selectionRuntime.createSelectionRuntime(api,{lifecycle:runtime,features:runtime.entityRepository.list()});h.ports.spatialQuery.mapEditClient=client;}
 }
 controls.hydro=wireExchangeHydro(loaded,runtime);
 const snap=(name,outcome=null)=>{stages[name]=exchangeCheckpoint(api,runtime,outcome);};
 const settle=async()=>{const end=Date.now()+15000;for(;;){await new Promise(r=>setTimeout(r,5));const s=h.workflow.activeSession();if(!s||(!s.computationPending&&!s.previewPending&&!s.preparation&&!s.workerRequests&&!s.setupSourceCache?.pending&&!h.pendingSplitWorkerRequests))return;if(Date.now()>end)throw Error('Exchange workflow did not settle');}};
 const ready=async()=>{if(!client.stats().workerActive)client.rebase();const end=Date.now()+15000;while(!client.stats().ready){if(Date.now()>end)throw Error('Actual Worker did not become ready');await new Promise(r=>setTimeout(r,5));}};
 const startAnnex=async()=>{
  await ready();require(h.workflow.start('annex',{targetCountryId:'target',sourceCountryIds:['donor']}),'annex start');require(await h.workflow.advance(),'annex source');require(await h.workflow.selectMethod(definition.full?'components':'polygon'),'annex method');await settle();
  if(definition.full){for(const item of h.components.territoryComponentItems())h.workflow.toggleComponent(item.key);await settle();}
  else{h.setDraft([[-1,0],[4,0],[4,10],[-1,10]]);require(h.workflow.finishDraft(),'annex finish');await settle();require(h.workflow.addPart(),'annex archive');await settle();}
 };
 const startSplit=async()=>{
  await split.prepareWorker();require(split.drafts.enterTerritorialUnitSplitMode('source'),'split start');if(definition.definition.scope==='root')require(h.workflow.toggleSourceCountry('source'),'split source');require(await h.workflow.advance(),'split source advance');require(await h.workflow.selectMethod('line'),'split line');await settle();h.setDraft(definition.definition.coords);await split.prepareCut();require(await h.workflow.finishDraft(),'split finish');await settle();
  const current=h.workflow.activeSession();if(definition.definition.select!=='default'){const wanted=new Set(definition.definition.select==='all'?current.candidates.map((_,i)=>i):definition.definition.select);for(const [i,item]of current.candidates.entries())if(current.selectedCandidateIds.includes(item.id)!==wanted.has(i))h.workflow.selectCandidate(item.id);await settle();}
  require(h.workflow.addPart(),'split archive');await settle();require(await h.workflow.advance(),'split review');
 };
 try{
  snap('before');
  if(definition.operation==='delete'){
   let confirmed=null;const noop=()=>{};runtime.domains.projectDomain.getGeneration=()=>1;runtime.domains.selectionDomain=api.createSelectionDomain({projectDomain:runtime.domains.projectDomain});runtime.domains.selectionUiController=api.createSelectionUiController({selectionDomain:runtime.domains.selectionDomain,resolveRef:api.normalizeObjectRef});runtime.ports.snapshots=runtime.snapshots;runtime.ports.projectSnapshots=runtime.snapshots;runtime.ports.objectModelB={territorialApplicationService:runtime.service};runtime.ports.geometryOperations=runtime.geometryPreview;runtime.ports.countryCommands={bumpLandRevision:noop};runtime.ports.countryValidation={refreshCountryCentroids:noop};runtime.ports.propertyEditingA={distributionLayerById:id=>runtime.state.distributionLayers.find(v=>v.id===id)};runtime.ports.projectRestore={openConfirmModal:modal=>{confirmed=modal.onConfirm();}};
   const land=api.createLandRelations();land.connect(runtime.ports);runtime.ports.landRelations=land;const presentation=api.createObjectPresentation();presentation.connect({...runtime.ports,objectCatalog:api});presentation.initializeObjectPresentationModel();runtime.ports.objectPresentation=presentation;const objects=api.createObjectCommands();objects.connect({...runtime.ports,selectionServices:api});require(objects.requestObjectDeletion([{domain:'generic',type:'feature',id:definition.deleteId}]),'delete request');require(confirmed===true,'delete confirmation');snap('confirm',true);require(runtime.snapshots.historyService.undo(),'delete Undo');snap('undo',true);require(runtime.snapshots.historyService.redo(),'delete Redo');snap('redo',true);return finish();
  }
  const start=definition.operation==='split'?startSplit:startAnnex;
  await start();snap('preview');h.workflow.clear();await settle();snap('cancel');await start();if(definition.operation==='annex')require(await h.workflow.advance(),'annex review');const applied=await h.workflow.apply();
  if(definition.reject){require(!applied,'expected real commit rejection');snap('rejected',false);controls.rejection={errors:clone(h.errors)};}
  else{require(applied,'actual edit confirmation '+JSON.stringify(h.errors));snap('confirm',true);require(runtime.snapshots.historyService.undo(),'actual Undo');snap('undo',true);require(runtime.snapshots.historyService.redo(),'actual Redo');snap('redo',true);}
  if(definition.id==='m975-annex-full-recursive'){
   const baseline=loaded.runtime.createRuntime(api);installExchangeInput(api,baseline,input);const fingerprint=await api.fingerprintProjectBaseline(baseline.entityRepository.list()),serializer=exchangeSerializer(api,runtime,{fullAutosave:false,fingerprint}),delta=serializer.buildAutosave(),options={baseEntities:input.territorialEntities,baseDataset:input.baseDataset,baseDatasetFingerprint:fingerprint};
   const recoveredStorage=api.prepareProjectForStorage(delta,options),recoveredRuntime=loaded.runtime.createRuntime(api);installExchangeInput(api,recoveredRuntime,recoveredStorage);const sourceContext=clone(input.physicalSourceInfo),recovered=exchangeSerializer(api,recoveredRuntime,{sourceContext}).buildProject(),full=exchangeSerializer(api,runtime).buildAutosave();let missing=false,wrong=false;try{api.prepareProjectForStorage(delta);}catch{missing=true;}try{api.prepareProjectForStorage(delta,{...options,baseDatasetFingerprint:'0'.repeat(64)});}catch{wrong=true;}
   require(delta.entityDelta.changed.length&&delta.entityDelta.removedIds.length,'actual nonempty changed + removed delta');require(missing&&wrong,'delta baseline refusal');controls.delta={file:await exchangeRaw(JSON.stringify(delta)),recoveredStorage:await exchangeRaw(JSON.stringify(recoveredStorage)),recovered:await exchangeRaw(JSON.stringify(recovered)),sourceContext,full:await exchangeRaw(JSON.stringify(full)),fullStored:await exchangeRaw(JSON.stringify(api.prepareProjectForStorage(full))),changedIds:delta.entityDelta.changed.map(f=>f.id),removedIds:delta.entityDelta.removedIds,baselineFingerprint:fingerprint,missingBaselineRejected:missing,wrongBaselineRejected:wrong};
  }
  return finish();
 }finally{if(h){h.workflow.clear();await settle();}client.stop();}
}
