import test from 'node:test';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {createCaptureSuite,verifyBrowserReport} from './protocol.mjs';
import {createCapturePage} from './browser-runner.mjs';
import {loadNodeSources} from './sources.mjs';
test('portable browser runtime has no missing module closure in snap and canonical boundary workflows',async()=>{
 const suite=await createCaptureSuite({commit:'a'.repeat(40),runId:'protocol-test-only'}),loaded=await loadNodeSources();
 try{
  loaded.runtime=(0,eval)(suite.runtime.lifecycle.source);
  const snap=(0,eval)(suite.runtime.snap.source),boundary=(0,eval)(suite.runtime.boundary.source);
  const snapResult=await snap.runSnapCase(loaded,suite.inputs.snap.find(row=>row.id==='snap-boundary-priority'));
  assert.equal(snapResult.queries[0].ready.indicator.kind,'boundary');
  const result=await boundary.runBoundaryCase(loaded,suite.inputs.boundary.find(row=>row.id==='root-triple'));
  assert.equal(result.stages.confirm.outcome.ok,true);assert.equal(result.stages.undo.outcome.ok,true);assert.equal(result.stages.redo.outcome.ok,true);
  const html=createCapturePage(suite);assert.ok(html.includes('DecompressionStream'));assert.ok(html.includes('__m974Report'));assert.ok(!html.includes('node:'));
 }finally{loaded.cleanup();}
});
test('browser launch outside authorized exact-commit CI is blocked before capture',()=>{
 const result=spawnSync(process.execPath,[fileURLToPath(new URL('./browser-runner.mjs',import.meta.url)),'/tmp/m974-must-not-capture'],{encoding:'utf8',env:{...process.env,GITHUB_ACTIONS:'false'}});
 assert.notEqual(result.status,0);assert.match(result.stderr,/authorized exact-commit CI/);
});
test('an incomplete browser envelope cannot certify omitted observations',async()=>{
 const suite=await createCaptureSuite({commit:'a'.repeat(40),runId:'protocol-test-only'});
 assert.throws(()=>verifyBrowserReport(suite,{schema:'pando-m974-actual-chromium-workflows',version:1,state:'complete',identity:suite.identity,runtime:{},snap:[],boundary:[]}),/AssertionError/);
});
