import test from 'node:test';
import assert from 'node:assert/strict';
const runtime=await import('./source-history-runtime.mjs').catch(error=>{if(error.code!=='ERR_MODULE_NOT_FOUND')throw error;return {};});
test('source-history runtime exposes a portable actual-production driver',()=>{
 assert.equal(typeof runtime.runSourceHistoryCase,'function','New production workflow source-history driver required');
});
test('generic delete and Undo without Worker observation retain first generic owner',async()=>{
 assert.equal(typeof runtime.runSourceHistoryCase,'function','Actual production source-history driver required');
 const {loadSourceHistoryNodeSources}=await import('./source-history-sources.mjs');
 const loaded=await loadSourceHistoryNodeSources();
 try{const row=await runtime.runSourceHistoryCase(loaded,{id:'generic-unsynced',scenario:'generic-unsynced'});
 assert.equal(row.stages.warm.winner,'ga');assert.equal(row.stages.final.winner,'ga');assert.equal(row.stages.final.indicator?.ownerIds[0],'ga','Actual production snap indicator is recorded');assert.equal(row.stages.restored.canonicalEqual,true);
 assert.ok(row.transport.some(e=>e.direction==='request'&&e.message?.operation==='territorial-snap'));
 }finally{loaded.cleanup();}
});
for(const [scenario,winner] of [['boundary-ready-cancel','gb'],['boundary-held-result-stop','ga'],['preview-stop-ready-snap-hit','ga'],['stopped-root-delete-undo','ga'],['replacement-after-stop','ga'],['superseded-result','gb']])test(scenario+' records its actual transport lifecycle',async()=>{
 const {loadSourceHistoryNodeSources}=await import('./source-history-sources.mjs'),loaded=await loadSourceHistoryNodeSources();
 try{const row=await runtime.runSourceHistoryCase(loaded,{id:scenario,scenario});assert.equal(row.stages.warm.winner,'ga');assert.equal(row.stages.final.winner,winner);
 if(scenario==='preview-stop-ready-snap-hit'){assert.equal(row.stages.cached.winner,'gb');assert.equal(row.stages.cached.requestCount,0);assert.equal(row.stages.final.requestCount,1);assert.equal(row.stages.cached.worker.workerActive,false);}
 if(scenario==='boundary-ready-cancel')assert.equal(row.transport.filter(e=>e.direction==='terminate'&&e.phase!=='cleanup').length,0);
 if(scenario==='boundary-held-result-stop'){assert.equal(row.stages.operation.preparation.workerPending,true);assert.equal(row.stages.cancelled.worker.workerActive,false);assert.ok(row.transport.some(e=>e.direction==='held'));}
 if(scenario==='stopped-root-delete-undo')assert.deepEqual(row.transport.filter(e=>e.direction==='client-sync-patch').map(e=>e.result),[false,false]);
 assert.equal(row.errors.length,0,JSON.stringify(row.errors));
 }finally{loaded.cleanup();}
});
for(const [scenario,winner,postSync]of [['settled-success-observes-late-deletion','gb',true],['cancelled-ticket-skips-post-sync','ga',false]])test(scenario+' distinguishes accepted success from cancelled ticket settlement',async()=>{
 const {loadSourceHistoryNodeSources}=await import('./source-history-sources.mjs'),loaded=await loadSourceHistoryNodeSources();
 try{const row=await runtime.runSourceHistoryCase(loaded,{id:scenario,scenario});assert.equal(row.stages.final.winner,winner);assert.equal(row.stages.restored.canonicalEqual,true);
 assert.equal(row.transport.some(e=>e.direction==='request'&&e.phase==='releaseResult'&&e.message.type==='edit-sync'&&e.message.removedKeys.includes('generic:ga')),postSync);
 assert.equal(row.transport.filter(e=>e.direction==='terminate'&&e.phase!=='cleanup').length,0);
 assert.ok(row.requests.some(r=>r.phase==='lateRequest'&&r.status==='rejected'));
 }finally{loaded.cleanup();}
});
