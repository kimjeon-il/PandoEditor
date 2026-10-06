import assert from 'node:assert/strict';
import test from 'node:test';
import {loadLifecycleSources} from './sources.mjs';
import {runPendingLifecycleCase} from './runtime.mjs';
import {verifyWebLifecycleCase} from './contract.mjs';
// This fails if pending inputs are retained, any real Finish/Apply/history step is
// omitted, or cancellation/history restore only geometry rather than the model.
test('both public pending-input profiles finish, cancel, retry, apply and replay exact project history',async()=>{
 const loaded=await loadLifecycleSources();
 try{for(const definition of loaded.bundle.corpus.cases){const row=await runPendingLifecycleCase(loaded,definition,loaded.bundle.corpus);verifyWebLifecycleCase(loaded.bundle,definition,row);assert.equal(Object.keys(row.stages).length,29);}}
 finally{loaded.cleanup();}
});
