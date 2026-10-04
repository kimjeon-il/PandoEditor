import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { loadOracle } from './calculations.mjs';

const moduleUrl=new URL('./native-selection.mjs',import.meta.url);
const implementation=async()=>{
  assert.equal(existsSync(moduleUrl),true,'real EditorController differential adapter must exist');
  return import(moduleUrl);
};
const geometry={type:'Polygon',coordinates:[[[0,0],[0,1],[1,1],[1,0],[0,0]]]};
const state=()=>({stage:'selection',activeMethod:'line',activePhase:'candidate',sourceCountryIds:['donor'],candidates:[{id:'candidate:0',geometry},{id:'candidate:1',geometry}],selectedCandidateIds:['candidate:1','candidate:0'],selectedComponentKeys:[],components:[],parts:[],previewReady:true,canAddPart:true});

test('real controller adapter exists and explicitly bounds A-C coverage',async()=>{
  const api=await implementation();
  assert.equal(api.nativeSelectionScope.entrypoint,'EditorController');
  assert.deepEqual(api.nativeSelectionScope.deferred,['real-river-partition','complex-cut-graphs']);
  assert.ok(api.nativeSelectionScope.unobserved.includes('native-internal-selection-caches'));
});
test('selection comparison preserves ordered stable IDs and detects reordered candidate geometry',async()=>{
  const {compareSelectionStates}=await implementation();const {clipper}=await loadOracle();const a=state(),b=structuredClone(a);
  b.selectedCandidateIds.reverse();
  assert.ok(compareSelectionStates(a,b,clipper).some(row=>row.field==='selectedCandidateIds'));
  b.selectedCandidateIds=a.selectedCandidateIds;b.candidates.reverse();
  assert.ok(compareSelectionStates(a,b,clipper).some(row=>row.field==='candidates.0.id'));
});
test('selection geometry comparison accepts winding only and reports exact small XOR',async()=>{
  const {compareSelectionStates}=await implementation();const {clipper}=await loadOracle();const a=state(),b=structuredClone(a);
  b.candidates[0].geometry.coordinates[0].reverse();assert.deepEqual(compareSelectionStates(a,b,clipper),[]);
  for(const p of b.candidates[0].geometry.coordinates[0])p[0]+=1e-8;
  assert.ok(compareSelectionStates(a,b,clipper).some(row=>row.field==='candidates.0.geometry'));
});
test('malformed or duplicate observations fail closed rather than matching',async()=>{
  const {compareSelectionStates}=await implementation();const {clipper}=await loadOracle();
  for(const mutate of [s=>s.candidates.push(s.candidates[0]),s=>delete s.previewReady,s=>delete s.candidates[0].geometry,s=>s.selectedCandidateIds=['unknown']]){
    const a=state();mutate(a);assert.throws(()=>compareSelectionStates(a,a,clipper),/observation|geometry|identity|candidate/);
  }
});
test('a missing executable cannot yield an empty passing controller report',async()=>{
  const {collectNativeSelection}=await implementation();await assert.rejects(()=>collectNativeSelection('/no/such/m972-probe'),/Native controller probe failed/);
});
test('actual controller timers, worker previews, strict Apply and Undo match pinned production traces', {timeout:180000},async()=>{
  assert.ok(process.env.PANDO_M972_SELECTION_PROBE,'PANDO_M972_SELECTION_PROBE must name the built real controller executable');
  const {collectNativeSelection,validateSelectionReport}=await implementation();
  const report=await collectNativeSelection(process.env.PANDO_M972_SELECTION_PROBE);
  validateSelectionReport(report);
  assert.equal(report.parityComplete,false);
  assert.equal(report.cases.length,17);
  assert.ok(report.cases.every(row=>row.status==='matched'||row.status==='known-mismatch'));
  const observation=(name,stage)=>report.cases.find(row=>row.case===name).observations.find(row=>row.name===stage);
  for(const name of ['line-equal-forward','line-equal-reversed']) {
    const initial=observation(name,'candidate').native.selection;
    assert.deepEqual(initial.candidates.map(row=>row.area),[50,50]);
    assert.deepEqual(initial.selectedCandidateIds,['candidate:0']);
    assert.deepEqual(observation(name,'reselectedOrder').native.selection.selectedCandidateIds,['candidate:1','candidate:0']);
  }
  for(const name of ['line-minimum-area','line-minimum-rotated-source-start']) {
    const selected=observation(name,'candidate').native.selection;
    assert.equal(selected.candidates.find(row=>row.id===selected.selectedCandidateIds[0]).area,10);
  }
  for(const name of ['polygon-partial-lifecycle','corrected-full-polygon-lifecycle','corrected-full-line-lifecycle']) {
    const candidate=observation(name,'candidate').native,archive=observation(name,'archived').native,review=observation(name,'review').native;
    assert.equal(candidate.state.stage,'selection');assert.equal(candidate.state.canApply,false);
    assert.equal(archive.selection.parts.length,1);assert.equal(archive.state.canApply,false);
    assert.equal(review.state.canApply,true);assert.equal(review.unchangedFromBefore,true);
    assert.equal(observation(name,'apply').native.outcome,true);
    assert.equal(observation(name,'undo').native.unchangedFromBefore,true);
    assert.deepEqual(observation(name,'redo').native.features,observation(name,'apply').native.features);
  }
  for(const kind of ['distribution','label','label-settings']) {
    const name=`dangling-${kind}-apply-rejected`,preview=observation(name,'preview'),apply=observation(name,'apply');
    assert.equal(preview.native.state.previewReady,true);assert.equal(preview.native.state.canApply,false);
    assert.equal(apply.native.outcome,false);assert.equal(apply.native.unchangedFromBefore,true);
    assert.equal(apply.native.history.canUndo,false);assert.match(apply.native.state.error,/DANGLING_REF/);
    assert.equal(apply.web.outcome,false);assert.ok(apply.web.errors.length);
  }
  const archivedResult=observation('components-autoarchive-empty-finish-lifecycle','finishedArchivedDraft').native;
  assert.equal(archivedResult.accepted,true);assert.equal(archivedResult.selection.activePhase,'result');assert.equal(archivedResult.selection.previewReady,true);
  assert.equal(observation('components-autoarchive-empty-finish-lifecycle','apply').native.outcome,true);
  assert.equal(observation('components-autoarchive-empty-finish-lifecycle','undo').native.unchangedFromBefore,true);
  assert.equal(report.cases.flatMap(row=>row.webCacheMismatches).length,4);
  const missing=structuredClone(report);missing.cases.pop();assert.throws(()=>validateSelectionReport(missing),/missing or extra/);
  const forged=structuredClone(report);forged.cases[0].differences=[];assert.throws(()=>validateSelectionReport(forged),/unexpected difference/);
});
