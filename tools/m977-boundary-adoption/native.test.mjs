import test from 'node:test';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {boundaryTimingCases,runBoundaryTimingCase} from './runtime.mjs';
import {loadSourceHistoryNodeSources} from '../m974-snap-boundary/source-history-sources.mjs';
import {verifyNativeTiming,compareTimingObservations} from './compare.mjs';
const probe=process.env.M977_BOUNDARY_PROBE;
test('native public-controller timing and truthful comparison', {skip:!probe}, async()=>{
 const cases=boundaryTimingCases(),result=spawnSync(probe,[],{input:JSON.stringify(cases),encoding:'utf8',timeout:180000,maxBuffer:32*1024*1024});assert.equal(result.status,0,result.stderr);const receipt=JSON.parse(result.stdout);verifyNativeTiming(cases,receipt);
 const loaded=await loadSourceHistoryNodeSources();
 try{
  const rows=[];for(const definition of cases)rows.push(await runBoundaryTimingCase(loaded,definition));
  const comparison=compareTimingObservations({cases:rows},receipt);assert.equal(comparison.parityAccepted,false);assert.equal(comparison.rawParity,false);assert.deepEqual(comparison.summary,{cases:9,publicSelectionCases:3,boundaryReadinessDivergences:3,previewDivergences:1,nativeAbsentIntervalCases:6,canonicalOrHistoryMutations:0});
 }finally{loaded.cleanup();}
 for(const mutate of [
  r=>r.rows[0].completionBarrier.workerPoolCompletedBeforeFinalSnapshot=false,
  r=>r.rows[0].stages.settled.state.canonicalBytesBase64+='QQ==',
  r=>r.rows[0].stages.settled.state.documentSha256='0'.repeat(64),
  r=>r.rows[0].stages.settled.state.history.canUndo=true,
  r=>r.rows[0].stages.interval.outcome.workerStarted=true,
  r=>r.rows[3].stages.interval.observed=true,
  r=>r.rows[3].limits.renderAdoptionPending=true,
  r=>r.rows[8].stages.settled.edit.previewReady=true,
  r=>r.rows[2].stages.settled.selectionItems=[],
  r=>r.rows.pop(),
 ]){const bad=structuredClone(receipt);mutate(bad);assert.throws(()=>verifyNativeTiming(cases,bad));}
});
