import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
const commit='a'.repeat(40);
test('runner rejects local browser launch before output creation or Playwright import',async()=>{
 assert.ok(fs.existsSync(new URL('./browser-runner.mjs',import.meta.url)),'CI-only replacement collector is missing');
 const {verifySessionReplacementCI}=await import('./browser-runner.mjs');
 for(const env of [{},{GITHUB_ACTIONS:'true'},{GITHUB_ACTIONS:'true',GITHUB_SHA:commit,GITHUB_RUN_ID:'1'}])assert.throws(()=>verifySessionReplacementCI(env,'b'.repeat(40)));
 verifySessionReplacementCI({GITHUB_ACTIONS:'true',GITHUB_SHA:commit,GITHUB_RUN_ID:'1'},commit);
 const directory=fs.mkdtempSync(path.join(os.tmpdir(),'m977-replacement-no-browser-')),output=path.join(directory,'must-not-exist');try{const env={...process.env};delete env.GITHUB_ACTIONS;const result=spawnSync(process.execPath,['tools/m977-boundary-session-replacement/browser-runner.mjs',output],{env,encoding:'utf8'});assert.notEqual(result.status,0);assert.match(result.stderr,/exact-commit CI/);assert.equal(fs.existsSync(output),false);}finally{fs.rmSync(directory,{recursive:true});}
});
test('every executable harness file must match committed HEAD bytes',async()=>{
 const {verifyExactTrackedHarness}=await import('./browser-runner.mjs'),{sha256}=await import('../m974-snap-boundary/sources.mjs');const directory=fs.mkdtempSync(path.join(os.tmpdir(),'m977-replacement-tracked-'));
 const git=args=>{const r=spawnSync('git',args,{cwd:directory,encoding:'utf8'});assert.equal(r.status,0,r.stderr);return r.stdout;};
 try{git(['init','-q']);git(['config','user.name','Test']);git(['config','user.email','test@example.invalid']);fs.writeFileSync(path.join(directory,'tracked.mjs'),'export const x=1;\n');git(['add','tracked.mjs']);git(['commit','-qm','fixture']);const digest=sha256(fs.readFileSync(path.join(directory,'tracked.mjs')));verifyExactTrackedHarness(directory,{'tracked.mjs':digest},directory);fs.writeFileSync(path.join(directory,'untracked.mjs'),'export const x=2;');assert.equal(git(['diff','HEAD','--']),'');assert.throws(()=>verifyExactTrackedHarness(directory,{'untracked.mjs':sha256(fs.readFileSync(path.join(directory,'untracked.mjs')))},directory));fs.appendFileSync(path.join(directory,'tracked.mjs'),' ');assert.throws(()=>verifyExactTrackedHarness(directory,{'tracked.mjs':digest},directory));}finally{fs.rmSync(directory,{recursive:true});}
});
