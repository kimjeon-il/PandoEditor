import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {verifyBoundaryTimingCI} from './browser-runner.mjs';
const commit='a'.repeat(40);
test('browser runner rejects non-CI and mismatched checkout before output or launch',()=>{
 for(const env of [{},{GITHUB_ACTIONS:'true'},{GITHUB_ACTIONS:'true',GITHUB_SHA:commit,GITHUB_RUN_ID:'1'}])assert.throws(()=>verifyBoundaryTimingCI(env,'b'.repeat(40)));
 verifyBoundaryTimingCI({GITHUB_ACTIONS:'true',GITHUB_SHA:commit,GITHUB_RUN_ID:'1'},commit);
 const directory=fs.mkdtempSync(path.join(os.tmpdir(),'m977-boundary-no-browser-')),output=path.join(directory,'must-not-exist');
 try{const env={...process.env};delete env.GITHUB_ACTIONS;const result=spawnSync(process.execPath,['tools/m977-boundary-adoption/browser-runner.mjs',output],{env,encoding:'utf8'});assert.notEqual(result.status,0);assert.match(result.stderr,/exact-commit CI/);assert.equal(fs.existsSync(output),false);}finally{fs.rmSync(directory,{recursive:true});}
});

test('CI must reject harness bytes absent from exact HEAD even with clean tracked diff',async()=>{
 const {verifyExactTrackedHarness}=await import('./browser-runner.mjs');
 const directory=fs.mkdtempSync(path.join(os.tmpdir(),'m977-boundary-tracked-'));
 const git=args=>{const r=spawnSync('git',args,{cwd:directory,encoding:'utf8'});assert.equal(r.status,0,r.stderr);return r.stdout;};
 try{
  git(['init','-q']);git(['config','user.name','Test']);git(['config','user.email','test@example.invalid']);fs.writeFileSync(path.join(directory,'tracked.mjs'),'export const value=1;\n');git(['add','tracked.mjs']);git(['commit','-qm','fixture']);
  const {sha256}=await import('../m974-snap-boundary/sources.mjs');const known=sha256(fs.readFileSync(path.join(directory,'tracked.mjs')));
  verifyExactTrackedHarness(directory,{'tracked.mjs':known},directory);
  fs.writeFileSync(path.join(directory,'untracked.mjs'),'export const value=2;\n');assert.equal(git(['diff','HEAD','--']),'');assert.throws(()=>verifyExactTrackedHarness(directory,{'untracked.mjs':sha256(fs.readFileSync(path.join(directory,'untracked.mjs')))},directory));
  assert.throws(()=>verifyExactTrackedHarness(directory,{'tracked.mjs':'0'.repeat(64)},directory));
 }finally{fs.rmSync(directory,{recursive:true});}
});
