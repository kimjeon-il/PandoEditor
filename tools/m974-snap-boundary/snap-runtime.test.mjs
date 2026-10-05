import test from 'node:test';
import assert from 'node:assert/strict';
import {loadNodeSources} from './sources.mjs';
const module=await import('./snap-runtime.mjs').catch(error=>{if(error.code!=='ERR_MODULE_NOT_FOUND')throw error;return {};});
test('actual pointer provider is cold first and settled candidate metadata drives a later draft input', async()=>{
 assert.equal(typeof module.runSnapCase,'function','Actual pointer workflow harness must exist');
 const loaded=await loadNodeSources();try{
  const definition=module.snapCases(loaded.api).find(row=>row.id==='snap-vertex-cold-ready');
  const row=await module.runSnapCase(loaded,definition);
  assert.deepEqual(row.queries[0].cold.candidates,[]);
  assert.equal(row.queries[0].pending.sameArray,true);
  assert.equal(row.queries[0].pending.requestCount,1);
  assert.ok(row.queries[0].ready.candidates.length>0);
  assert.equal(row.queries[0].ready.indicator.kind,'vertex');
  assert.deepEqual(row.queries[0].afterSettlementDraft,row.queries[0].cold.draft);
  assert.equal(row.queries[0].ready.indicator.ownerIds[0],'a');
  assert.ok(row.transport.some(event=>event.direction==='request'&&event.message.operation==='territorial-snap'));
 }finally{loaded.cleanup();}
});
test('actual source insertion history survives reorder and changes on deletion/reinsert',async()=>{
 assert.equal(typeof module.runSnapCase,'function','Actual pointer workflow harness must exist');
 const loaded=await loadNodeSources();try{
  const row=await module.runSnapCase(loaded,module.snapCases(loaded.api).find(row=>row.id==='snap-source-order'));
  assert.deepEqual(row.queries.map(q=>q.ready.candidates.find(c=>c.kind==='vertex').ownerIds[0]),['a','a','a','b','b']);
 }finally{loaded.cleanup();}
});
test('all five candidate classes and exact inclusive pointer thresholds use real worker candidates',async()=>{
 const loaded=await loadNodeSources();try{
  const cases=module.snapCases(loaded.api),classes=new Set();
  for(const id of ['snap-vertex-cold-ready','snap-distance-before-kind','snap-intersection-priority','snap-boundary-priority','snap-neighbor']){
   const row=await module.runSnapCase(loaded,cases.find(c=>c.id===id));classes.add(row.queries[0].ready.indicator.kind);
  }
  assert.deepEqual([...classes].sort(),['boundary','edge','intersection','neighbor','vertex']);
  for(const pointer of ['mouse','touch','pen'])for(const distance of [10,10.00001,18,18.00001]){
   const row=await module.runSnapCase(loaded,cases.find(c=>c.id===`snap-threshold-${pointer}-${distance}`));
   assert.equal(Boolean(row.queries[0].ready.result),distance<=(pointer==='touch'?18:10));
  }
 }finally{loaded.cleanup();}
});
test('real cancellations and stale source responses cannot become a settled result',async()=>{
 const loaded=await loadNodeSources();try{
  for(const definition of module.snapCases(loaded.api).filter(row=>row.scenario)){
   const row=await module.runSnapCase(loaded,definition);
   assert.equal(row.requests[0].status,'rejected',definition.id);
   assert.equal(row.queries[0].ready.observed,false,definition.id);
   assert.ok(row.transport.some(event=>event.direction==='request'&&event.message.type==='execute'));
   if(['cancel','project-replacement'].includes(definition.scenario))assert.deepEqual(row.lifecycle.after.draft.coords,[]);
  }
 }finally{loaded.cleanup();}
});
test('a current production worker error stays explicit and next real input retries successfully',async()=>{
 const loaded=await loadNodeSources();try{
  const row=await module.runSnapCase(loaded,module.snapCases(loaded.api).find(c=>c.id==='snap-current-error-retry'));
  assert.equal(row.requests[0].status,'rejected');
  assert.equal(row.queries[0].ready.observed,false);
  assert.equal(row.queries[1].ready.indicator.kind,'vertex');
  assert.equal(row.requests.at(-1).status,'resolved');
 }finally{loaded.cleanup();}
});
