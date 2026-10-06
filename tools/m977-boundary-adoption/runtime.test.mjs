import test from 'node:test';
import assert from 'node:assert/strict';
import {loadSourceHistoryNodeSources} from '../m974-snap-boundary/source-history-sources.mjs';
import {boundaryTimingCases,runBoundaryTimingCase,verifyBoundaryTimingCase} from './runtime.mjs';

test('actual production preparation, adoption and move are observed with public actions',async()=>{
 const loaded=await loadSourceHistoryNodeSources();
 try {
  for(const definition of boundaryTimingCases()){
   const row=await runBoundaryTimingCase(loaded,definition);
   verifyBoundaryTimingCase(definition,row);
   assert.equal(row.limits.workerCPUExecutionPhase,false);
   if(definition.phase==='render-adoption'){assert.ok(row.stages.interval.adoptionFreezeEvidence.frozen>0);assert.ok(row.stages.interval.adoptionFreezeEvidence.unfrozen>0,'freezing is genuinely unfinished at the adoption interval');}
  }
 } finally {loaded.cleanup();}
});

test('evidence rejects fabricated settlement, early post, stale state and omitted public actions',async()=>{
 const loaded=await loadSourceHistoryNodeSources();
 try{
  for(const definition of boundaryTimingCases()){
   const row=await runBoundaryTimingCase(loaded,definition),bad=mutate=>{const copy=structuredClone(row);mutate(copy);assert.throws(()=>verifyBoundaryTimingCase(definition,copy));};
   bad(r=>r.stages.settled.canonical+=' ');
   bad(r=>r.stages.settled.history.undo++);
   bad(r=>r.transport.find(e=>e.kind==='public-action').action='forged-generation-change');
   bad(r=>delete r.stages.interval);
   bad(r=>r.limits.rawParity=true);
   if(definition.phase==='render-adoption'){
    bad(r=>r.stages.interval.preparation.workerPending=true);
    bad(r=>r.stages.interval.preparation.hasPacket=true);
    bad(r=>r.stages.interval.adoptionFreezeEvidence.unfrozen=0);
    bad(r=>r.stages.interval.adoptionFreezeEvidence.frozen=0);
    bad(r=>{for(const e of r.transport)if(e.kind==='client-resolved')e.kind='held-completed-result';});
   }else{
    bad(r=>r.transport.find(e=>e.kind==='queued-before-worker-post').kind='held-completed-result');
    bad(r=>{const q=r.transport.find(e=>e.kind==='queued-before-worker-post');r.transport.push({...q,kind:'posted',sequence:0});});
   }
  }
 }finally {loaded.cleanup();}
});

test('resolved clients cannot survive removal of their actual Worker transport',async()=>{
 const loaded=await loadSourceHistoryNodeSources();
 try{for(const definition of boundaryTimingCases()){
  const row=await runBoundaryTimingCase(loaded,definition),bad=structuredClone(row),removed=new Set(['request','posted','response','delivered','released-request']);
  for(const stage of Object.values(bad.stages))stage.transportSequence=bad.transport.filter(e=>e.sequence<stage.transportSequence&&!removed.has(e.kind)).length;
  bad.transport=bad.transport.filter(e=>!removed.has(e.kind));bad.transport.forEach((e,i)=>e.sequence=i);
  assert.throws(()=>verifyBoundaryTimingCase(definition,bad),'missing actual Worker evidence must fail '+definition.id);
 }}finally{loaded.cleanup();}
});

test('public action cannot be moved outside its declared pending interval',async()=>{
 const loaded=await loadSourceHistoryNodeSources();
 try{for(const definition of boundaryTimingCases()){
  const bad=structuredClone(await runBoundaryTimingCase(loaded,definition)),index=bad.transport.findIndex(e=>e.kind==='public-action'),[action]=bad.transport.splice(index,1);
  for(const s of Object.values(bad.stages))if(s.transportSequence>index)s.transportSequence--;
  bad.transport.push(action);bad.transport.forEach((e,i)=>e.sequence=i);
  assert.throws(()=>verifyBoundaryTimingCase(definition,bad),'late public action must fail '+definition.id);
 }}finally{loaded.cleanup();}
});
