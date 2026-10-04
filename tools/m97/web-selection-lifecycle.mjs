import assert from 'node:assert/strict';
import { writeFileSync,readFileSync,readdirSync,mkdirSync } from 'node:fs';
import { resolve,dirname } from 'node:path';
import { pathToFileURL,fileURLToPath } from 'node:url';
import { loadSelectionModules,createSelectionRuntime,seedSelectionFeatures,square,settle,observeSelection,behavioralCommit,defaultSelectionSourceRoot } from './web-selection.mjs';
// Integration: export these existing harness helpers from tools/m97/web-lifecycle.mjs.
const { productionModules,createRuntime,observe,workerFactory,referenceEffects } = await import(process.env.WEB_LIFECYCLE_MODULE || new URL('./web-lifecycle.mjs',import.meta.url));
const clone=value=>structuredClone(value);
export const rootSelectionCaseIds = Object.freeze(['root-partial','root-full','root-with-children-partial','root-with-children-full','root-with-distribution-full','root-with-child-distribution-full','root-with-label-full','root-with-label-settings-full','root-selected-remote-donor']);
const rootObservationIndex = new URL('../../tests/fixtures/web-m97/root-lifecycle-observations.json',import.meta.url);
const rootCaseDirectory = 'root-lifecycle-observations';

/** Assemble the bounded corpus without tolerating missing, duplicate, extra or renamed cases. */
export function readRootSelectionLifecycleObservations(indexPath=rootObservationIndex) {
  const indexFile=indexPath instanceof URL?fileURLToPath(indexPath):resolve(indexPath);
  const {caseFiles,...metadata}=JSON.parse(readFileSync(indexFile));
  assert.equal(metadata.schema,'pando-web-root-selection-lifecycle-observations');
  assert.equal(metadata.version,1);
  assert.equal(metadata.behavioralCommit,behavioralCommit);
  assert.equal(Object.hasOwn(metadata,'cases'),false,'split index must not contain inline cases');
  assert.ok(Array.isArray(caseFiles),'caseFiles must be an array');
  assert.deepEqual(caseFiles.map(row=>row.case),rootSelectionCaseIds,'missing, duplicate, extra or reordered case IDs');
  const wanted=[];
  for(const row of caseFiles) {
    assert.deepEqual(Object.keys(row).sort(),['case','path'],'unexpected case reference fields');
    assert.equal(row.path,`${rootCaseDirectory}/${row.case}.json`,'case path must match its exact case ID');
    wanted.push(`${row.case}.json`);
  }
  const directory=resolve(dirname(indexFile),rootCaseDirectory);
  const entries=readdirSync(directory,{withFileTypes:true});
  assert.ok(entries.every(entry=>entry.isFile()),'case directory must contain only case files');
  assert.deepEqual(entries.map(entry=>entry.name).sort(),wanted.sort(),'missing or extra case files');
  const cases=caseFiles.map(row=>{
    const observation=JSON.parse(readFileSync(resolve(dirname(indexFile),row.path)));
    assert.equal(observation.case,row.case,'case file identity must match index');
    return observation;
  });
  return {...metadata,cases};
}

/** Storage-only serialization: every parsed observation value stays identical. */
export function writeRootSelectionLifecycleObservations(corpus,indexPath=rootObservationIndex) {
  assert.deepEqual(corpus.cases.map(row=>row.case),rootSelectionCaseIds);
  const indexFile=indexPath instanceof URL?fileURLToPath(indexPath):resolve(indexPath);
  const directory=resolve(dirname(indexFile),rootCaseDirectory);
  mkdirSync(directory,{recursive:true});
  const {cases,...metadata}=corpus;
  const caseFiles=cases.map(row=>{
    const path=`${rootCaseDirectory}/${row.case}.json`;
    writeFileSync(resolve(dirname(indexFile),path),JSON.stringify(row)+'\n');
    return {case:row.case,path};
  });
  writeFileSync(indexFile,JSON.stringify({...metadata,caseFiles},null,2)+'\n');
  assert.deepEqual(readRootSelectionLifecycleObservations(indexFile),corpus,'split serialization must preserve every observation value');
}

export async function runRootSelectionLifecycleCorpus({selectionRoot, caseNames=null, ...options}={}){
 const loaded=await productionModules(options);const api={...loaded.api,...await loadSelectionModules(selectionRoot)}; const cases=[];
 for(const name of caseNames || rootSelectionCaseIds){
  const runtime=createRuntime(api);const children=name.includes('children')||name.includes('child-distribution');
  const features=seedSelectionFeatures(api,{remote:false,children});
  if(name.includes('remote-donor'))features.push(api.createTerritorialFeature({id:'remote-donor',name:'remote-donor',entityKind:'general',geometry:square(30,0,32,2)}));
  runtime.entityStore.restoreProject(api.createStaticTerritorialSnapshot(features));
  Object.assign(runtime.state,{distributionEntries:[],labelSettings:{},itemVisibility:{},layerPresentation:{schemaVersion:4,styles:{},objectStyles:{},objectOrder:[]}});
  if(name.includes('distribution'))runtime.state.distributionEntries=api.normalizeDistributionEntries([{id:'donor-reference',schemaVersion:3,layerId:'distribution',mode:'territorial',territorialUnitId:name.includes('child-distribution')?'child-left':'donor',value:1}],{layerExists:()=>true});
  if(name==='root-with-label-full') runtime.state.labels=[{id:'donor-label',countryId:'donor',name:'Donor label',lat:5,lon:5}];
  if(name==='root-with-label-settings-full') runtime.state.labelSettings={'territorial:donor':{visible:false}};
  api.assertProjectReferenceIntegrity({...runtime.state,territorialEntities:runtime.entityRepository.list()});
  runtime.state.historyDirtyEntityIds.clear();
  const client=api.createMapEditWorkerClient({createWorker:workerFactory(loaded.root,loaded.manifest),getEntities:runtime.entityRepository.list,getFeatureById:runtime.entityRepository.get,getTargetRevision:()=>runtime.state.stateRevision});
  runtime.ports.spatialQuery.mapEditClient=client;
  const h=createSelectionRuntime(api,{lifecycle:runtime,features});
  const full=name.endsWith('full');
  const method=full?'components':name==='root-full-line'?'line':'polygon';
  const input={features:clone(features),targetId:'target',sourceCountryIds:name.includes('remote-donor')?['remote-donor','donor']:['donor'],method,draftCoordinates:method==='components'?[]:name==='root-full-polygon'?[[-1,-1],[11,-1],[11,11],[-1,11]]:method==='line'?[[-1,5],[11,5]]:[[-1,0],[4,0],[4,10],[-1,10]],selectedComponentKeys:[]};
  const originalBegin=h.ports.geometryOperations.beginWorkerGeometryPreview;
  h.ports.geometryOperations={...h.ports.geometryOperations};
  // Factory accessors are enumerable. Observation records the production request and delegates intact.
  h.ports.geometryOperations.beginWorkerGeometryPreview=async request=>{h.previews.push({payload:clone(request.payload)});return originalBegin(request);};
  const snap=(outcome=null)=>{const document=observe(runtime);document.document.labels=clone(runtime.state.labels);return {outcome,document,selection:observeSelection(h),errors:clone(h.errors)};};
  const start=async()=>{
   assert.ok(h.workflow.start('annex',{targetCountryId:'target',sourceCountryIds:input.sourceCountryIds}));
   assert.equal(await h.workflow.advance(),true);
   assert.equal(await h.workflow.selectMethod(method),true);await settle(h);
   if(full){input.selectedComponentKeys=h.components.territoryComponentItems().map(item=>item.key);for(const key of input.selectedComponentKeys)h.workflow.toggleComponent(key);await settle(h);}
   else{h.setDraft(input.draftCoordinates);assert.equal(h.workflow.finishDraft(),true);await settle(h);if(method==='line'){for(const candidate of h.workflow.activeSession().candidates.filter(c=>!h.workflow.activeSession().selectedCandidateIds.includes(c.id)))h.workflow.selectCandidate(candidate.id);await settle(h);}assert.equal(h.workflow.addPart(),true);await settle(h);}
  };
  try{
   const stages={before:snap()};await start();stages.preview=snap();h.workflow.clear();stages.cancel=snap();await start();assert.equal(await h.workflow.advance(),true);stages.review=snap();
   const applied=await h.workflow.apply();stages.confirm=snap({ok:applied});
   if(applied){stages.undo=snap({ok:runtime.snapshots.historyService.undo()});stages.redo=snap({ok:runtime.snapshots.historyService.redo()});}
   else {assert.deepEqual(stages.confirm.document.document,stages.before.document.document);assert.equal(runtime.state.history.length,0);}
   cases.push({case:name,input,entrypoint:'app-territory-selection-workflow → app-country-commits.prepareAnnexSelectionPreview → map-edit worker client/RPC/worker → app-geometry-preview → app-cut-geometry.applyWorkerCountryPatches + app-land-relations.transferLandDependents → reference assertion + project snapshots/history',stages,previews:h.previews,requests:h.requests,referenceEffects:referenceEffects(stages.before.document.document,stages.confirm.document.document)});
  }finally{h.workflow.clear();client.stop();}
 }
 return {schema:'pando-web-root-selection-lifecycle-observations',version:1,behavioralCommit,observationMode:'Actual production root annex workflow, worker client/RPC/worker, store transaction, dependent transfer, reference validation, cancel, confirm, history Undo/Redo. Headless presentation and deterministic UID ports; hydro/network/browser gestures are not observed.',cases};
}
if(process.argv[1]&&import.meta.url===pathToFileURL(resolve(process.argv[1])).href){const value=await runRootSelectionLifecycleCorpus({selectionRoot:process.env.WEB_SELECTION_ROOT||defaultSelectionSourceRoot});if(process.argv[2]){writeRootSelectionLifecycleObservations(value,process.argv[2]);console.log(`Observed ${value.cases.length} root lifecycle cases`);}else process.stdout.write(JSON.stringify(value,null,2)+'\n');}
