import assert from 'node:assert/strict';
import { runInThisContext } from 'node:vm';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL,fileURLToPath } from 'node:url';

export const defaultSelectionSourceRoot=fileURLToPath(new URL('../../tests/fixtures/web-m97/lifecycle-source/',import.meta.url));
export const behavioralCommit = '53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47';
export const selectionEntrypoints = ['app-territory-selection-workflow','app-territory-components','territory-component-plan','app-country-modes','app-country-commits','app-river-candidates','river-territory-partition','annex-geometry','cut-worker-preparation','map-edit-country-commands','map-edit-preview-calculations','map-edit-geometry','app-cut-geometry','app-land-relations'];
const clone = value => structuredClone(value);
const noop = () => {};
// This is the original stage-2 oracle view. Controller replays pass it as
// explicit input so camera-dependent endpoint snapping is compared fairly.
export const selectionCutView = () => ({kind:'flat',scale:1000,translate:[500,500],rotate:[0,0,0],center:[0,0],snapDistance:{mouse:10,touch:18},coarsePointer:false,size:{width:2000,height:2000}});
export const square = (x0,y0,x1,y1) => ({ type:'Polygon', coordinates:[[[x0,y0],[x0,y1],[x1,y1],[x1,y0],[x0,y0]]] });
export async function loadSelectionModules(root) {
  await import(pathToFileURL(resolve(root, 'assets/js/vendor/polygon-clipping.min.js')).href);
  runInThisContext(readFileSync(resolve(root, 'assets/js/vendor/d3.min.js'),'utf8'), {filename:'pinned-d3.min.js'});
  const modules = await Promise.all([...selectionEntrypoints,'territorial-units'].map(name => import(pathToFileURL(resolve(root,`assets/js/modules/${name}.js`)).href)));
  globalThis.window = globalThis;
  return {...Object.assign({}, ...modules), ...globalThis.PandoLabPolygonGeometry, clipper:globalThis.polygonClipping, d3:globalThis.d3};
}
export function seedSelectionFeatures(api, {remote = true, children = false} = {}) {
  const feature = (id, geometry, parentId='', entityKind='general') => api.createTerritorialFeature({id,name:id,entityKind,parentId,coverageMode:parentId?'partition':'explicit',geometry});
  const donor = remote ? {type:'MultiPolygon', coordinates:[square(0,0,10,10).coordinates,square(12,0,14,2).coordinates]} : square(0,0,10,10);
  return [feature('target',square(-5,0,0,10)),feature('donor',donor), ...(children ? [feature('child-left',square(0,0,5,10),'donor'),feature('child-right',square(5,0,10,10),'donor'),feature('independent-region',square(1,1,9,9),'','regional')] : [])];
}
/** All geometry and selection state transitions are production code. The default preview adapter executes the real annex calculator+validator; it never mutates document state. Supply production lifecycle ports to exercise confirmation. */
export function createSelectionRuntime(api, {features=seedSelectionFeatures(api), lifecycle=null, riverFeatures=[], cutView=selectionCutView()}={}) {
  const state = lifecycle?.state || {territorySelectionSession:null, geometryPreview:{session:null},hydroEdits:[],physicalLoadState:{hydro:'ready'}};
  state.hydroEdits ||= []; state.physicalLoadState ||= {hydro:'ready'};
  const repository = lifecycle?.entityRepository || { get:id => features.find(f=>String(f.id)===String(id)),list:() => features };
  const errors=[], requests=[], previews=[], effects=[], cutInputs=[]; let draft=[], uid=0;
  const view=clone(cutView);
  const workflow=api.createTerritorySelectionWorkflow(), components=api.createTerritoryComponents(), modes=api.createCountryModes(), commits=api.createCountryCommits(), rivers=api.createRiverCandidates();
  const plan=api.createTerritoryComponentPlan({clipper:api.clipper,normalize:api.normalizePolygonGeometry});
  const calculator=api.createCountryCommandCalculator(api.clipper);
  const worker={ stop(){effects.push('worker.stop');}, async execute(operation,{payload}) {
    requests.push({operation,payload:clone(payload)});
    const method={'territory-components':'prepare','territory-selection':'selection','territory-slivers':'slivers'}[operation];
    if (!method) throw Error(`Unsupported selection operation ${operation}`);
    return {result:await plan[method](payload)};
  }};
  const defaultGeometryOperations={
    discardActiveGeometryPreview(){state.geometryPreview.session=null;effects.push('preview.discard');return true;},
    async beginWorkerGeometryPreview(request){
      const message={operation:request.operation,...request.payload};
      const before=repository.list();
      const calculated=calculator.calculate(message,new Map(before.filter(f=>f.properties.entityKind==='general'&&!f.properties.parentId).map(f=>[String(f.id),f])));
      const preview=api.calculateCountryPreview(message,calculated.result,before,calculated.afterFeatures,api.clipper);
      previews.push({payload:clone(request.payload),result:clone(calculated.result),preview:clone(preview)});
      state.geometryPreview.session=preview; return true;
    },
    async applyActiveGeometryPreview(){throw Error('Read-only session observation cannot apply');},
  };
  const geometryOperations=lifecycle?.geometryPreview || defaultGeometryOperations;
  const editingDomain={ ...(lifecycle?.domains.editingDomain||{}),setTool:tool=>{state.tool=tool;return true;},startDraft:({coords})=>{draft=clone(coords);},replaceDraftCoordinates:coords=>{draft=clone(coords);},clearDraft:()=>{draft=[];},draftInputActive:()=>draft.length>0,cancelActiveGesture:noop,clearDraftHover:noop,refreshTerritorySelection:noop };
  const domains={...(lifecycle?.domains||{}), editingDomain,projectDomain:{...(lifecycle?.domains.projectDomain||{}),getGeneration:()=>7},renderingDomain:lifecycle?.domains.renderingDomain||{}};
  if(lifecycle) Object.assign(lifecycle.domains,domains);
  const ports={...(lifecycle?.ports||{}), projectState:{state}, territorialModel:{...(lifecycle?.ports.territorialModel||{}),...api,entityRepository:repository},platform:{...(lifecycle?.ports.platform||{}),d3:api.d3,polygonClipping:api.clipper,deepClone:clone},
    geometryModel:api,cutGeometry:{normalizeClippedLandGeometry:value=>api.normalizePolygonGeometry(Array.isArray(value)?{type:'MultiPolygon',coordinates:value}:value)},territoryGeometry:components,
    surfaces:{uid:prefix=>`${prefix}-${++uid}`},domains,taskUi:{setModeBanner:noop,updateModeButtons:noop}, feedback:{setActionStatus:noop,reportOperationError:(error,_message,code)=>errors.push({code,message:error.message})},
    objectPresentation:{territorialEntityName:feature=>feature?.properties?.name||feature?.id||''},objectOperationsB:{requireObjectsUnlocked:refs=>refs.every(ref=>!repository.get(ref.id)?.properties?.locked)},
    territoryComponents:components,territoryComponentUi:{updateTerritoryComponentSelectionFeedback:noop},countryEditingA:{editingDraftCoordinates:()=>draft},countryEditingB:modes,countryEditingC:modes,countryCommitFlow:commits,
    geometryOperations,spatialQuery:{...(lifecycle?.ports.spatialQuery||{}),mapEditClient:worker},countryValidation:{refreshCountryCentroids:noop},
    snapshots:lifecycle?.snapshots||{snapshotEditable:()=>clone(features)},territorialServicesA:api,applicationConstantsA:api,riverCandidates:rivers,
    interactionPresentation:{defaultDraftInstruction:()=>''},territorialEditingA:{},territorialEditingB:{},
    territorySelectionA:{activeTerritorySelectionSession:workflow.activeSession,setTerritorySelectionCandidates:workflow.setCurrentCandidates,clearTerritorySelection:workflow.clear},
    geometryPreview:{geometryPolygonSets:components.geometryMultiCoordinates},
    gisServicesA:{ensureGisRuntime:async()=>true},physicalData:{loadHydroData:async()=>true},
    domainControllers:{...(lifecycle?.ports.domainControllers||{}),gisDomain:{cancelRiverPartition:noop,loadRiverPartitionFeatures:async()=>({features:riverFeatures,diagnostics:{fixture:true}}),computeRiverPartition:async payload=>api.buildRiverTerritoryPartitions({...payload,clipper:api.clipper})}},
  };
  const cut=api.createCutGeometry(); const land=api.createLandRelations();
  const actualCut={applyWorkerCountryPatches:cut.applyWorkerCountryPatches,buildCutSplitCandidates(source,coords){
    const payload={source,coords,buildPreview:true,view};cutInputs.push(clone(payload));
    const result=api.prepareCutInWorker(payload,api,api.d3,api.clipper);
    effects.push({cutAssessment:clone(result)}); if(!result.valid||!result.split) throw Error(result.message||result.splitError||'Cut failed'); return result.split;
  }};
  ports.cutOperations=actualCut;ports.landRelations=land;ports.geometryMutation={setApplyingWorkerResult:noop};
  cut.connect(ports);land.connect(ports);components.connect(ports);modes.connect(ports);commits.connect(ports);rivers.connect(ports);rivers.initializeRiverPartitionGeneration();workflow.connect(ports);workflow.initializeTerritorySelectionWorkflow();
  return {state,api,workflow,components,commits,ports,features,errors,requests,previews,effects,cutInputs,cutView:clone(view),setDraft:coords=>{draft=clone(coords);},draft:()=>clone(draft),lifecycle};
}
export async function settle(h) {
  const end=Date.now()+10000;
  for(;;){
    await new Promise(resolve=>setTimeout(resolve,5));
    const s=h.workflow.activeSession();
    if(h.errors.length) throw Error(JSON.stringify(h.errors));
    if(!s||(!s.computationPending&&!s.previewPending&&!s.preparation&&!s.workerRequests&&s.riverPartitionStatus!=='loading'))return;
    if(Date.now()>end)throw Error('Selection did not settle');
  }
}
export function observeSelection(h){
  const s=h.workflow.activeSession();if(!s)return null;
  const keys=['id','kind','stage','activePhase','activeMethod','requestedMethod','methodChangeConfirmation','targetCountryId','sourceCountryIds','settingsRevision','selectionRevision','sourceRevision','projectGeneration','candidates','selectedCandidateIds','selectedComponentKeys','parts','componentSnapshots','baseSourceFeatures','componentFeatures','currentGeometry','combinedGeometry','baseSourceGeometry','workingSourceGeometry','remainingGeometry','archivedGeometry','useRiverBoundaries','riverPartitionStatus','riverPartitionCandidates','riverPartitionDonorResults','previewReadyKey','computationPending','previewPending','applying'];
  const out=Object.fromEntries(keys.filter(k=>s[k]!==undefined).map(k=>[k,clone(s[k])]));
  out.componentItems=clone(h.components.territoryComponentItems());out.baseComponentItems=clone(h.components.territoryBaseComponentItems());out.previewReady=h.workflow.previewReady();out.canAddPart=h.workflow.canAddPart();out.partCount=h.workflow.partCount();out.previewPayload=clone(h.previews.at(-1)?.payload||null);return out;
}
async function start(h,method){assert.ok(h.workflow.start('annex',{targetCountryId:'target',sourceCountryIds:['donor']}));assert.equal(await h.workflow.advance(),true);assert.equal(await h.workflow.selectMethod(method),true);await settle(h);}
export async function runSelectionCorpus({root}={}) {
 const api=await loadSelectionModules(root);const cases=[];
 {
 const h=createSelectionRuntime(api);await start(h,'polygon');const stages={drawing:observeSelection(h)};h.setDraft([[-2,2],[4,2],[4,8],[-2,8]]);assert.equal(h.workflow.finishDraft(),true);await settle(h);stages.candidate=observeSelection(h);assert.equal(await h.workflow.advance(),false);assert.equal(h.workflow.addPart(),true);await settle(h);stages.archived=observeSelection(h);assert.equal(await h.workflow.advance(),true);stages.review=observeSelection(h);assert.equal(h.workflow.back(),true);stages.back=observeSelection(h);h.workflow.clear();stages.cancel=observeSelection(h);cases.push({case:'polygon-clip-archive-review-cancel',stages,requests:h.requests,previews:h.previews});
 }
 {
 const h=createSelectionRuntime(api,{features:seedSelectionFeatures(api,{remote:false})});await start(h,'line');h.setDraft([[-1,5],[11,5]]);assert.equal(h.workflow.finishDraft(),true);await settle(h);const stages={initial:observeSelection(h)};const ids=h.workflow.activeSession().candidates.map(c=>c.id);assert.equal(ids.length,2);h.workflow.selectCandidate(ids[1]);await settle(h);stages.both=observeSelection(h);h.workflow.selectCandidate(ids[0]);await settle(h);stages.secondOnly=observeSelection(h);h.workflow.selectCandidate(ids[0]);await settle(h);stages.reselectedOrder=observeSelection(h);h.workflow.undoPart();await settle(h);stages.undoSelection=observeSelection(h);h.workflow.selectCandidate(ids[1]);await settle(h);stages.deselected=observeSelection(h);h.workflow.clear();cases.push({case:'line-two-crossing-candidate-toggle-order',stages,effects:h.effects.filter(x=>typeof x==='object'),requests:h.requests,previews:h.previews});
 }
 {
 const h=createSelectionRuntime(api);await start(h,'components');const keys=h.components.territoryComponentItems().map(x=>x.key);const stages={initial:observeSelection(h)};h.workflow.toggleComponent(keys[1]);await settle(h);h.workflow.toggleComponent(keys[0]);await settle(h);stages.selectedReverseClickOrder=observeSelection(h);h.workflow.toggleComponent(keys[1]);await settle(h);stages.deselectedOne=observeSelection(h);h.workflow.toggleComponent(keys[1]);await settle(h);assert.equal(h.workflow.addPart(),true);await settle(h);stages.archivedTwoParts=observeSelection(h);const first=h.workflow.activeSession().parts[0].id;h.workflow.removePart(first);await settle(h);stages.removeFirst=observeSelection(h);h.workflow.undoPart();await settle(h);stages.undoLast=observeSelection(h);h.workflow.clear();cases.push({case:'components-union-toggle-deselect-archive-remove-undo',stages,requests:h.requests,previews:h.previews});
 }
 {
 const h=createSelectionRuntime(api);await start(h,'polygon');h.setDraft([[0,0],[2,0],[2,2],[0,2]]);h.workflow.finishDraft();await settle(h);const stages={candidate:observeSelection(h)};assert.equal(await h.workflow.selectMethod('line'),false);stages.confirmation=observeSelection(h);h.workflow.cancelMethodChange();stages.kept=observeSelection(h);await h.workflow.selectMethod('line');assert.equal(await h.workflow.confirmMethodChange(),true);await settle(h);stages.switched=observeSelection(h);h.workflow.clear();stages.cancel=observeSelection(h);cases.push({case:'method-switch-confirm-cancel',stages,requests:h.requests});
 }
 {
 const riverFeatures=[{type:'Feature',id:'fixture-river',properties:{pandolab_id:'fixture-river',category:'river'},geometry:{type:'LineString',coordinates:[[5,-1],[5,11]]}}];
 const h=createSelectionRuntime(api,{riverFeatures});await start(h,'components');h.workflow.toggleRiverBoundaries(true);await settle(h);const stages={partitioned:observeSelection(h)};const keys=h.components.territoryComponentItems().filter(i=>i.usesRiverBoundary).map(i=>i.key);assert.equal(keys.length,2);h.workflow.toggleComponent(keys[0]);await settle(h);stages.selected=observeSelection(h);assert.equal(h.workflow.addPart(),true);await settle(h);stages.archived=observeSelection(h);assert.ok(h.previews.at(-1).payload.riverSliverContext.length);h.workflow.removePart(h.workflow.activeSession().parts[0].id);await settle(h);stages.removed=observeSelection(h);h.workflow.clear();cases.push({case:'river-partition-provenance-snapshot-slivers',input:{riverFeatures},stages,requests:h.requests,previews:h.previews});
 }
 {
 const h=createSelectionRuntime(api);await start(h,'components');h.workflow.toggleComponent(h.components.territoryComponentItems()[1].key);await settle(h);const stages={componentSelected:observeSelection(h)};assert.equal(await h.workflow.selectMethod('polygon'),true);await settle(h);stages.automaticallyArchived=observeSelection(h);h.setDraft([[0,0],[2,0],[2,2],[0,2]]);assert.equal(h.workflow.finishDraft(),true);await settle(h);assert.equal(h.workflow.addPart(),true);await settle(h);stages.mixedParts=observeSelection(h);h.workflow.removePart(h.workflow.activeSession().parts[0].id);await settle(h);stages.componentPartRemoved=observeSelection(h);h.workflow.undoPart();await settle(h);stages.empty=observeSelection(h);h.workflow.clear();cases.push({case:'components-method-switch-autoarchive-mixed-parts',stages,requests:h.requests,previews:h.previews});
 }
 {
 const h=createSelectionRuntime(api,{features:seedSelectionFeatures(api,{remote:false})});await start(h,'polygon');h.setDraft([[-1,-1],[11,-1],[11,11],[-1,11]]);h.workflow.finishDraft();await settle(h);const stages={fullCandidate:observeSelection(h)};assert.equal(h.workflow.canAddPart(),false);assert.equal(await h.workflow.advance(),false);stages.blockedAdvance=observeSelection(h);h.workflow.clear();cases.push({case:'full-donor-polygon-candidate-blocked',stages});
 }
 return {schema:'pando-web-selection-observations',version:1,behavioralCommit,observationMode:'Unmodified production workflow, country modes/draft/preview request preparation, component plan, clipper, cut worker and river preparation. Read-only preview adapter uses actual root annex calculator and validation; no apply/history claim in session corpus.',cases};
}
if(process.argv[1]&&import.meta.url===pathToFileURL(resolve(process.argv[1])).href){const value=await runSelectionCorpus({root:process.env.WEB_SELECTION_ROOT||defaultSelectionSourceRoot});if(process.argv[2]){writeFileSync(process.argv[2],JSON.stringify(value,null,2)+'\n');console.log(`Observed ${value.cases.length} selection cases`);}else process.stdout.write(JSON.stringify(value,null,2)+'\n');}
