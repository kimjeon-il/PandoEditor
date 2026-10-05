import assert from 'node:assert/strict';
import {isDeepStrictEqual} from 'node:util';
import {spawnSync} from 'node:child_process';
import {readFileSync,writeFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {compareCalculation} from './native-baseline.mjs';
import {loadSelectionModules,createSelectionRuntime,seedSelectionFeatures,settle,observeSelection,defaultSelectionSourceRoot,behavioralCommit,selectionCutView} from './web-selection.mjs';
import {prepareCorrectedSelectionSources} from './web-selection-correction.mjs';
import {productionModules,createRuntime,observe,workerFactory} from './web-lifecycle.mjs';

export const nativeSelectionScope=Object.freeze({entrypoint:'EditorController',
  observed:['public-selection-state','actual-SVG-overlays','timer-and-worker-transitions','canonical-document-bytes','strict-Apply','Undo','Redo'],
  unobserved:['native-internal-selection-caches','native-exact-history-depth','river-provenance'],
  deferred:['real-river-partition','complex-cut-graphs']});
const clone=value=>structuredClone(value);
const expectations=()=>JSON.parse(readFileSync(new URL('../../tests/fixtures/web-m97/native-controller-expectations.json',import.meta.url)));

const arrays=['sourceCountryIds','candidates','selectedCandidateIds','selectedComponentKeys','components','parts'];
function checkState(value) {
  if(value===null)return;
  if(!value||typeof value!=='object'||!['setup','selection','review'].includes(value.stage)||typeof value.previewReady!=='boolean'||typeof value.canAddPart!=='boolean')throw Error('invalid selection observation');
  for(const key of arrays)if(!Array.isArray(value[key]))throw Error(`invalid observation ${key}`);
  for(const key of ['candidates','components','parts']) {
    const ids=new Set();for(const item of value[key]){const id=item.id??item.key;if(typeof id!=='string'||!id||ids.has(id))throw Error(`duplicate or invalid ${key} identity`);ids.add(id);if(!item.geometry)throw Error(`missing ${key} geometry`);}
  }
  for(const id of value.selectedCandidateIds)if(!value.candidates.some(c=>c.id===id))throw Error('unknown selected candidate identity');
  for(const key of ['selectedCandidateIds','selectedComponentKeys','sourceCountryIds'])if(new Set(value[key]).size!==value[key].length||value[key].some(id=>typeof id!=='string'))throw Error(`invalid ${key} identity`);
}
export function compareSelectionStates(web,native,clipper) {
  checkState(web);checkState(native);
  if(web===null||native===null)return web===native?[]:[{field:'active',web:web!==null,native:native!==null}];
  const differences=[];const difference=(field,a,b)=>{if(!isDeepStrictEqual(a,b))differences.push({field,web:a,native:b});};
  for(const key of ['stage','activeMethod','activePhase','sourceCountryIds','selectedCandidateIds','selectedComponentKeys','previewReady','canAddPart'])difference(key,web[key],native[key]);
  for(const key of ['candidates','components','parts']) {
    difference(`${key}.length`,web[key].length,native[key].length);
    for(let i=0;i<Math.min(web[key].length,native[key].length);++i){
      const a=web[key][i],b=native[key][i];for(const field of ['id','key','method'])if(Object.hasOwn(a,field)||Object.hasOwn(b,field))difference(`${key}.${i}.${field}`,a[field]??null,b[field]??null);
      const row=g=>({id:'observed-geometry',properties:{parentId:''},geometry:g});
      for(const d of compareCalculation({ok:true,afterFeatures:[row(a.geometry)]},{ok:true,features:[row(b.geometry)]},clipper))differences.push({field:`${key}.${i}.geometry`,...Object.fromEntries(Object.entries(d).filter(([key])=>!['field','id'].includes(key)))});
      // Only line candidates expose planar area in the web workflow. Polygon
      // candidate area and geographic component area are different UI measures.
      if(web.activeMethod==='line'&&key==='candidates')difference(`${key}.${i}.area`,a.area,b.area);
    }
  }
  return differences;
}
function idNormalizer() {
  const parts=new Map();
  return {candidate:id=>{const match=/^(?:territory-candidates-\d+|candidate):(\d+)$/.exec(id);if(!match)throw Error(`unexpected generated candidate identity ${id}`);return `candidate:${match[1]}`;},
    part:id=>{if(!/^(?:territory-part-\d+|part:\d+)$/.test(id))throw Error(`unexpected generated part identity ${id}`);if(!parts.has(id))parts.set(id,`part:${parts.size+1}`);return parts.get(id);}};
}
function normalizeWeb(selection,ids) {
  if(!selection)return null;
  for(const id of selection.selectedCandidateIds)assert.ok(selection.candidates.some(row=>row.id===id),'web selected candidate belongs to a stale generated batch');
  return {stage:selection.stage,activeMethod:selection.activeMethod??null,activePhase:selection.activePhase??null,sourceCountryIds:selection.sourceCountryIds,
    candidates:selection.candidates.map(row=>({...row,id:ids.candidate(row.id)})),selectedCandidateIds:selection.selectedCandidateIds.map(ids.candidate),selectedComponentKeys:selection.selectedComponentKeys,
    components:selection.stage==='selection'&&selection.activePhase==='components'?selection.componentItems.map(({key,geometry})=>({key,geometry})):[],
    parts:selection.parts.map(({id,method,geometry})=>({id:ids.part(id),method,geometry})),previewReady:selection.previewReady,canAddPart:selection.canAddPart};
}
function normalizeNative(observation,ids,geometryCache) {
  const state=observation.state;if(!state.active)return null;
  for(const id of state.selectedCandidateIds)assert.ok(state.candidates.some(row=>row.id===id),'native selected candidate belongs to a stale generated batch');
  const byKey=new Map();for(const overlay of observation.overlays){const key=`${overlay.kind}:${overlay.id}`;if(!byKey.has(key))byKey.set(key,{type:'MultiPolygon',coordinates:[]});byKey.get(key).coordinates.push(...overlay.geometry.coordinates);}
  for(const [key,geometry]of byKey)geometryCache.set(key,geometry);
  const geometry=(kind,id)=>{const result=geometryCache.get(`${kind}:${id}`);if(!result)throw Error(`missing observed ${kind} geometry ${id}`);return result;};
  return {stage:state.stage,activeMethod:state.activeMethod||null,activePhase:state.selectionPhase==='candidates'?'candidate':state.selectionPhase==='sources'?null:state.selectionPhase||null,sourceCountryIds:state.providers.map(row=>row.id),
    candidates:state.candidates.map(row=>({id:ids.candidate(row.id),geometry:geometry('candidate',row.id),...(state.activeMethod==='line'?{area:row.area}:{})})),selectedCandidateIds:state.selectedCandidateIds.map(ids.candidate),selectedComponentKeys:state.selectedComponentKeys,
    components:state.stage==='selection'&&state.selectionPhase==='components'?state.components.map(row=>({key:row.key,geometry:geometry('component',row.key)})):[],
    parts:state.parts.map(row=>({id:ids.part(row.id),method:row.method,geometry:geometry('part',row.id)})),previewReady:state.previewReady,canAddPart:state.canAddPart};
}
const step=(op,name,fields={})=>({op,...(name?{name}:{}),...fields});
export function selectionControllerCases(api) {
  // Equator-symmetric fixtures keep the controller's actual flat projection
  // exactly invertible. No geometry tolerance or coordinate rounding is used.
  const features=remote=>seedSelectionFeatures(api,{remote}).map(feature=>{const copy=clone(feature);const shift=value=>{if(typeof value[0]==='number')value[1]-=5;else value.forEach(shift);};shift(copy.geometry.coordinates);return copy;});
  const row=(name,actions,{remote=false,corrected=false,reference=''}={})=>({case:name,sourceIds:['donor'],features:features(remote),view:selectionCutView(),corrected,reference,actions:[step('observe','before'),...actions]});
  const start=method=>[step('begin'),step('advance'),step('method',null,{method})];
  const draft=(method,points)=>[...start(method),step('draft',null,{points}),step('finish','candidate')];
  const line=y=>[[-1,y],[11,y]];
  const polygon=[[-2,-3],[4,-3],[4,3],[-2,3]],full=[[-1,-6],[11,-6],[11,6],[-1,6]];
  const life=(method,points,fullLine=false)=>[...draft(method,points),...(fullLine?[step('candidate','fullCandidate',{index:1})]:[]),step('archive','archived'),step('advance','review'),step('back','back'),step('cancel','cancel'),...draft(method,points).map(action=>action.name?{...action,name:action.name+'Again'}:action),...(fullLine?[step('candidate',null,{index:1})]:[]),step('archive','archivedAgain'),step('advance','reviewAgain'),step('apply','apply'),step('undo','undo'),step('redo','redo')];
  const all=[];
  for(const [name,points]of [['line-equal-forward',line(0)],['line-equal-reversed',line(0).reverse()]])all.push(row(name,[...draft('line',points),step('candidate','both',{index:1}),step('candidate','secondOnly',{index:0}),step('candidate','reselectedOrder',{index:0}),step('undoPart','undoSelection'),step('candidate','empty',{index:1}),step('cancel','cancel')]));
  all.push(row('line-minimum-area',[...draft('line',line(-4)),step('cancel','cancel')]));
  for(const variant of ['reversed-winding','rotated-start'])for(const reversed of [false,true]){const item=row(`line-source-${variant}${reversed?'-reversed-gesture':''}`,[...draft('line',reversed?line(0).reverse():line(0)),step('cancel','cancel')]);const ring=item.features[1].geometry.coordinates[0];if(variant==='reversed-winding')ring.reverse();else {ring.pop();ring.push(ring.shift());ring.push([...ring[0]]);}all.push(item);}
  {const item=row('line-minimum-rotated-source-start',[...draft('line',line(-4)),step('cancel','cancel')]);const ring=item.features[1].geometry.coordinates[0];ring.pop();ring.push(ring.shift());ring.push([...ring[0]]);all.push(item);}
  all.push(row('line-one-point-rejected',[...start('line'),step('draft',null,{points:[[-1,0]]}),step('finish','rejected',{expectError:true}),step('cancel','cancel')]));
  all.push(row('polygon-partial-lifecycle',life('polygon',polygon)));
  all.push(row('corrected-full-polygon-lifecycle',life('polygon',full),{corrected:true}));
  all.push(row('corrected-full-line-lifecycle',life('line',line(0),true),{corrected:true}));
  all.push(row('components-autoarchive-mixed-parts',[...start('components'),step('component','selected',{key:'component:donor:1:0'}),step('method','automaticallyArchived',{method:'polygon'}),step('draft',null,{points:[[0,-5],[2,-5],[2,-3],[0,-3]]}),step('finish','candidate'),step('archive','mixedParts'),step('removePart','componentPartRemoved',{index:0}),step('undoPart','empty'),step('cancel','cancel')],{remote:true}));
  all.push(row('components-autoarchive-empty-finish-lifecycle',[...start('components'),step('component','selected',{key:'component:donor:1:0'}),step('method','automaticallyArchived',{method:'polygon'}),step('finish','finishedArchivedDraft'),step('advance','review'),step('apply','apply'),step('undo','undo'),step('redo','redo')],{remote:true}));
  for(const reference of ['distribution','label','label-settings'])all.push(row(`dangling-${reference}-apply-rejected`,[...start('components'),step('component','preview',{key:'component:donor:0:0'}),step('archive','archived'),step('advance','review'),step('apply','apply'),step('cancel','cancel')],{reference}));
  return all;
}
function webReferences(runtime) {
  return [...runtime.state.distributionEntries.filter(e=>e.territorialUnitId).map(e=>({kind:'distribution',id:e.id,target:e.territorialUnitId})),...runtime.state.labels.filter(l=>l.countryId).map(l=>({kind:'label',id:l.id,target:l.countryId})),...Object.keys(runtime.state.labelSettings).map(key=>({kind:'label-settings',id:key.slice('territorial:'.length),target:key.slice('territorial:'.length)}))];
}
export async function replayControllerWeb(row,api,loaded) {
  assert.deepEqual(row.view,selectionCutView(),`${row.case}: missing or altered explicit stage-2 view`);
  const runtime=createRuntime(api);runtime.entityStore.restoreProject(api.createStaticTerritorialSnapshot(row.features));
  Object.assign(runtime.state,{distributionEntries:[],labels:[],labelSettings:{},itemVisibility:{},layerPresentation:{schemaVersion:4,styles:{},objectStyles:{},objectOrder:[]}});
  if(row.reference==='distribution')runtime.state.distributionEntries=api.normalizeDistributionEntries([{id:'donor-reference',schemaVersion:3,layerId:'distribution',mode:'territorial',territorialUnitId:'donor',value:1}],{layerExists:()=>true});
  if(row.reference==='label')runtime.state.labels=[{id:'donor-label',countryId:'donor',name:'Donor label',lat:0,lon:5}];
  if(row.reference==='label-settings')runtime.state.labelSettings={'territorial:donor':{visible:false}};
  api.assertProjectReferenceIntegrity({...runtime.state,territorialEntities:runtime.entityRepository.list()});runtime.state.historyDirtyEntityIds.clear();
  const client=api.createMapEditWorkerClient({createWorker:workerFactory(loaded.root,loaded.manifest),getEntities:runtime.entityRepository.list,getFeatureById:runtime.entityRepository.get,getTargetRevision:()=>runtime.state.stateRevision});runtime.ports.spatialQuery.mapEditClient=client;
  const h=createSelectionRuntime(api,{lifecycle:runtime,features:row.features,cutView:row.view}),observations=[];
  const canonical=()=>{const o=observe(runtime);return {document:o.document,labels:runtime.state.labels};};const before=clone(canonical());
  try {
    for(const action of row.actions) {
      let accepted=true,outcome=null;
      switch(action.op){
        case 'begin':accepted=Boolean(h.workflow.start('annex',{targetCountryId:'target',sourceCountryIds:row.sourceIds}));break;
        case 'advance':accepted=await h.workflow.advance();break;
        case 'method':accepted=await h.workflow.selectMethod(action.method);break;
        case 'draft':h.setDraft(action.points);break;
        case 'finish':accepted=h.workflow.finishDraft();break;
        case 'candidate':accepted=h.workflow.selectCandidate(h.workflow.activeSession().candidates[action.index].id);break;
        case 'component':accepted=h.workflow.toggleComponent(action.key);break;
        case 'archive':accepted=h.workflow.addPart();break;
        case 'undoPart':accepted=h.workflow.undoPart();break;
        case 'removePart':accepted=h.workflow.removePart(h.workflow.activeSession().parts[action.index].id);break;
        case 'back':accepted=h.workflow.back();break;
        case 'cancel':h.workflow.clear();break;
        case 'apply':outcome=await h.workflow.apply();accepted=true;break;
        case 'undo':accepted=runtime.snapshots.historyService.undo();break;
        case 'redo':accepted=runtime.snapshots.historyService.redo();break;
        case 'observe':break;
        default:throw Error(`unknown web action ${action.op}`);
      }
      if(!['apply','cancel'].includes(action.op)&&!action.deferSettle)await settle(action.expectError?{...h,errors:[]}:h);
      if(action.name)observations.push({name:action.name,accepted:Boolean(accepted),outcome,view:clone(h.cutView),cutInputs:clone(h.cutInputs),selection:observeSelection(h),features:clone(runtime.entityRepository.list()),references:webReferences(runtime),history:{canUndo:runtime.state.history.length>0,canRedo:runtime.state.future.length>0},unchangedFromBefore:isDeepStrictEqual(before,canonical()),errors:clone(h.errors),reviewFeatures:h.workflow.activeSession()?.stage==='review'&&runtime.state.geometryPreview.session?clone(runtime.state.geometryPreview.session.afterFeatures):[]});
    }
  }finally{h.workflow.clear();client.stop();}
  return {case:row.case,observations};
}
export function compareControllerCase(row,web,native,clipper) {
  assert.equal(native.case,row.case);assert.deepEqual(native.observations.map(o=>o.name),web.observations.map(o=>o.name),'missing or reordered controller observations');
  let webIds=idNormalizer(),nativeIds=idNormalizer();const geometryCache=new Map(),differences=[],observations=[];
  for(let i=0;i<web.observations.length;i++) {
    const w=web.observations[i],n=native.observations[i];if(!w.selection){webIds=idNormalizer();nativeIds=idNormalizer();geometryCache.clear();}const ws=normalizeWeb(w.selection,webIds),ns=normalizeNative(n,nativeIds,geometryCache);
    const add=d=>differences.push({stage:w.name,...d});
    for(const d of compareSelectionStates(ws,ns,clipper))add(d);
    for(const field of ['accepted','outcome','history','references','unchangedFromBefore'])if(!isDeepStrictEqual(w[field],n[field]))add({field,web:w[field],native:n[field]});
    const nativeReview=new Map();for(const overlay of n.overlays.filter(o=>o.kind==='preview')){if(!nativeReview.has(overlay.id))nativeReview.set(overlay.id,{id:overlay.id,properties:{parentId:''},geometry:{type:'MultiPolygon',coordinates:[]}});nativeReview.get(overlay.id).geometry.coordinates.push(...overlay.geometry.coordinates);}
    for(const d of compareCalculation({ok:true,afterFeatures:w.reviewFeatures??[]},{ok:true,features:[...nativeReview.values()]},clipper))add({field:`review.${d.field}`,...Object.fromEntries(Object.entries(d).filter(([key])=>key!=='field'))});
    for(const d of compareCalculation({ok:true,afterFeatures:w.features},{ok:true,features:n.features},clipper))add({field:`document.${d.field}`,...Object.fromEntries(Object.entries(d).filter(([key])=>key!=='field'))});
    observations.push({name:w.name,web:{...w,selection:ws},native:{...n,selection:ns}});
  }
  return {case:row.case,inputView:clone(row.view),behavioralCommit:row.corrected?'12cd8c8ec47c83cfb8c650e8f44c81cdfac10043':behavioralCommit,status:differences.length?'unexpected-mismatch':'matched',differences,observations,events:native.events};
}
export async function collectNativeSelection(binary) {
  const loaded=await productionModules(),api={...loaded.api,...await loadSelectionModules(defaultSelectionSourceRoot)},corpus=selectionControllerCases(api);
  const processResult=spawnSync(binary,[],{input:JSON.stringify(corpus),encoding:'utf8',maxBuffer:32*1024*1024,timeout:180000});
  if(processResult.error||processResult.status!==0)throw Error(`Native controller probe failed: ${processResult.error?.message??processResult.stderr}`);
  const native=JSON.parse(processResult.stdout);assert.ok(Array.isArray(native));assert.deepEqual(native.map(row=>row.case),corpus.map(row=>row.case),'incomplete or reordered native controller corpus');
  const expected=expectations();assert.deepEqual(corpus.map(row=>({case:row.case,stages:row.actions.filter(action=>action.name).map(action=>action.name)})),expected.cases,'controller corpus identities/stages changed');
  const corrected=prepareCorrectedSelectionSources();
  try {
    const correctedApi={...loaded.api,...await loadSelectionModules(corrected.root)},results=[];
    for(let i=0;i<corpus.length;i++){const row=corpus[i],web=await replayControllerWeb(row,row.corrected?correctedApi:api,loaded);const result=compareControllerCase(row,web,native[i],api.clipper);const known=expected.knownDifferences[row.case];assert.ok(Array.isArray(known),'missing explicit case expectation');result.status=isDeepStrictEqual(result.differences,known)?(known.length?'known-mismatch':'matched'):'unexpected-mismatch';result.webCacheMismatches=[];for(const entry of expected.webCacheMismatches.filter(item=>item.case===row.case)){const state=web.observations.find(o=>o.name===entry.stage)?.selection;assert.ok(state,'missing explicit web empty-cache observation');assert.deepEqual(state.selectedCandidateIds,[]);assert.deepEqual(state.selectedComponentKeys,[]);assert.deepEqual(state.parts,[]);assert.equal(state.currentGeometry,null);assert.equal(state.combinedGeometry,null);assert.deepEqual(state[entry.field],entry.web,'web stale-cache value changed');assert.deepEqual(state.baseSourceGeometry,entry.emptySelectionExpected);const feature=geometry=>({id:'empty-cache',properties:{parentId:''},geometry});assert.ok(compareCalculation({ok:true,afterFeatures:[feature(entry.emptySelectionExpected)]},{ok:true,features:[feature(entry.web)]},api.clipper).length,'web empty-cache mismatch unexpectedly disappeared');result.webCacheMismatches.push(entry);}results.push(result);}
    return {schema:'pando-m972-real-controller-differential',version:1,behavioralCommit,correctionCommit:corrected.manifest.behavioralCommit,parityComplete:false,scope:nativeSelectionScope,diagnostics:{nativeStderr:[...new Set(processResult.stderr.trim().split('\n').filter(Boolean))]},cases:results};
  }finally{corrected.cleanup();}
}
export function validateSelectionReport(report) {
  assert.equal(report.schema,'pando-m972-real-controller-differential');assert.equal(report.parityComplete,false);
  const expected=expectations();assert.deepEqual(report.cases.map(row=>row.case),expected.cases.map(row=>row.case),'missing or extra controller report case');
  for(const row of report.cases) {
    assert.deepEqual(row.inputView,selectionCutView(),`${row.case}: missing or altered explicit stage-2 view`);
    for(const observation of row.observations) {
      const view=row.inputView,actual=observation.native.mapViewState;
      assert.ok(actual,`${row.case}/${observation.name}: missing actual native view`);
      const expectedView={projection:view.kind,scale:view.scale,translateX:view.translate[0],translateY:view.translate[1],
        centerLongitude:view.center[0],centerLatitude:view.center[1],rotationLongitude:view.rotate[0],rotationLatitude:view.rotate[1],rotationRoll:view.rotate[2],
        viewportWidth:view.size.width,viewportHeight:view.size.height};
      assert.deepEqual(Object.fromEntries(Object.keys(expectedView).map(key=>[key,actual[key]])),expectedView,
        `${row.case}/${observation.name}: native camera differs from explicit web view`);
      assert.equal(observation.native.coarsePointer,view.coarsePointer,`${row.case}: native pointer mode differs`);
      assert.deepEqual(observation.web.view,view,`${row.case}/${observation.name}: web view differs from input`);
      assert.ok(Array.isArray(observation.web.cutInputs),`${row.case}: missing actual web cut input evidence`);
      for(const payload of observation.web.cutInputs)assert.deepEqual(payload.view,view,`${row.case}: worker cut view differs from input`);
      if(observation.web.selection?.activeMethod==='line'&&observation.web.selection.candidates.length)
        assert.ok(observation.web.cutInputs.length,`${row.case}: candidates lack actual cut input evidence`);
    }
    assert.deepEqual(row.differences,expected.knownDifferences[row.case],`${row.case}: unexpected difference`);
    assert.notEqual(row.status,'unexpected-mismatch',`${row.case}: ${JSON.stringify(row.differences)}`);
    assert.ok(row.events.some(event=>event.selectionPending),`${row.case} did not execute selection worker`);
    if(!row.case.includes('one-point'))assert.ok(row.events.some(event=>event.previewPending),`${row.case} did not execute preview timer`);
    if(row.case.includes('lifecycle')||row.case.includes('apply-rejected'))assert.ok(row.events.some(event=>event.applying),`${row.case} did not execute strict Apply`);
  }
}
if(process.argv[1]===fileURLToPath(import.meta.url)) {
  const [binary,path]=process.argv.slice(2);if(!binary||!path)throw Error('usage: native-selection.mjs <m972_selection_probe> <report.json>');
  const report=await collectNativeSelection(binary);writeFileSync(path,JSON.stringify(report,null,2)+'\n');validateSelectionReport(report);console.log(`Observed ${report.cases.length} real controller traces`);
}
