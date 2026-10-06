import test from 'node:test';
import assert from 'node:assert/strict';
import {createBoundaryTimingSuite,verifyBoundaryTimingSuite,verifyBoundaryTimingReport,timingRuntimeSource} from './suite.mjs';
import {createBoundaryTimingPage} from './browser-runner.mjs';
import {sha256} from '../m974-snap-boundary/sources.mjs';
import {readSourceHistorySources} from '../m974-snap-boundary/source-history-sources.mjs';
const commit='1'.repeat(40),make=()=>createBoundaryTimingSuite({commit,runId:'123'});
test('suite preserves every committed source manifest and original source byte',()=>{
 const suite=make(),original=readSourceHistorySources();assert.deepEqual(suite.manifest,original.manifest);assert.deepEqual(suite.sources,original.sources);assert.equal(suite.cases.length,9);verifyBoundaryTimingSuite(suite);
 const functions=(0,eval)(timingRuntimeSource());assert.equal(typeof functions.runBoundaryTimingCase,'function');const html=createBoundaryTimingPage(suite);assert.ok(html.includes('__m977BoundaryReport'));assert.ok(!html.includes('node:worker_threads'));
});
test('suite rejects source repins, altered runtime, omissions and stale harness evidence',()=>{
 const suite=make();for(const mutate of [
  s=>s.sources[s.manifest.sources[0].path]+='\n',s=>s.manifest.behavioralCommit='0'.repeat(40),s=>s.cases.pop(),s=>s.cases.reverse(),s=>s.cases[3].features[0].ring[0][0]=90,
  s=>s.timingRuntime.source+=' ',s=>s.runtime.sha256='0'.repeat(64),s=>s.harnessFiles['native-probe.cpp']='0'.repeat(64),s=>s.identity.limits.rawParity=true,s=>s.identity.commit='0'.repeat(40),
 ]){const bad=structuredClone(suite);mutate(bad);assert.throws(()=>verifyBoundaryTimingSuite(bad));}
});
test('Node receipt cannot be admitted as a browser result',()=>{
 const suite=make();assert.throws(()=>verifyBoundaryTimingReport(suite,{schema:'pando-m977-node-boundary-timing',version:1,authoritativeBrowserObservation:false,cases:[]}));
 assert.equal(suite.identity.timingRuntimeSha256,sha256(timingRuntimeSource()));
});
