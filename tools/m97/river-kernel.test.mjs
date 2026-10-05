import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {spawnSync} from 'node:child_process';
import {root,verifySources,loadOracle,syntheticCases,assertBehavior} from './river/oracle.mjs';
import {createPage} from './river/browser/page.mjs';
import {verifyRiverCheckout} from './river/checkout-test.mjs';
// Deliberately synthetic protocol rows; never persisted as browser evidence.
function protocolControllerRows(scenarios){
  const geometry={type:'Polygon',coordinates:[[[0,0],[1,0],[0,1],[0,0]]]};
  return scenarios.map(selection=>{
    const component={key:selection.donorId+':protocol-only',countryId:selection.donorId,geometry};
    const components=selection.samplePoints.map((_,i)=>({...component,key:component.key+'-'+i}));
    return {name:selection.name,selection,expectedTransferAreaKm2:1,components,componentFeatures:[{id:selection.donorId,geometry:{type:'MultiPolygon',coordinates:[geometry.coordinates]}}],selectedCells:structuredClone(components),
      input:{operation:'annex',targetId:selection.targetId,donorIds:[selection.donorId],transferredGeometry:geometry,riverSliverContext:[{donorId:selection.donorId,polygonIndex:0,unselectedGeometries:[]}]},
      combinedGeometry:geometry,result:{transferredGeometry:geometry,autoIncludedSlivers:{count:0,areaM2:0},affectedIds:[selection.donorId,selection.targetId],removedIds:[]},after:[{id:selection.donorId,geometry},{id:selection.targetId,geometry}],
      sourceDiagnostics:{loadedRivers:1,failedLogicalIds:[],indexSha256:'a'.repeat(64),version:'0.13.1'},donorRevisionStrings:[selection.donorId+':'+JSON.stringify([geometry.coordinates])],editedRiverSignature:'',hydroRevision:'0.13.1:'+ 'a'.repeat(64)+':',inputUnchanged:true,fullWorldFeatureCount:258};
  });
}
// Protocol scaffolding only; coordinates here are not browser/native output.
function protocolLifecycleRows(scenarios){
  const row=protocolControllerRows(scenarios)[0],parts=row.components.map((component,index)=>({id:'part-'+index,method:'components',geometry:structuredClone(component.geometry),component:{...structuredClone(component),snapshotId:'snapshot'}}));
  const checkpoints=Array.from({length:7},(_,index)=>{
    const previewReady=[0,1,6].includes(index),componentFeatures=structuredClone(row.componentFeatures);
    componentFeatures[0].geometry.coordinates[0][0][0][0]+=index===0?0:index<3?1:2;
    return {name:index===0?'initial-three-selected':scenarios[0].actions[index-1].name,componentKind:[1,3].includes(index)?'unavailable':index===4?'ordinary':'river',components:structuredClone(row.components),componentFeatures,
      parts:structuredClone(index===0?[]:index<3?parts:[parts[0],parts[2]]),componentSnapshots:index===0?[]:[{id:'snapshot',items:structuredClone(row.components)}],
      selectedComponentKeys:index===0?row.components.map(component=>component.key):index===6?[row.components[1].key]:[],combinedGeometry:row.combinedGeometry,workingSourceGeometry:componentFeatures[0].geometry,
      previewReady,input:previewReady?structuredClone(row.input):null,result:previewReady?structuredClone(row.result):null,transferredGeometry:previewReady?row.result.transferredGeometry:null,riverSliverContext:previewReady?row.input.riverSliverContext:null,autoIncludedSliverCount:previewReady?0:null,autoIncludedSliverAreaM2:previewReady?0:null,transferAreaKm2:previewReady?1:null,inputUnchanged:true};
  });
  return [{...row,initialCheckpoint:checkpoints[0],lifecycleActions:scenarios[0].actions.map((action,index)=>({...action,...([2,5].includes(index)?{removedPart:structuredClone(parts[1])}:{}),...(index===5?{resolvedPoint:scenarios[0].samplePoints[1]}:{}),checkpoint:checkpoints[index+1]}))}];
}
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
  const {controllerAnnexRole,requiredControllerAnnexScenarios,requiredControllerLifecycleScenarios}=await import('./river/controller-suite.mjs');
  const payload={protocolScaffoldingOnly:true,commit,runId,identity,cases:names.map(name=>({name})),annex:{role:annexRole,scenarios:requiredAnnexScenarios()},controllerAnnex:{role:controllerAnnexRole,scenarios:requiredControllerAnnexScenarios()},controllerLifecycle:{scenarios:requiredControllerLifecycleScenarios()},controllerSources:{baseBehavioralCommit:'53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47',behavioralCommit:'12cd8c8ec47c83cfb8c650e8f44c81cdfac10043'}};
  const geometry=()=>({type:'Polygon',coordinates:[[[0,0],[2,0],[0,2],[0,0]]]});
  const native={schema:'river-native-observations-v2',identity:structuredClone(identity),runtime:{binarySha256:'4'.repeat(64),qt:runtimePin.qt},
    observations:names.map(name=>({name,result:{diagnostics:{computeMs:0},candidates:[{geometry:geometry()}]}})),
    presentations:names.map(name=>({name,candidates:[{geometry:geometry()}]})),
    traces:names.map(name=>({name,center:[0,0]}))};
  const browser={...structuredClone(native),schema:'river-browser-observations-v2',
    controllerAnnex:protocolControllerRows(payload.controllerAnnex.scenarios),controllerLifecycle:protocolLifecycleRows(payload.controllerLifecycle.scenarios),controllerSourceCommit:payload.controllerSources.baseBehavioralCommit,controllerBehavioralCommit:payload.controllerSources.behavioralCommit,
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
  for(const mutate of [r=>delete r.controllerLifecycle,r=>r.controllerLifecycle[0].lifecycleActions.pop(),r=>delete r.controllerAnnex,r=>r.controllerAnnex.pop(),r=>r.controllerAnnex[0].selectedCells[0].key='substitute',r=>r.controllerBehavioralCommit='wrong']){const bad=structuredClone(browser);mutate(bad);assert.throws(()=>compare(payload,native,bad),'Controller entry-chain evidence is mandatory');}
  const corruptedSource=structuredClone(browser);corruptedSource.sourceHashes['protocol-source']='6'.repeat(64);
  assert.throws(()=>compare(payload,native,corruptedSource),/Observed browser source hashes exact mismatch/,'Observed source identity');
  const wrongCommit=structuredClone(payload);wrongCommit.commit='7'.repeat(40);
  assert.throws(()=>compare(wrongCommit),{code:'ERR_ASSERTION',actual:wrongCommit.commit,expected:commit},'Requested exact commit');
});

test('controller entry-chain contract is separate, complete, and rejects source or representation substitution',async()=>{
  const {requiredControllerAnnexScenarios,assertControllerAnnexContract,controllerSourceBundle}=await import('./river/controller-suite.mjs');
  const scenarios=requiredControllerAnnexScenarios();
  const valid={scenarios,role:'Actual production controller entry chain; browser preview oracle only.'};
  assertControllerAnnexContract(valid);assert.equal(scenarios.length,2);
  for(const mutate of [r=>r.scenarios.pop(),r=>r.scenarios.reverse(),r=>r.scenarios[0].representation='normalized-filtered-presentation',r=>r.scenarios[0].base='SRB-live']) {
    const bad=structuredClone(valid);mutate(bad);assert.throws(()=>assertControllerAnnexContract(bad),/Required two actual controller entry-chain scenarios/);
  }
  const bundle=controllerSourceBundle();
  assert.equal(bundle.baseBehavioralCommit,'53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47');
  assert.equal(bundle.behavioralCommit,'12cd8c8ec47c83cfb8c650e8f44c81cdfac10043');
  for(const name of ['app-territory-selection-workflow.js','territory-component-plan.js','app-territory-components.js','app-river-candidates.js','river-territory-partition.js','d3.min.js'])assert.ok(bundle.modules[name]?.source,name);
  const {sha256}=await import('./river/oracle.mjs');
  for(const module of Object.values(bundle.modules))assert.equal(sha256(module.source),module.sha256);
  const boundary=vm.runInThisContext(bundle.runtime.source);assert.equal(typeof boundary.createSelectionRuntime,'function');assert.equal(typeof boundary.settle,'function');
});

test('controller entry-chain evidence fails closed for omissions and mutated installed geometry or preview payload',async()=>{
  const {requiredControllerAnnexScenarios,assertControllerAnnexObservations}=await import('./river/controller-suite.mjs');
  // Protocol-only stand-ins, never browser results or geometry goldens.
  const scenarios=requiredControllerAnnexScenarios(),rows=protocolControllerRows(scenarios);
  const contract={scenarios};assertControllerAnnexObservations(contract,rows);
  for(const mutate of [r=>r.pop(),r=>r.reverse(),r=>delete r[0].componentFeatures,r=>r[0].componentFeatures[0].geometry.type='Polygon',r=>r[0].selectedCells[0].key='different',r=>r[0].input.transferredGeometry={type:'Polygon',coordinates:[]},r=>r[0].hydroRevision='::',r=>delete r[0].after,r=>delete r[0].expectedTransferAreaKm2]) {
    const bad=structuredClone(rows);mutate(bad);assert.throws(()=>assertControllerAnnexObservations(contract,bad));
  }
});

test('controller browser boundary drives the real production workflow and preserves prepare MultiPolygon',async()=>{
  // Small Node execution checks wiring only. It is never saved as Chromium evidence.
  const {controllerSourceBundle}=await import('./river/controller-suite.mjs');
  const {prepareCorrectedSelectionSources}=await import('./web-selection-correction.mjs');
  const {loadSelectionModules,seedSelectionFeatures,square}=await import('./web-selection.mjs');
  const corrected=prepareCorrectedSelectionSources();
  try {
    const api=await loadSelectionModules(corrected.root),bundle=controllerSourceBundle();
    Object.assign(api,await import('../../tests/fixtures/web-m97/lifecycle-source/assets/js/modules/ring-hit-test.js'));
    const runtime=vm.runInThisContext(bundle.runtime.source);
    const observe=vm.runInThisContext(fs.readFileSync(path.join(root,'tools/m97/river/browser/controller-annex.js'),'utf8'));
    const features=seedSelectionFeatures(api,{remote:false});
    for(let i=0;i<256;i++)features.push(api.createTerritorialFeature({id:'unrelated-'+i,name:'unrelated',entityKind:'general',parentId:'',coverageMode:'explicit',geometry:square(30+i*.03,40,30+i*.03+.01,40.01)}));
    const river={type:'Feature',id:'fixture-river',properties:{pandolab_id:'fixture-river',category:'river'},geometry:{type:'LineString',coordinates:[[5,-1],[5,11]]}};
    const scenario={name:'protocol-synthetic-entry-chain',base:'donor-controller',representation:'controller-entry-chain',donorId:'donor',targetId:'target',samplePoints:[[1,1]]};
    const payload={world:{source:JSON.stringify({features})},controllerAnnex:{scenarios:[scenario],source:{version:'0.13.1',indexSha256:'a'.repeat(64),inputs:[{donorId:'donor',riverFeatures:[river]}]}}};
    const {sha256,canonical}=await import('./river/oracle.mjs');
    const rows=await observe(payload,api,runtime,async text=>sha256(text),canonical);
    assert.equal(rows.length,1);const row=rows[0];
    assert.equal(row.componentFeatures[0].geometry.type,'MultiPolygon');assert.equal(row.components.length,2);
    assert.equal(row.selectedCells.length,1);assert.ok(row.selectedCells[0].selected);
    assert.equal(row.input.riverSliverContext.length,1);assert.equal(row.sourceDiagnostics.loadedRivers,1);
    assert.equal(row.donorRevisionStrings[0],'donor:'+JSON.stringify(row.componentFeatures[0].geometry.coordinates));
    assert.deepEqual(row.input.transferredGeometry,row.combinedGeometry);assert.equal(row.inputUnchanged,true);
    assert.equal(row.expectedTransferAreaKm2,api.d3.geo.area(row.result.transferredGeometry)*6371.0088**2);
    assert.equal(row.result.autoIncludedSlivers.count,0);assert.equal(row.after.length,2);
    const {requiredControllerLifecycleScenarios}=await import('./river/controller-suite.mjs');
    const lifecycleScenario={...requiredControllerLifecycleScenarios()[0],donorId:'donor',targetId:'target',samplePoints:[[1,1],[4,1],[6,1]]};
    const lifecyclePayload=structuredClone(payload);lifecyclePayload.controllerLifecycle={scenarios:[lifecycleScenario]};
    lifecyclePayload.controllerAnnex.source.inputs[0].riverFeatures=[2.5,5,7.5].map((x,i)=>({...structuredClone(river),id:'synthetic-'+i,properties:{pandolab_id:'synthetic-'+i,category:'river'},geometry:{type:'LineString',coordinates:[[x,-1],[x,11]]}}));
    const lifecycleRows=await observe(lifecyclePayload,api,runtime,async text=>sha256(text),canonical,{lifecycle:true});
    assert.equal(lifecycleRows.length,1);const lifecycle=lifecycleRows[0];
    assert.equal(lifecycle.lifecycleActions?.length,6,'Required actual archive/residual/removal action checkpoints');
    assert.equal(lifecycle.initialCheckpoint.selectedComponentKeys.length,3);
    const checkpoints=lifecycle.lifecycleActions.map(action=>action.checkpoint);
    assert.deepEqual(checkpoints.map(checkpoint=>checkpoint.parts.length),[3,3,2,2,2,2]);
    assert.notDeepEqual(checkpoints[1].componentFeatures,lifecycle.componentFeatures,'Residual source must be prepared by production');
    assert.notDeepEqual(checkpoints[4].componentFeatures,checkpoints[1].componentFeatures,'Removing middle part must restore residual source');
    for(const checkpoint of checkpoints){assert.equal(checkpoint.inputUnchanged,true);if(!checkpoint.previewReady){assert.equal(checkpoint.input,null);assert.equal(checkpoint.result,null);}}
    assert.equal(checkpoints.at(-1).previewReady,true);
    assert.deepEqual(checkpoints[2].parts.map(part=>part.id),[checkpoints[0].parts[0].id,checkpoints[0].parts[2].id]);
    assert.deepEqual(lifecycle.input,checkpoints.at(-1).input);assert.deepEqual(lifecycle.result,checkpoints.at(-1).result);
    const {assertControllerLifecycleObservations}=await import('./river/controller-suite.mjs');
    const lifecycleContract={scenarios:[lifecycleScenario]};assertControllerLifecycleObservations(lifecycleContract,lifecycleRows);
    for(const mutate of [r=>r.pop(),r=>r[0].lifecycleActions.pop(),r=>r[0].lifecycleActions[2].index=0,r=>delete r[0].lifecycleActions[1].checkpoint.componentFeatures,r=>r[0].lifecycleActions[2].checkpoint.parts.reverse(),r=>delete r[0].lifecycleActions[2].checkpoint.componentSnapshots,r=>r[0].lifecycleActions[1].checkpoint.input=r[0].input]){
      const changed=structuredClone(lifecycleRows);mutate(changed);assert.throws(()=>assertControllerLifecycleObservations(lifecycleContract,changed));
    }


  }finally{corrected.cleanup();}
});

test('controller lifecycle contract requires the actual Serbia archive, residual, and middle-removal checkpoints',async()=>{
  const {requiredControllerLifecycleScenarios,assertControllerLifecycleContract}=await import('./river/controller-suite.mjs');
  const scenarios=requiredControllerLifecycleScenarios();
  assert.equal(scenarios.length,1);assert.equal(scenarios[0].donorId,'SRB');
  const contract={scenarios};assertControllerLifecycleContract(contract);
  for(const mutate of [x=>x.scenarios.pop(),x=>x.scenarios[0].actions.pop(),x=>x.scenarios[0].actions.find(a=>a.op==='removePart').index=0]){
    const changed=structuredClone(contract);mutate(changed);assert.throws(()=>assertControllerLifecycleContract(changed),/Required Serbia controller lifecycle/);
  }
});
