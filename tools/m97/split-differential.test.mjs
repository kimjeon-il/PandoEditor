import test from 'node:test';
import assert from 'node:assert/strict';
import {readSplitObservation} from './web-split-fixtures.mjs';
const implementation=await import('./split-differential.mjs').catch(error=>{if(error.code==='ERR_MODULE_NOT_FOUND')return {};throw error;});
const fixture=()=>readSplitObservation('lifecycle-observations-v2').cases.find(row=>row.case==='root-two-crossing');
const api=()=>{assert.equal(typeof implementation.normalizeSplitWebCase,'function','actual web observation adapter is required');assert.equal(typeof implementation.compareSplitObservations,'function','fail-closed split comparator is required');return implementation;};

test('split comparison accepts independently serialized complete observation values',async()=>{
 const {normalizeSplitWebCase,compareSplitObservations}=api(),row=normalizeSplitWebCase(fixture());
 assert.deepEqual(await compareSplitObservations(row,structuredClone(row)),[]);
});
test('split comparison rejects missing stages and missing required fields',async()=>{
 const {normalizeSplitWebCase,compareSplitObservations}=api(),row=normalizeSplitWebCase(fixture());
 for(const mutate of [r=>delete r.stages.cancel,r=>delete r.stages.confirm.features,r=>delete r.stages.redo.history,r=>delete r.stages.before.references,r=>delete r.stages.candidates.selection.candidates[0].geometry]){
  const changed=structuredClone(row);mutate(changed);await assert.rejects(()=>compareSplitObservations(row,changed),/missing|geometry|observation|stage|features|references|history/i);
 }
});
test('split comparison detects candidate order, chosen identity, tiny geometry, refs and history corruption',async()=>{
 const {normalizeSplitWebCase,compareSplitObservations}=api(),row=normalizeSplitWebCase(fixture());
 for(const mutate of [r=>r.stages.candidates.selection.candidates.reverse(),r=>r.stages.selected.selection.selectedCandidateIds.push('candidate:0'),r=>r.stages.confirm.features[0].geometry.coordinates[0][1][0]+=1e-10,r=>r.stages.confirm.features[0].parentId='unexpected',r=>r.stages.confirm.references.push({kind:'label',id:'foreign',target:'source'}),r=>r.stages.undo.history.canRedo=false,r=>r.stages.confirm.createdIds=[],r=>r.stages.confirm.outcome=false]){
  const changed=structuredClone(row);mutate(changed);assert.ok((await compareSplitObservations(row,changed)).length,'corrupted observation must not match');
 }
});
test('split comparison rejects malformed rings, dangling selections and duplicate identities',async()=>{
 const {normalizeSplitWebCase,compareSplitObservations}=api(),row=normalizeSplitWebCase(fixture());
 for(const mutate of [r=>r.stages.confirm.features[0].geometry.coordinates[0].pop(),r=>r.stages.confirm.features.push(r.stages.confirm.features[0]),r=>r.stages.selected.selection.selectedCandidateIds.push('missing-candidate')]){
  const changed=structuredClone(row);mutate(changed);await assert.rejects(()=>compareSplitObservations(row,changed),/ring|duplicate|selected|identity/i);
 }
});
test('generated object mapping preserves cross-stage identities and rejects unknown replacements',async()=>{
 const {normalizeSplitWebCase,compareSplitObservations}=api(),raw=fixture(),row=normalizeSplitWebCase(raw),changed=structuredClone(raw);
 const current=changed.stages.redo.document.document.entities.find(e=>e.id!=='source');current.id='entity-unobserved';
 await assert.rejects(async()=>compareSplitObservations(row,normalizeSplitWebCase(changed)),/identity|created|redo/i);
});
test('retained presentation and opaque generic metadata are compared at each checkpoint',async()=>{
 const {normalizeSplitWebCase,compareSplitObservations}=api();
 for(const id of ['root-with-dependent-presentation','root-with-dependent-generic-metadata']){
  const row=normalizeSplitWebCase(readSplitObservation('lifecycle-observations-v2').cases.find(c=>c.case===id)),bad=structuredClone(row);
  assert.ok(row.stages.confirm.presentation,'retained presentation observation required');assert.ok(Array.isArray(row.stages.confirm.genericMetadata),'retained generic metadata observation required');
  if(id.endsWith('presentation'))bad.stages.confirm.presentation.hiddenItems=[];else bad.stages.confirm.genericMetadata=[];
  assert.ok((await compareSplitObservations(row,bad)).length,'retained state corruption must fail');
 }
});
test('raw web adapter fails closed on missing history and stale candidate batches',()=>{
 const {normalizeSplitWebCase}=api();
 for(const mutate of [r=>delete r.stages.confirm.document.history.undo,r=>{const s=r.stages.selected.selection;s.candidates[0].id='replacement:0';s.selectedCandidateIds=s.selectedCandidateIds.map(id=>id===r.stages.candidates.selection.candidates[0].id?'replacement:0':id);}]){
  const raw=fixture();mutate(raw);assert.throws(()=>normalizeSplitWebCase(raw),/history|candidate|batch|identity/);
 }
});
test('successful commit selection is observed and identity checked',async()=>{
 const {normalizeSplitWebCase,compareSplitObservations}=api(),row=normalizeSplitWebCase(fixture());assert.deepEqual(row.stages.confirm.committedSelection,{domain:'territorial',id:'$created'});const bad=structuredClone(row);bad.stages.confirm.committedSelection={domain:'territorial',id:'source'};assert.ok((await compareSplitObservations(row,bad)).length);
});
test('inactive cache differences remain explicit and require successful actual reactivation evidence',async()=>{
 const {normalizeSplitWebCase,compareSplitObservations}=api();assert.equal(typeof implementation.classifyInactiveSplitCache,'function','bounded inactive-cache classifier required');
 const raw=readSplitObservation('lifecycle-observations-v2').cases.find(c=>c.case==='root-empty-selection'),web=normalizeSplitWebCase(raw),native=structuredClone(web);for(const name of ['selected','archive','review'])native.stages[name].selection.remainingGeometry=structuredClone(native.stages.before.features.find(f=>f.id==='source').geometry);
 const differences=await compareSplitObservations(web,native);assert.equal(differences.length,3);assert.equal(await implementation.classifyInactiveSplitCache(web,native,differences,{}),null,'missing reactivation must not excuse a raw difference');
 const {loadSplitModules,runSplitLifecycleCase,splitReactivationCaseDefinition}=await import('./web-split.mjs'),loaded=await loadSplitModules();let reactivationWeb;try{reactivationWeb=normalizeSplitWebCase(await runSplitLifecycleCase(loaded,splitReactivationCaseDefinition()));}finally{loaded.cleanup();}
 const reactivationNative=structuredClone(reactivationWeb);reactivationNative.stages.emptied.selection.remainingGeometry=structuredClone(reactivationNative.stages.before.features.find(f=>f.id==='source').geometry);const reactivationDifferences=await compareSplitObservations(reactivationWeb,reactivationNative),evidence={web:reactivationWeb,native:reactivationNative,differences:reactivationDifferences};
 const diagnostic=await implementation.classifyInactiveSplitCache(web,native,differences,evidence);assert.equal(diagnostic.rawParity,false);assert.equal(diagnostic.checkpoints.length,3);assert.notDeepEqual(diagnostic.checkpoints[0].web.remainingGeometry,diagnostic.checkpoints[0].native.remainingGeometry);
 const broken=structuredClone(evidence);broken.native.stages.reactivated.selection.previewReady=false;assert.equal(await implementation.classifyInactiveSplitCache(web,native,differences,broken),null);
 const corrupt=structuredClone(native);corrupt.stages.selected.selection.previewReady=true;assert.equal(await implementation.classifyInactiveSplitCache(web,corrupt,differences,evidence),null);const oneUlp=structuredClone(native);oneUlp.stages.selected.selection.remainingGeometry.coordinates[0][2][0]=10.000000000000002;assert.equal(await implementation.classifyInactiveSplitCache(web,oneUlp,differences,evidence),null,'an adjacent-double boundary error is not an inactive-cache exemption');
});
test('raw adapters reject omitted geometry values and missing or conflicting parent identities',()=>{
 const {normalizeSplitWebCase}=api();for(const mutate of [r=>delete r.stages.selected.selection.remainingGeometry,r=>delete r.stages.selected.selection.combinedGeometry,r=>{const f=r.stages.before.document.document.entities[0];delete f.parentId;delete f.properties.parentId;},r=>r.stages.before.document.document.entities[0].properties.parentId='conflicting-parent']){const raw=readSplitObservation('lifecycle-observations-v2').cases.find(c=>c.case==='root-all-selected');mutate(raw);assert.throws(()=>normalizeSplitWebCase(raw),/missing|parent|geometry|observation/i);}
 const invalid={case:'protocol-missing-native-geometry',stageOrder:['before','candidates','selected'],stages:{before:{features:[{id:'source',geometry:{type:'Polygon',coordinates:[[[0,0],[0,1],[1,1],[0,0]]]},parentId:'',coverageMode:'explicit',name:'source'}]},candidates:{raw:{candidates:[]}},selected:{raw:{candidates:[]}}}};
 assert.throws(()=>implementation.normalizeSplitNativeCase(invalid),/missing|observation/);
});
test('raw selection identity and control observations cannot disappear through normalization',()=>{
 const {normalizeSplitWebCase}=api();for(const mutate of [r=>delete r.stages.archive.selection.activeMethod,r=>delete r.stages.archive.selection.activePhase,r=>delete r.stages.candidates.selection.candidates[0].area,r=>delete r.stages.archive.selection.parts[0].method,r=>r.stages.review.selection.parts[0].id='replacement-part',r=>{r.stages.emptied=structuredClone(r.stages.candidates);r.stages.reactivated=structuredClone(r.stages.candidates);const s=r.stages.reactivated.selection,old=s.candidates[0].id;s.candidates[0].id='replacement-candidate';s.selectedCandidateIds=s.selectedCandidateIds.map(id=>id===old?'replacement-candidate':id);}]){const raw=fixture();mutate(raw);assert.throws(()=>normalizeSplitWebCase(raw),/missing|candidate|part|batch|identity|method|phase|area/i);}
});
test('adjacent binary64 boundary movement is never hidden by polygon clipping rounding',async()=>{
 const {normalizeSplitWebCase,compareSplitObservations}=api(),row=normalizeSplitWebCase(fixture());for(const coordinate of [10.000000000000002,9.999999999999998]){const bad=structuredClone(row);bad.stages.confirm.features[0].geometry.coordinates[0][1][0]=coordinate;assert.ok((await compareSplitObservations(row,bad)).some(d=>d.field==='confirm.features.geometry'),'one ULP of real boundary movement must fail');}
});
test('exact ring start, winding, container and collinear subdivision changes remain equivalent',async()=>{
 const {normalizeSplitWebCase,compareSplitObservations}=api(),row=normalizeSplitWebCase(fixture()),same=structuredClone(row),feature=same.stages.confirm.features[0],ring=feature.geometry.coordinates[0].slice(0,-1);ring.splice(1,0,[(ring[0][0]+ring[1][0])/2,ring[0][1]]);ring.reverse();ring.push(ring.shift());ring.push([...ring[0]]);feature.geometry={type:'MultiPolygon',coordinates:[[ring]]};assert.deepEqual(await compareSplitObservations(row,same),[]);
});

test('exact boundaries preserve exterior-hole roles and reject backtracking spikes',()=>{
 const outer=[[0,0],[10,0],[10,10],[0,10],[0,0]],hole=[[1,1],[2,1],[2,2],[1,2],[1,1]],a={type:'Polygon',coordinates:[outer,hole]};
 assert.notDeepEqual(implementation.exactGeometryBoundary(a),implementation.exactGeometryBoundary({type:'Polygon',coordinates:[hole,outer]}));
 const spike=structuredClone(outer);spike.splice(2,0,[12,0],[10,0]);assert.notDeepEqual(implementation.exactGeometryBoundary({type:'Polygon',coordinates:[outer]}),implementation.exactGeometryBoundary({type:'Polygon',coordinates:[spike]}));
});

test('review preserves actual preview parent identity and ordered moved and retained descendants',()=>{
 const {normalizeSplitWebCase}=api();
 const root=normalizeSplitWebCase(fixture());assert.equal(root.stages.review.previewPresent,true);assert.deepEqual(root.stages.review.previewFeatures.map(({id,parentId})=>({id,parentId})),[{id:'source',parentId:''},{id:'$created-preview',parentId:''}]);
 const raw=readSplitObservation().cases.find(row=>row.case==='child-with-dependents'),row=normalizeSplitWebCase(raw);
 assert.deepEqual(row.stages.review.previewFeatures.map(({id,parentId})=>({id,parentId})),[
  {id:'$created-preview',parentId:'parent'},{id:'source',parentId:'parent'},{id:'child-moved',parentId:'$created-preview'},{id:'child-cross',parentId:'source'},{id:'grandchild-cross',parentId:'child-cross'}]);
 assert.equal(row.stages.review.features.find(f=>f.id==='child-moved').parentId,'source','preview must not mutate canonical ancestry');
 assert.equal(row.stages.cancel.features.find(f=>f.id==='child-moved').parentId,'source');
});
test('review rejects omitted, null, conflicting and unknown preview parent observations',()=>{
 const {normalizeSplitWebCase}=api();
 for(const mutate of [r=>delete r.stages.review.document.preview,r=>delete r.stages.review.document.preview.afterFeatures,r=>delete r.stages.review.document.preview.afterFeatures[0].properties.parentId,r=>r.stages.review.document.preview.afterFeatures[0].properties.parentId=null,r=>r.stages.review.document.preview.afterFeatures[0].parentId='source',r=>r.stages.review.document.preview.afterFeatures[0].properties.parentId='unobserved-parent']){
  const raw=fixture();mutate(raw);assert.throws(()=>normalizeSplitWebCase(raw),/missing|invalid|conflicting|unknown|identity|preview/i);
 }
});
test('review compares parent mutations and cannot accept missing parents on both sides',async()=>{
 const {normalizeSplitWebCase,compareSplitObservations}=api(),row=normalizeSplitWebCase(readSplitObservation().cases.find(c=>c.case==='child-with-dependents'));
 for(const mutate of [r=>r.stages.review.previewFeatures.find(f=>f.id==='child-moved').parentId='source',r=>r.stages.review.previewFeatures.find(f=>f.id==='grandchild-cross').parentId='$created-preview',r=>r.stages.review.previewFeatures[0].parentId='',r=>r.stages.review.previewFeatures.reverse()]){
  const bad=structuredClone(row);mutate(bad);assert.ok((await compareSplitObservations(row,bad)).some(d=>d.field==='review.previewFeatures.metadata'));
 }
 const missing=structuredClone(row);delete missing.stages.review.previewFeatures[0].parentId;
 await assert.rejects(()=>compareSplitObservations(row,missing),/missing.*parent/i);
 await assert.rejects(()=>compareSplitObservations(missing,structuredClone(missing)),/missing.*parent/i);
});
test('an observed null review preview differs from an observed empty preview and never fills missing evidence',async()=>{
 const {normalizeSplitWebCase,compareSplitObservations}=api(),raw=readSplitObservation().cases.find(c=>c.case==='root-empty-selection'),row=normalizeSplitWebCase(raw);
 assert.equal(row.stages.review.previewPresent,false);assert.deepEqual(row.stages.review.previewFeatures,[]);
 const empty=structuredClone(raw);empty.stages.review.document.preview={afterFeatures:[]};const normalized=normalizeSplitWebCase(empty);assert.equal(normalized.stages.review.previewPresent,true);
 assert.ok((await compareSplitObservations(row,normalized)).some(d=>d.field==='review.previewPresent'));
 const absent=structuredClone(row);delete absent.stages.review.previewPresent;await assert.rejects(()=>compareSplitObservations(row,absent),/missing.*preview/i);
});
test('normalized review cannot accept an out-of-map parent on both sides',async()=>{
 const {normalizeSplitWebCase,compareSplitObservations}=api(),row=normalizeSplitWebCase(fixture());row.stages.review.previewFeatures[0].parentId='unobserved-parent';
 await assert.rejects(()=>compareSplitObservations(row,structuredClone(row)),/unknown.*parent|parent.*identity/i);
});
