import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {resolve} from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {productionModules,createRuntime,observe,workerFactory,referenceEffects,runWorkerCalculation} from './web-lifecycle.mjs';
import {loadSelectionModules,createSelectionRuntime,observeSelection,square} from './web-selection.mjs';
import {prepareRiverRemovalCorrectedSelectionSources} from './web-selection-removal-correction.mjs';
const clone=value=>structuredClone(value),noop=()=>{};
const fixtureRoot=fileURLToPath(new URL('../../tests/fixtures/web-m973-split/',import.meta.url));
const cuts=JSON.parse(readFileSync(new URL('../../tests/fixtures/web-m97/cut-corpus.json',import.meta.url)));
export const splitCaseIds=Object.freeze([
 'root-two-crossing','root-six-crossing','root-hole','root-multi','root-date-line','root-outer-hole-bridge',
 'root-no-cut','root-tangent','root-boundary-overlap','root-endpoint-inside','root-duplicate-vertex','root-self-intersection',
 'root-multiple-selected','root-untouched-island','root-all-candidates-island','root-all-selected','root-empty-selection',
 'root-with-dependents','root-with-dependent-reference','root-with-source-label','root-with-dependent-label-settings','root-with-dependent-presentation','root-with-dependent-generic-metadata','child-two-crossing','child-multiple-selected','child-all-selected','child-with-dependents'
]);
export async function loadSplitModules(){
 const production=await productionModules(), corrected=prepareRiverRemovalCorrectedSelectionSources();
 try {
  const supplemental=JSON.parse(readFileSync(resolve(fixtureRoot,'source-manifest.json')));
  assert.equal(supplemental.behavioralCommit,production.manifest.behavioralCommit);
  assert.deepEqual(supplemental.sources,[{path:'assets/js/modules/app-object-picking.js',file:'original/app-object-picking.js',blob:'d14bdc2c506118d33d19db19419ded1155dd16ce',sha256:'19e62d26000134b4c7ca695ed21e3a9bbaad176ed3b90b757f59d0b141c13e23'}]);
  for(const source of supplemental.sources){
   const bytes=readFileSync(resolve(fixtureRoot,source.file));
   assert.equal(createHash('sha256').update(bytes).digest('hex'),source.sha256);
   assert.equal(createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`),bytes])).digest('hex'),source.blob);
   writeFileSync(resolve(corrected.root,source.path),bytes);
  }
  assert.equal(typeof globalThis.polygonClipping?.difference,'function','Run this ESM harness as a file or node --test; CommonJS global exports hijacks UMD vendor loading.');
  const api={...production.api,...await loadSelectionModules(corrected.root),
   ...await import(pathToFileURL(resolve(corrected.root,'assets/js/modules/app-territorial-drafts.js'))),
   ...await import(pathToFileURL(resolve(corrected.root,'assets/js/modules/app-country-validation.js'))),
   ...await import(pathToFileURL(resolve(corrected.root,'assets/js/modules/app-object-picking.js')))};
  return {...production,api,selectionRoot:corrected.root,sourceChain:corrected.sourceChain,supplemental,cleanup:corrected.cleanup};
 }catch(error){corrected.cleanup();throw error;}
}
export function splitCaseDefinition(id){
 if(!splitCaseIds.includes(id))throw Error('Unknown split corpus case: '+id);
 const geometryId=id.replace(/^(root|child)-/,'');
 const cut=cuts.find(row=>row.id===geometryId)||cuts.find(row=>row.id===(id.includes('multiple-selected')?'six-crossing':'two-crossing'));
 const definition={id,scope:id.startsWith('root-')?'root':'child',source:clone(cut.payload.source),coords:clone(cut.payload.coords),view:clone(cut.payload.view),select:'default'};
 if(id.includes('all-'))definition.select='all';
 if(id.endsWith('empty-selection'))definition.select=[];
 if(id.includes('multiple-selected'))definition.select=[0,1,2];
 if(id.includes('island'))definition.source={type:'MultiPolygon',coordinates:[...apiPolygons(definition.source),square(20,0,22,2).coordinates]};
 const invalid={
  'no-cut':[[-3,-5],[13,-5]],'tangent':[[-3,-3],[0,0],[-3,3]],
  'boundary-overlap':[[0,0],[0,10]],'endpoint-inside':[[5,5],[13,5]],
  'duplicate-vertex':[[-3,5],[5,5],[5,5],[13,5]],'self-intersection':[[-3,2],[13,8],[-3,8],[13,2]],
 };
 if(invalid[geometryId])definition.coords=invalid[geometryId];
 definition.dependents=id.includes('dependen');definition.dependentReference=id.endsWith('dependent-reference');definition.dependentLabel=id.endsWith('source-label');definition.dependentLabelSettings=id.endsWith('dependent-label-settings');definition.dependentPresentation=id.endsWith('dependent-presentation');definition.dependentGeneric=id.endsWith('dependent-generic-metadata');
 return definition;
}
export function splitReactivationCaseDefinition(){return {...splitCaseDefinition('root-empty-selection'),id:'root-empty-selection-reactivated',reactivateAfterEmpty:true};}
function apiPolygons(geometry){return geometry.type==='Polygon'?[geometry.coordinates]:geometry.coordinates;}
export function seedSplitFeatures(api,definition){
 const make=(id,geometry,parentId='')=>api.createTerritorialFeature({id,name:id,entityKind:'general',parentId,coverageMode:parentId?'partition':'explicit',geometry});
 const features=definition.scope==='root'?[make('source',definition.source)]:[make('parent',definition.parentGeometry||square(-10,-10,30,30)),make('source',definition.source,'parent')];
 if(definition.dependents)features.push(make('child-kept',square(1,1,2,2),'source'),make('child-moved',square(1,7,2,8),'source'),make('child-cross',square(3,3,4,7),'source'),make('grandchild-cross',square(3.1,4,3.2,6),'child-cross'));
 for(const dependent of definition.dependentFeatures||[])features.push(dependent.type==='Feature'?clone(dependent):make(dependent.id,dependent.geometry,dependent.parentId||'source'));
 return features;
}
export function createSplitRuntime(loaded,definition){
 const {api}=loaded, runtime=createRuntime(api), features=seedSplitFeatures(api,definition);
 runtime.entityStore.restoreProject(api.createStaticTerritorialSnapshot(features));
 Object.assign(runtime.state,{labels:[],distributionEntries:[],labelSettings:{},itemVisibility:{},layerPresentation:{schemaVersion:4,styles:{},objectStyles:{},objectOrder:[]}});
 if(definition.dependentReference)runtime.state.distributionEntries=api.normalizeDistributionEntries([{id:'child-reference',schemaVersion:3,layerId:'distribution',mode:'territorial',territorialUnitId:'child-moved',value:1}],{layerExists:()=>true});
 if(definition.dependentLabel)runtime.state.labels=[{id:'child-label',countryId:'source',name:'Source',lat:7.5,lon:1.5}];
 if(definition.dependentLabelSettings)runtime.state.labelSettings={'territorial:child-moved':{visible:false}};
 if(definition.dependentPresentation){runtime.state.itemVisibility.subunits={'child-moved':false};runtime.state.layerPresentation.objectStyles={'territorial:entity:child-moved':{opacity:0.5}};}
 if(definition.dependentGeneric)runtime.state.genericFeatures=[api.normalizeGenericFeatureSemantics({type:'Feature',id:'legacy-owner-metadata',properties:{schemaVersion:1,name:'Imported generic metadata',ownerId:'child-moved'},geometry:{type:'Point',coordinates:[1.5,7.5]}})];
 api.assertProjectReferenceIntegrity({...runtime.state,territorialEntities:runtime.entityRepository.list()});
 runtime.state.historyDirtyEntityIds.clear();
 const client=api.createMapEditWorkerClient({createWorker:loaded.createWorker||workerFactory(loaded.root,loaded.manifest),getEntities:runtime.entityRepository.list,getFeatureById:runtime.entityRepository.get,getTargetRevision:()=>runtime.state.stateRevision});
 runtime.ports.spatialQuery.mapEditClient=client;
 const h=createSelectionRuntime(api,{lifecycle:runtime,features});
 const requests=[], modalDecisions=[];
 h.pendingSplitWorkerRequests=0;
 h.ports.spatialQuery.mapEditClient={...client,async execute(operation,message,options){
  requests.push({operation,payload:clone(message.payload)});h.pendingSplitWorkerRequests+=1;
  try{return await client.execute(operation,message,options);}finally{h.pendingSplitWorkerRequests-=1;}
 }};
 const originalBeginWorker=h.ports.geometryOperations.beginWorkerGeometryPreview, originalBeginLocal=h.ports.geometryOperations.beginLocalGeometryPreview;
 h.ports.geometryOperations={...h.ports.geometryOperations,
  async beginWorkerGeometryPreview(request){h.previews.push({operation:request.operation,payload:clone(request.payload)});return originalBeginWorker(request);},
  async beginLocalGeometryPreview(request){h.previews.push({operation:request.operation,beforeFeatures:clone(request.beforeFeatures),afterFeatures:clone(request.afterFeatures),removedIds:clone(request.removedIds)});return originalBeginLocal(request);}};
 const validation=api.createCountryValidation();validation.connect(h.ports);h.ports.countryValidation={...h.ports.countryValidation,...validation};
 const picking=api.createObjectPicking();picking.connect(h.ports);h.ports.objectPicking=picking;
 h.ports.territorySelectionA={...h.ports.territorySelectionA,startTerritorySelection:h.workflow.start,resetTerritorySelection:h.workflow.resetSelection};
 h.ports.territorySelectionB={territorySelectionBack:h.workflow.back,territorySelectionPreviewIsCurrent:h.workflow.previewIsCurrent};
 h.ports.applicationServicesB={...api,orientRing:api.orientRing,sphericalGeometryAreaKm2:api.geometryAreaKm2};
 const transfer=h.ports.landRelations.transferLandDependents;h.ports.landRelations={...h.ports.landRelations,transferLandDependents(...args){const before=clone(runtime.entityRepository.list());const result=transfer(...args);h.effects.push({landTransfer:{args:clone(args),result:clone(result),before,after:clone(runtime.entityRepository.list())}});return result;}};
 h.ports.platform.$=()=>({select:noop});
 h.ports.projectRestore={openConfirmModal:modal=>{modalDecisions.push({title:modal.title,impacts:clone(modal.impacts),decision:'confirm'});modal.onConfirm();}};
 const drafts=api.createTerritorialDrafts();drafts.connect(h.ports);h.ports.territorialEditingA=drafts;h.ports.territorialEditingB=drafts;
 // Cut is prepared through production RPC/worker, then consumed unchanged by the production draft entrypoint.
 let preparedCut=null;
 h.ports.cutOperations={...h.ports.cutOperations,buildCutSplitCandidates(source,coords){
  assert.ok(preparedCut,'Missing actual worker cut preparation');assert.deepEqual(source,preparedCut.source);assert.deepEqual(coords,preparedCut.coords);
  const result=preparedCut.result;if(!result.valid||!result.split)throw Object.assign(new Error(result.message||result.splitError||'Cut pending'),{cutIssue:result.issues?.[0]});return result.split;
 }};
 const prepareCut=async()=>{const source=h.workflow.activeSession().workingSourceGeometry, coords=h.draft();const response=await client.execute('territorial-cut',{payload:{source,sourceKey:`${definition.id}-cut`,coords,view:definition.view,buildPreview:true}});preparedCut={source:clone(source),coords:clone(coords),result:clone(response.result)};return response.result;};
 const snapshot=(outcome=null)=>({outcome,document:{...observe(runtime),labels:clone(runtime.state.labels),genericFeatures:clone(runtime.state.genericFeatures)},selection:observeSelection(h),errors:clone(h.errors),selected:clone(runtime.state.selected),selectionEffects:clone(runtime.effects.filter(row=>row.name==='selection.applyIntent'))});
 globalThis.requestAnimationFrame ||= callback=>{callback();return 0;};
 // Pinned app-progressive-startup.js:450 rebases afterInitialMapSetup. Reproduce
 // that public ready boundary before invoking a workflow, rather than letting
 // concurrent setup reads independently race through cold readiness polling.
 const prepareWorker=async()=>{
  if(client.stats().ready)return;
  if(!client.stats().workerActive)client.rebase();const deadline=Date.now()+10000;
  while(!client.stats().ready){if(Date.now()>deadline)throw Error('Split worker startup did not become ready');await new Promise(resolve=>setTimeout(resolve,5));}
 };
 return {h,runtime,client,drafts,features,requests,modalDecisions,snapshot,prepareCut,prepareWorker,definition};
}
export async function settleSplit(h){
 const end=Date.now()+10000;
 for(;;){await new Promise(resolve=>setTimeout(resolve,5));const s=h.workflow.activeSession();
  if((!s||(!s.computationPending&&!s.previewPending&&!s.preparation&&!s.workerRequests&&!s.setupSourceCache?.pending))&&!h.pendingSplitWorkerRequests)return;
  if(Date.now()>end)throw Error('Split workflow did not settle');
 }
}
export async function runSplitLifecycleCase(loaded,definition){
 const r=createSplitRuntime(loaded,definition),{h,runtime,client,drafts,snapshot}=r;
 const stages={before:snapshot()};let reactivation=null;
 async function start(){
  await r.prepareWorker();
  assert.equal(drafts.enterTerritorialUnitSplitMode('source'),true);
  if(definition.scope==='root')assert.equal(h.workflow.toggleSourceCountry('source'),true);
  assert.equal(await h.workflow.advance(),true);
  assert.equal(await h.workflow.selectMethod('line'),true);await settleSplit(h);
  h.setDraft(definition.coords);const assessment=await r.prepareCut();
  const finished=await h.workflow.finishDraft();await settleSplit(h);
  const current=h.workflow.activeSession();
  return {assessment,finished,defaultCandidateIndex:current.candidates.findIndex(item=>current.selectedCandidateIds.includes(item.id))};
 }
 async function selectWanted(){
  if(definition.select==='default')return;
  const current=h.workflow.activeSession(), wanted=new Set(definition.select==='all'?current.candidates.map((_,index)=>index):definition.select);
  for(const [index,item]of current.candidates.entries())if(current.selectedCandidateIds.includes(item.id)!==wanted.has(index))h.workflow.selectCandidate(item.id);
  await settleSplit(h);
 }
 async function reactivate(initial,{record=false}={}){
  if(!definition.reactivateAfterEmpty)return;
  assert.ok(Array.isArray(definition.select)&&definition.select.length===0,'Reactivation requires the explicit empty-selection action');
  const current=h.workflow.activeSession();
  assert.deepEqual(current.selectedCandidateIds,[]);assert.equal(current.currentGeometry,null);assert.equal(current.combinedGeometry,null);
  assert.equal(h.workflow.previewReady(),false);assert.equal(h.workflow.canAddPart(),false);
  const advanceOutcome=await h.workflow.advance();assert.equal(advanceOutcome,false);await settleSplit(h);
  if(record)stages.emptied=snapshot(advanceOutcome);
  assert.ok(initial.defaultCandidateIndex>=0,'No actual originally default-selected candidate');
  const candidate=current.candidates[initial.defaultCandidateIndex];assert.ok(candidate,'Original candidate no longer exists');
  const requestOffset=r.requests.length,previewOffset=h.previews.length;
  const toggleOutcome=h.workflow.selectCandidate(candidate.id);assert.equal(toggleOutcome,true);await settleSplit(h);
  const rebuildEvidence={selectionRequests:clone(r.requests.slice(requestOffset).filter(row=>row.operation==='territory-selection')),previewRequests:clone(h.previews.slice(previewOffset))};
  assert.ok(rebuildEvidence.selectionRequests.length>0,'Public reactivation must execute a fresh selection worker calculation');
  assert.ok(rebuildEvidence.previewRequests.length>0,'Public reactivation must prepare a fresh geometry preview');
  assert.equal(h.workflow.previewReady(),true);assert.equal(h.workflow.canAddPart(),true);
  if(record){
   stages.reactivated=snapshot(toggleOutcome);
   reactivation={candidateIndex:initial.defaultCandidateIndex,candidateId:candidate.id,emptiedAction:'advance',reactivatedAction:'selectCandidate',rebuildEvidence,
    inactiveCache:{remainingGeometry:clone(stages.emptied.selection.remainingGeometry),workingSourceGeometry:clone(stages.emptied.selection.workingSourceGeometry),selectedCandidateIds:clone(stages.emptied.selection.selectedCandidateIds),combinedGeometry:clone(stages.emptied.selection.combinedGeometry),previewReady:stages.emptied.selection.previewReady,canAddPart:stages.emptied.selection.canAddPart,canAdvance:advanceOutcome},
    rebuiltBeforeConsumption:{currentGeometry:clone(stages.reactivated.selection.currentGeometry),remainingGeometry:clone(stages.reactivated.selection.remainingGeometry),previewReady:stages.reactivated.selection.previewReady,canAddPart:stages.reactivated.selection.canAddPart}};
  }else{reactivation.confirmationCandidateId=candidate.id;reactivation.confirmationRebuildEvidence=rebuildEvidence;}
 }
 try {
  const first=await start();stages.candidates=snapshot(first.finished);
  await selectWanted();
  await settleSplit(h);await reactivate(first,{record:true});stages.selected=snapshot();
  const archived=h.workflow.addPart();await settleSplit(h);stages.archive=snapshot(archived);
  const reviewed=await h.workflow.advance();stages.review=snapshot(reviewed);
  h.workflow.clear();await settleSplit(h);stages.cancel=snapshot();
  if(archived&&reviewed){const restarted=await start();await selectWanted();await reactivate(restarted);assert.equal(h.workflow.addPart(),true);await settleSplit(h);assert.equal(await h.workflow.advance(),true);stages.confirm=snapshot(await h.workflow.apply());
   if(stages.confirm.outcome){stages.undo=snapshot(runtime.snapshots.historyService.undo());stages.redo=snapshot(runtime.snapshots.historyService.redo());}
  }
  return {case:definition.id,input:clone(definition),entrypoint:'enterTerritorialUnitSplitMode → actual entity selection/draft → production cut/selection/edit worker → production geometry preview/store/history',assessment:first.assessment,stages,requests:r.requests,previews:h.previews,modalDecisions:r.modalDecisions,effects:h.effects,...(reactivation?{reactivation}:{}),...(stages.confirm?{referenceEffects:referenceEffects(stages.before.document.document,stages.confirm.document.document)}:{})};
 }finally{h.workflow.clear();try{await settleSplit(h);}finally{client.stop();}}
}
export async function runSplitLifecycleCorpus({caseIds=splitCaseIds}={}){
 const loaded=await loadSplitModules();
 try{const cases=[];for(const id of caseIds)cases.push(await runSplitLifecycleCase(loaded,splitCaseDefinition(id)));return {schema:'pando-web-split-lifecycle-observations',version:1,captureVersion:2,baseBehavioralCommit:loaded.manifest.behavioralCommit,behavioralCommit:loaded.sourceChain.at(-1).behavioralCommit,sourceChain:loaded.sourceChain,supplementalSources:loaded.supplemental,observationMode:'Node discovery using actual production entity selection and worker/client/store/preview/history. Headless UI ports, not Chromium or pixel evidence.',cases};}finally{loaded.cleanup();}
}
export async function runSplitCalculationCorpus(){const rows=[];for(const row of cuts){const input=clone(row.payload),result=await runWorkerCalculation('territorial-cut',input);assert.deepEqual(input,row.payload);rows.push({id:row.id,payload:input,result});}return rows;}
if(process.argv[1]&&import.meta.url===pathToFileURL(resolve(process.argv[1])).href){
 const selected=process.argv.find(arg=>arg.startsWith('--cases='))?.slice(8).split(',');
 const result=await runSplitLifecycleCorpus(selected?{caseIds:selected}:{});
 const output=process.argv.slice(2).find(arg=>!arg.startsWith('--'));
 if(output){assert.notEqual(resolve(output),resolve(fixtureRoot,'lifecycle-observations.json'),'Original discovery capture is immutable; choose a separate output file.');writeFileSync(output,JSON.stringify(result,null,2)+'\n');}
 else if(process.argv.includes('--summary'))for(const row of result.cases)console.log(JSON.stringify({case:row.case,assessment:{status:row.assessment.status,count:row.assessment.split?.candidates?.length},canAdd:row.stages.selected.selection?.canAddPart,confirm:row.stages.confirm?.outcome,refs:row.referenceEffects,errors:row.stages.confirm?.errors}));
 else process.stdout.write(JSON.stringify(result,null,2)+'\n');
}
