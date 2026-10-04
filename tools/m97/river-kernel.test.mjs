import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {spawnSync} from 'node:child_process';
import {root,verifySources,loadOracle,syntheticCases,assertBehavior} from './river/oracle.mjs';
import {createPage} from './river/browser/page.mjs';
import {verifyRiverCheckout} from './river/checkout-test.mjs';
test('river original sources, exact defaults and 18 real-algorithm synthetic cases',async()=>{
  verifySources();const oracle=await loadOracle(),cases=syntheticCases();assert.equal(cases.length,18);
  assert.equal(oracle.module.RIVER_TERRITORY_PARTITION_ALGORITHM_REVISION,'river-partitions-v2');
  assert.deepEqual(oracle.module.RIVER_TERRITORY_PARTITION_CONFIG,{minRiverEdgeM:10,riverEndpointSnapM:50,nodeMergeToleranceM:.5,boundaryCoincidenceToleranceM:5,spatialGridCellM:25000,minCandidateAreaM2:1000,coverageToleranceM2:1000});
  assertBehavior(cases.map(oracle.observe),cases);
});
test('six-expression syntax bridge, immutable resources and cosine closure regenerate exactly',()=>{
  const run=spawnSync('python3',[path.join(root,'tools/m97/river/test-adapters.py')],{encoding:'utf8'});
  assert.equal(run.status,0,run.stdout+run.stderr);
});
test('official browser gate is pinned, original-source only and parses without launching',()=>{
  const browser=path.join(root,'tools/m97/river/browser'),pkg=JSON.parse(fs.readFileSync(path.join(browser,'package.json'))),lock=JSON.parse(fs.readFileSync(path.join(browser,'package-lock.json')));
  assert.equal(pkg.devDependencies['@playwright/test'],'1.62.1');
  for(const name of ['@playwright/test','playwright','playwright-core']){assert.equal(lock.packages['node_modules/'+name].version,'1.62.1');assert.ok(lock.packages['node_modules/'+name].resolved.startsWith('https://registry.npmjs.org/'));assert.match(lock.packages['node_modules/'+name].integrity,/^sha512-/);}
  const html=createPage({cases:[],expected:{},modules:{}}),script=html.match(/<script type="module">([\s\S]*)<\/script>/)[1];
  new vm.Script('(async()=>{'+script+'})');
  assert.ok(script.includes('SOURCE_HASH_MISMATCH'));assert.ok(!script.includes('__riverNativeMath'));
  const workflow=fs.readFileSync(path.join(root,'.github/workflows/m97-editing-parity.yml'),'utf8');
  assert.ok(workflow.includes('river-chromium-oracle:'));assert.ok(workflow.includes('river/browser/run.mjs'));assert.ok(workflow.includes('npm ci --prefix tools/m97/river/browser --ignore-scripts --registry=https://registry.npmjs.org'));
  const cmake=fs.readFileSync(path.join(root,'app/CMakeLists.txt'),'utf8');for(const name of ['river_partition_tests','m972_river_synthetic_differential','m972_river_full_source_differential','m972_river_oracle_contract'])assert.ok(cmake.includes('add_test(NAME '+name),name);
});

test('required full-source gate fails instead of skipping absent data',()=>{
  const env={...process.env};delete env.PANDOEDITOR_HYDRO_FULL_MANIFEST;
  const run=spawnSync(process.execPath,[path.join(root,'tools/m97/river/differential.mjs'),'--native',process.execPath,'--full'],{encoding:'utf8',env});
  assert.notEqual(run.status,0);assert.match(run.stderr,/Required PANDOEDITOR_HYDRO_FULL_MANIFEST missing/);
});

test('byte-pinned river resources survive a clean core.autocrlf=true checkout',()=>{
  verifySources();assert.ok(verifyRiverCheckout(root)>=20);
});
