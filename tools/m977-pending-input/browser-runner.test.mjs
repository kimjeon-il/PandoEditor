import assert from 'node:assert/strict';
import test from 'node:test';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import vm from 'node:vm';
import {webcrypto} from 'node:crypto';
import {spawnSync} from 'node:child_process';
const mod=await import('./browser-runner.mjs').catch(error=>{if(error.code!=='ERR_MODULE_NOT_FOUND')throw error;return {};});
const head='a'.repeat(40),env={GITHUB_ACTIONS:'true',GITHUB_SHA:head,GITHUB_RUN_ID:'123456789'};
test('actual browser execution requires exact CI head and run before side effects',()=>{
 assert.equal(typeof mod.verifyPendingCI,'function','Separate CI-only launch guard required');assert.doesNotThrow(()=>mod.verifyPendingCI(env,head));
 for(const [bad,checkout]of [[{},head],[{...env,GITHUB_ACTIONS:'false'},head],[{...env,GITHUB_SHA:'main'},head],[{...env,GITHUB_RUN_ID:'local'},head],[env,'b'.repeat(40)]])assert.throws(()=>mod.verifyPendingCI(bad,checkout));
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'m977-no-launch-')),output=path.join(dir,'must-not-exist');
 try{const child=spawnSync(process.execPath,[new URL('./browser-runner.mjs',import.meta.url).pathname,output],{env:{...process.env,GITHUB_ACTIONS:'false'},encoding:'utf8',timeout:10000});assert.notEqual(child.status,0);assert.match(child.stderr,/authorized exact-commit CI/);assert.equal(fs.existsSync(output),false);}finally{fs.rmSync(dir,{recursive:true,force:true});}
});
test('row context options match both actual viewport and pointer capability profiles',()=>{
 assert.equal(typeof mod.pendingContextOptions,'function');
 assert.deepEqual(mod.pendingContextOptions({width:1024,height:768,pointerType:'mouse'}),{viewport:{width:1024,height:768},deviceScaleFactor:1,hasTouch:false,isMobile:false});
 assert.deepEqual(mod.pendingContextOptions({width:360,height:800,pointerType:'touch'}),{viewport:{width:360,height:800},deviceScaleFactor:1,hasTouch:true,isMobile:true});
 assert.throws(()=>mod.pendingContextOptions({width:360,height:800,pointerType:'pen'}));
});
test('browser case refuses Node globals before evaluating production modules',async()=>{
 assert.equal(typeof mod.runPendingBrowserCase,'function');await assert.rejects(()=>mod.runPendingBrowserCase({suite:{},caseId:'unused',baseUrl:'http://127.0.0.1/'}),/actual Chromium page/);
});
test('generated page is case-specific and carries a viewport declaration and compressed verified suite',async()=>{
 assert.equal(typeof mod.createPendingPage,'function');const {createPendingSuite}=await import('./suite.mjs'),suite=await createPendingSuite({commit:head,runId:env.GITHUB_RUN_ID});
 const html=mod.createPendingPage(suite,suite.corpus.cases[7].id);assert.match(html,/name="viewport"/);assert.match(html,/DecompressionStream/);assert.match(html,/__m977PendingRow/);assert.match(html,/two-pending-empty-switch-mobile-360/);
 assert.throws(()=>mod.createPendingPage(suite,'unknown'));const bad=structuredClone(suite);bad.runtime.source+=' ';assert.throws(()=>mod.createPendingPage(bad,suite.corpus.cases[0].id));
});
test('raw browser row transfer is byte-exact, bounded and cleans temporary page exports',async()=>{
 assert.equal(typeof mod.exportPendingRow,'function');const dir=fs.mkdtempSync(path.join(os.tmpdir(),'m977-transfer-')),
  row={schema:'unit-transfer-only',state:'complete',unicode:'abc😀한국어'.repeat(100),nested:{raw:[null,true,1.2345678901234567]}},context=vm.createContext({__m977PendingRow:row,TextEncoder,crypto:webcrypto});let largest=0;
 const page={async evaluate(fn,arg){context.__argument=arg;const result=await vm.runInContext('('+fn.toString()+')(__argument)',context);largest=Math.max(largest,JSON.stringify(result)?.length||0);return structuredClone(result);}};
 try{const destination=path.join(dir,'row.json'),receipt=await mod.exportPendingRow(page,destination,{chunkCharacters:64});assert.equal(fs.readFileSync(destination,'utf8'),JSON.stringify(row));assert.ok(receipt.chunks>1);assert.ok(largest<1000);assert.equal(context.__m977PendingExport,undefined);}finally{fs.rmSync(dir,{recursive:true,force:true});}
});
test('missing complete browser evidence cannot produce a raw row artifact',async()=>{
 assert.equal(typeof mod.exportPendingRow,'function');const dir=fs.mkdtempSync(path.join(os.tmpdir(),'m977-transfer-missing-')),context=vm.createContext({TextEncoder,crypto:webcrypto});
 const page={async evaluate(fn,arg){context.__argument=arg;return vm.runInContext('('+fn.toString()+')(__argument)',context);}};
 try{const destination=path.join(dir,'row.json');await assert.rejects(()=>mod.exportPendingRow(page,destination),/Missing complete/);assert.equal(fs.existsSync(destination),false);}finally{fs.rmSync(dir,{recursive:true,force:true});}
});
