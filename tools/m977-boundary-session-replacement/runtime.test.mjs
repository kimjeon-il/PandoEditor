import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {loadSourceHistoryNodeSources} from '../m974-snap-boundary/source-history-sources.mjs';

test('exact two public replacements retain new ownership after held old completion',async()=>{
 assert.ok(fs.existsSync(new URL('./runtime.mjs',import.meta.url)),'The additive paired session-replacement runtime must exist');
 const {boundarySessionReplacementCases,runBoundarySessionReplacementCase,verifyBoundarySessionReplacementCase}=await import('./runtime.mjs');
 const cases=boundarySessionReplacementCases();assert.deepEqual(cases.map(c=>c.id),['preparation-completed-reenter','preview-completed-reenter']);
 const loaded=await loadSourceHistoryNodeSources();
 try{for(const definition of cases){const row=await runBoundarySessionReplacementCase(loaded,definition);verifyBoundarySessionReplacementCase(definition,row);
  assert.deepEqual(row.stages.settled.preparation.ownerIds,['A','B']);assert.equal(row.stages.settled.selection.primaryKey,'territorial:entity:B');
  assert.equal(row.stages.settled.preparation.handles.find(h=>h.nodeKey==='1,1').fixed,true);assert.equal(row.stages.settled.preview,false);
  const mutate=fn=>{const bad=structuredClone(row);fn(bad);assert.throws(()=>verifyBoundarySessionReplacementCase(definition,bad));};
  mutate(r=>r.stages.settled.canonical+=' ');mutate(r=>r.stages.settled.history.undo++);mutate(r=>r.stages.settled.preview=true);
  mutate(r=>r.stages.settled.preparation.ownerIds=['A','B','C']);mutate(r=>r.stages.settled.preparation.handles.find(h=>h.nodeKey==='1,1').fixed=false);
  mutate(r=>r.stages.replacementSelected.selection.primaryKey='territorial:entity:B');mutate(r=>r.stages.replacementEntered.selection.primaryKey='territorial:entity:A');
  mutate(r=>delete r.stages.oldCompleted);mutate(r=>r.stages.replacementEntered.transportSequence=r.stages.settled.transportSequence);
  mutate(r=>r.transport.find(e=>e.kind==='held-result').message.requestId++);mutate(r=>r.transport.find(e=>e.kind==='released-result').message.result={});
  mutate(r=>r.transport.find(e=>e.kind==='public-action'&&e.action==='replace-selection').primary='B');
  mutate(r=>r.transport.find(e=>e.kind==='public-action'&&e.action==='cancel').action='generation-change');
  mutate(r=>r.heldRequest.requestId++);mutate(r=>r.transport.splice(r.transport.findIndex(e=>e.kind==='response'),1));
  mutate(r=>r.limits.workerCPUExecutionPhase=true);
  mutate(r=>{for(const e of r.transport)if(e.message?.jobKey)delete e.message.jobKey;});
  mutate(r=>{for(const e of r.transport)if(e.message?.type==='result')e.message.geometryRevision=999;});
  mutate(r=>r.stages.settled.preparation.current='true');mutate(r=>delete r.stages.settled.selection.keys);mutate(r=>r.stages.settled.selection.items[0].key='generic:entity:A');
  mutate(r=>{for(const e of r.transport)if(['rebase','execute'].includes(e.message?.type)){delete e.message.dataRevision;delete e.message.geometryRevision;}});
 }}finally{loaded.cleanup();}
});
