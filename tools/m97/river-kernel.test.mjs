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
test('six-expression syntax bridge and immutable resources regenerate exactly',()=>{
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
  const cmake=fs.readFileSync(path.join(root,'app/CMakeLists.txt'),'utf8');for(const name of ['river_partition_tests','m972_river_synthetic_browser_baseline','m972_river_full_source_browser_baseline','m972_river_oracle_contract'])assert.ok(cmake.includes('add_test(NAME '+name),name);
});

test('required full-source gate fails instead of skipping absent data',()=>{
  const env={...process.env};delete env.PANDOEDITOR_HYDRO_FULL_MANIFEST;
  const run=spawnSync(process.execPath,[path.join(root,'tools/m97/river/differential.mjs'),'--native',process.execPath,'--full'],{encoding:'utf8',env});
  assert.notEqual(run.status,0);assert.match(run.stderr,/Required PANDOEDITOR_HYDRO_FULL_MANIFEST missing/);
});

test('byte-pinned river resources survive a clean core.autocrlf=true checkout',()=>{
  verifySources();assert.ok(verifyRiverCheckout(root)>=18);
});

test('captured actual Chromium baseline rejects the observed Node-specific output',async()=>{
  const {browserBaseline}=await import('./river/browser-baseline.mjs');
  const {assertRuntimeHash}=await import('./river/compare-browser-native.mjs');
  const baseline=browserBaseline();assert.equal(baseline.capture.run,'37231724016');assert.equal(baseline.runtime.v8,'15.1.206.8');
  const different=baseline.cases.filter(c=>c.nodeOutputSha256!==c.outputSha256);assert.equal(different.length,6);
  for(const c of different)assert.throws(()=>assertRuntimeHash(c.nodeOutputSha256,c.outputSha256,c.name),/exact runtime output/);
});
test('18 stress cases are deterministic bounded inputs, not output goldens',async()=>{
  const {stressCases}=await import('./river/stress-cases.mjs');
  const synthetic=syntheticCases(),sample=synthetic[0];
  // Protocol-only stand-ins let this construction test remain offline. Actual
  // full-source input bytes are required and pinned by createSuite in CI.
  const names=['SRB-raw','SRB-live','HRV-raw','HRV-live','MDA-raw','MDA-live'];
  const base=[...synthetic,...names.map(name=>({...structuredClone(sample),name}))];
  const a=stressCases(base),b=stressCases(base);assert.deepEqual(a,b);assert.equal(a.length,18);assert.equal(new Set(a.map(c=>c.name)).size,18);
  assert.equal(a.filter(c=>c.name.startsWith('next-latitude-')).length,3);assert.equal(a.filter(c=>c.name.startsWith('node-gap-')).length,3);assert.ok(a.every(c=>!('expectedOutput' in c)));
});
test('direct comparator rejects missing native evidence and truncated runtime reports',async()=>{
  const {assertCompleteRows}=await import('./river/compare-browser-native.mjs');
  // Protocol scaffolding only, never a browser geometry golden.
  const names=Array.from({length:42},(_,i)=>'protocol-'+i);
  const report=()=>Object.fromEntries(['observations','presentations','traces'].map(field=>[field,names.map(name=>({name}))]));
  const valid=report();assertCompleteRows(names,valid,report());
  assert.throws(()=>assertCompleteRows(names,null,valid),/Missing compiled native/);
  for(const count of [18,24,41]){const incomplete=report();incomplete.observations.length=count;assert.throws(()=>assertCompleteRows(names,valid,incomplete),/Missing browser observations/);}
  const missing=report();delete missing.presentations;assert.throws(()=>assertCompleteRows(names,valid,missing),/Missing browser presentations/);
  const reordered=report();reordered.traces.reverse();assert.throws(()=>assertCompleteRows(names,valid,reordered),/ordered traces identity/);
});

test('fixed four-scenario annex contract rejects omitted or mislabeled raw/live coverage',async()=>{
  const {annexRole,requiredAnnexScenarios,assertAnnexContract}=await import('./river/suite.mjs');
  const valid={role:annexRole,scenarios:requiredAnnexScenarios()};assert.equal(valid.scenarios.length,4);assertAnnexContract(valid);
  const omitted=structuredClone(valid);omitted.scenarios=[];assert.throws(()=>assertAnnexContract(omitted),/Required four raw\/live annex scenarios/);
  const removed=structuredClone(valid);removed.scenarios.pop();assert.throws(()=>assertAnnexContract(removed),/Required four raw\/live annex scenarios/);
  for(const [field,value] of [['name','different'],['base','MDA-raw'],['representation','raw-upstream-fixture']]) {
    const mislabeled=structuredClone(valid);mislabeled.scenarios[2][field]=value;assert.throws(()=>assertAnnexContract(mislabeled),/Required four raw\/live annex scenarios/);
  }
});

test('compareBrowserNative rejects corrupted protocol scaffolding through its public entry point',async()=>{
  const {registerHooks}=await import('node:module');
  const {runtimePin,annexRole,requiredAnnexScenarios}=await import('./river/suite.mjs');
  const comparatorUrl=new URL('./river/compare-browser-native.mjs?protocol-scaffolding',import.meta.url).href;
  const suiteUrl=new URL('./river/suite.mjs',import.meta.url).href;
  // This is deliberately NOT native/browser output or a geometry golden. The
  // full-country bytes cannot be fabricated for verifySuite's cryptographic
  // checks. Isolate only that full-data prerequisite; import the real comparator
  // unchanged, retaining its equality, runtime, source, commit and row checks.
  // Real verifySuite and actual browser/native evidence remain mandatory in CI.
  const suiteScaffolding='data:text/javascript,'+encodeURIComponent(`
    import assert from 'node:assert/strict';
    import {assertAnnexContract} from ${JSON.stringify(suiteUrl)};
    export {canonicalHash,runtimePin} from ${JSON.stringify(suiteUrl)};
    export function verifySuite(payload) {
      assert.equal(payload.protocolScaffoldingOnly,true,'Explicit protocol scaffolding required');
      assertAnnexContract(payload.annex);
    }
  `);
  const hook=registerHooks({resolve(specifier,context,nextResolve){
    if(context.parentURL===comparatorUrl&&specifier==='./suite.mjs')return {url:suiteScaffolding,shortCircuit:true};
    return nextResolve(specifier,context);
  }});
  let compareBrowserNative;
  try {({compareBrowserNative}=await import(comparatorUrl));} finally {hook.deregister();}
  const commit='1'.repeat(40),runId='offline-protocol-scaffolding';
  const names=Array.from({length:42},(_,i)=>'protocol-'+i);
  const identity={commit,runId,sourceHashes:{'protocol-source':'2'.repeat(64)},boundaryHashes:{'protocol-boundary':'3'.repeat(64)}};
  const payload={protocolScaffoldingOnly:true,commit,runId,identity,cases:names.map(name=>({name})),annex:{role:annexRole,scenarios:requiredAnnexScenarios()}};
  const geometry=()=>({type:'Polygon',coordinates:[[[0,0],[2,0],[0,2],[0,0]]]});
  const native={schema:'river-native-observations-v2',identity:structuredClone(identity),runtime:{binarySha256:'4'.repeat(64),qt:runtimePin.qt},
    observations:names.map(name=>({name,result:{diagnostics:{computeMs:0},candidates:[{geometry:geometry()}]}})),
    presentations:names.map(name=>({name,candidates:[{geometry:geometry()}]})),
    traces:names.map(name=>({name,center:[0,0]}))};
  const browser={...structuredClone(native),schema:'river-browser-observations-v2',
    sourceHashes:structuredClone(identity.sourceHashes),boundaryHashes:structuredClone(identity.boundaryHashes),
    runtime:{playwright:runtimePin.playwright,browserVersion:runtimePin.chromium,chromiumRevision:runtimePin.revision,cdp:{jsVersion:runtimePin.v8,product:'HeadlessChrome/'+runtimePin.chromium}},
    branchObservations:Array.from({length:3},()=>({outputExact:true,segments:[{protocolScaffoldingOnly:true}]})),
    annex:payload.annex.scenarios.map(({name})=>({name,inputUnchanged:true,fullWorldFeatureCount:258,selectedCells:[{key:'protocol-only'}],result:{autoIncludedSlivers:[]}}))};
  const compare=(p=payload,n=native,b=browser)=>compareBrowserNative(p,n,b,{commit,runId});
  assert.equal(compare().state,'passed','Self-consistent scaffolding exercises the complete comparator return path');
  for(const side of ['native','browser']) {
    const corruptedRaw=structuredClone(side==='native'?native:browser);
    corruptedRaw.observations[0].result.candidates[0].geometry.coordinates[0][1][0]+=.25;
    assert.throws(()=>compare(payload,side==='native'?corruptedRaw:native,side==='browser'?corruptedRaw:browser),/protocol-0\/observations exact runtime output/,side+' raw coordinate');
    const corruptedPresentation=structuredClone(side==='native'?native:browser);
    corruptedPresentation.presentations[0].candidates[0].geometry.coordinates[0][1][0]+=.25;
    assert.throws(()=>compare(payload,side==='native'?corruptedPresentation:native,side==='browser'?corruptedPresentation:browser),/protocol-0\/presentations exact runtime output/,side+' presentation coordinate');
    const corruptedCommit=structuredClone(side==='native'?native:browser);corruptedCommit.identity.commit='5'.repeat(40);
    assert.throws(()=>compare(payload,side==='native'?corruptedCommit:native,side==='browser'?corruptedCommit:browser),/exact-commit\/input\/source identity exact mismatch/,side+' commit identity');
    for(const field of ['observations','presentations','traces']) {
      const missing=structuredClone(side==='native'?native:browser);missing[field].pop();
      assert.throws(()=>compare(payload,side==='native'?missing:native,side==='browser'?missing:browser),new RegExp('Missing '+side+' '+field),side+' missing '+field+' row');
    }
  }
  const corruptedSource=structuredClone(browser);corruptedSource.sourceHashes['protocol-source']='6'.repeat(64);
  assert.throws(()=>compare(payload,native,corruptedSource),/Observed browser source hashes exact mismatch/,'Observed source identity');
  const wrongCommit=structuredClone(payload);wrongCommit.commit='7'.repeat(40);
  assert.throws(()=>compare(wrongCommit),{code:'ERR_ASSERTION',actual:wrongCommit.commit,expected:commit},'Requested exact commit');
});
