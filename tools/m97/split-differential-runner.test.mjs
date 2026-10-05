import test from 'node:test';
import assert from 'node:assert/strict';
import {createSplitCaptureSuite} from './split-browser-runner.mjs';
const api=await import('./split-differential-runner.mjs').catch(error=>{if(error.code==='ERR_MODULE_NOT_FOUND')return {};throw error;});
test('split comparison refuses Node-discovery reports and missing actual browser runtime evidence',async()=>{
 assert.equal(typeof api.validateActualSplitCapture,'function','actual browser evidence validator required');const suite=await createSplitCaptureSuite({commit:'a'.repeat(40),runId:'test'});
 assert.throws(()=>api.validateActualSplitCapture(suite,{schema:'pando-web-split-lifecycle-observations'}),/browser|Chromium|schema/i);
 assert.throws(()=>api.validateActualSplitCapture(suite,{schema:'pando-m973-actual-chromium-split',identity:suite.identity}),/runtime|browser/i);
});
test('browser evidence cannot substitute different inputs under the correct ordered case names',async()=>{
 assert.equal(typeof api.validateActualSplitCapture,'function');const suite=await createSplitCaptureSuite({commit:'a'.repeat(40),runId:'input-binding-test'});
 // Protocol-only object, not captured browser observations or a parity fixture.
 const report={schema:'pando-m973-actual-chromium-split',identity:suite.identity,runtime:{playwright:suite.runtimePin.playwright,browserVersion:suite.runtimePin.chromium,chromiumRevision:suite.runtimePin.revision,cdp:{jsVersion:suite.runtimePin.v8}},lifecycle:{schema:'pando-web-split-browser-observations',sourceHashes:suite.identity.sourceHashes,harnessRuntimeSha256:suite.bundle.runtime.sha256,sourceChain:suite.bundle.sourceChain,cases:suite.bundle.cases.map(input=>({case:input.id,input:structuredClone(input)}))},kernel:suite.kernelCases.map(c=>({id:c.id}))};report.lifecycle.cases[0].input.coords[0][0]+=1;
 assert.throws(()=>api.validateActualSplitCapture(suite,report),/input/i);
});
test('a missing capture leaves an explicit failure artifact instead of ambiguous running evidence',async()=>{
 const {mkdtempSync,readFileSync,rmSync}=await import('node:fs'),{tmpdir}=await import('node:os'),{join}=await import('node:path'),{fileURLToPath}=await import('node:url'),{spawnSync}=await import('node:child_process');const root=mkdtempSync(join(tmpdir(),'split-protocol-failure-'));
 try{const result=spawnSync(process.execPath,[fileURLToPath(new URL('./split-differential-runner.mjs',import.meta.url)),join(root,'missing-browser'),join(root,'missing-runtime'),join(root,'evidence')],{encoding:'utf8'});assert.notEqual(result.status,0);let failure='';try{failure=readFileSync(join(root,'evidence','failure.txt'),'utf8');}catch{}assert.match(failure,/ENOENT|missing/i,'failure must be preserved');}finally{rmSync(root,{recursive:true,force:true});}
});
