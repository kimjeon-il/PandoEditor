import {test} from 'node:test';
import assert from 'node:assert/strict';
import {summarize} from './summarize-native-performance.mjs';
import * as measurement from './summarize-native-performance.mjs';
const names=['Idle','Flat pan','Flat zoom','Globe rotation','Globe zoom','Flat ↔ Globe switch','Hover','Selection',
  'Dense labels pan/zoom','Hydro dense viewport','DEM LOD zoom-in','DEM LOD zoom-out',
  'Terrain + labels + hydro 동시 pan','Menu/panel UI','Large project navigation'];
const ids=['idle','flat-pan','flat-zoom','globe-rotation','globe-zoom','projection-switch','hover','selection',
  'dense-labels','hydro-dense','dem-zoom-in','dem-zoom-out','combined-pan','menu-panel','large-navigation'];
const counts=[0,120,90,120,90,20,40,18,120,120,90,90,120,20,120];
const fixture=()=>{
  const metrics={sceneFullBuildCount:{unit:'count',kind:'cumulative',resetDomain:'project',supported:true},
    geometryUploads:{unit:'count',kind:'cumulative',resetDomain:'window',supported:true},
    uploadedBytes:{unit:'bytes',kind:'cumulative',resetDomain:'window',supported:true},
    'editing.snapQueryMs':{unit:'ms',kind:'distribution',resetDomain:'project',supported:false}};
  const state=(n)=>({viewRevision:n,sceneRevision:3,selectionRevision:1,selectedId:'DEU',projection:'flat',menuVisible:false});
  const result={schema:'pandoeditor-native-performance',version:2,measurementMode:'diagnostic',diagnosticReason:'synthetic schema mechanism test',
    completed:true,warmupMs:60000,repeatMs:720000,logicalProcessors:24,cpuNormalization:'process-time/all-active-logical-processors',metrics,
    provenance:{sourceCommit:'a'.repeat(40),binarySha256:'b'.repeat(64),fixtureManifestSha256:'c'.repeat(64),buildType:'Release',
      qtVersion:'6.8.3',os:'Windows',graphicsApi:'Direct3D11',runId:'synthetic-parser-fixture',testFixture:true},
    preflight:{status:'PASS',missing:[],placeDatasetVersion:'production-present'},
    fixtures:['world-standard','dense-view','editing-heavy','large-project'].map(id=>({id,manifestSha256:'d'.repeat(64),fullStack:true})),
    scenarios:ids.map((id,i)=>({id,name:names[i],fixtureId:i===14?'large-project':i>=8&&i<=9||i===12?'dense-view':'world-standard',
      status:'completed',startMs:i?90000+(i-1)*5000:60000,endMs:i?95000+(i-1)*5000:90000,expectedFinalState:state(i+2),actualFinalState:state(i+2)})),
    samples:Array.from({length:800},(_,i)=>({elapsedMs:i*1000,scenarioId:i<60?'warmup':i<90?'idle':ids[1+Math.floor((i-90)/5)]??'long-run',exposed:true,gpuReady:true,mapWidth:1920,mapHeight:929,
      activeDownloads:0,queuedDownloads:0,pendingJobs:0,cpuPercent:.5,cpuRawPercent:12,logicalProcessors:24,
      generations:{project:1,window:1},values:{sceneFullBuildCount:2,geometryUploads:4,uploadedBytes:1024,'editing.snapQueryMs':null}})),
    inputs:ids.flatMap((id,i)=>Array.from({length:counts[i]},(_,j)=>({scenarioId:id,inputSequence:i*1000+j+1,frameSequence:i*1000+j+1,
      startedNs:(90000+(i-1)*5000+j*20)*1e6,synchronizedNs:(90000+(i-1)*5000+j*20+17)*1e6,presentedNs:(90000+(i-1)*5000+j*20+18)*1e6,latencyMs:18,actionMs:1,
      delivered:true,stateChanged:true,matched:true,renderSnapshotMatched:true,renderUploadsPending:false,renderOwner:{windowGeneration:1,resourceGeneration:1,bridgeGeneration:1},expectedState:state(j+2),renderState:state(j+2)}))),
    presentationFrames:ids.flatMap((id,j)=>Array.from({length:8},(_,i)=>({scenarioId:id,frameSequence:i+1,elapsedMs:(j?90000+(j-1)*5000:60000)+1000+i*17,ms:17}))),
    heartbeatEvents:[{elapsedMs:61000,ms:20}],cycles:Array.from({length:12},(_,i)=>({index:i,startMs:60000+i*60000,endMs:120000+i*60000,
      settled:true,pendingJobs:0,stalePublications:0,returnState:{projection:'flat',centerLongitude:0,centerLatitude:0,rotationLongitude:0,rotationLatitude:0,rotationRoll:0,scale:300,translateX:960,translateY:464.5,selectedId:'',selectedObjects:[],editTargetId:'',menuVisible:false,hasPreparedPreview:false,geometryEditActive:false,contentEditActive:false},resourceCaches:{terrain:{residentBytes:10,budgetBytes:20}}}))};
  result.rawInputEvents=result.inputs.map(r=>({inputSequence:r.inputSequence,scenarioId:r.scenarioId,startedNs:r.startedNs,type:'pointer',x:960,y:464}));
  return result;
};
const has=(report,code)=>report.faults.some(f=>f.startsWith(`[${code}]`));
test('valid synthetic v2 parser fixture can never become acceptance evidence',()=>{
  const r=summarize(fixture());assert.equal(r.status,'DIAGNOSTIC');assert.deepEqual(r.faults,[]);
  assert.equal(r.acceptedBy,null);assert.equal(r.acceptanceEligible,false);assert.equal(r.scenarios.length,15);assert.equal(r.scenarios[1].inputP95Ms,18);
});
test('missing values remain null and unsupported editing is not zero work',()=>{
  assert.equal(summarize(fixture()).metricDeltas['editing.snapQueryMs'].delta,null);
  const d=fixture();delete d.samples[2].values.geometryUploads;assert.ok(has(summarize(d),'MISSING_METRIC'));
});
test('units and normalized CPU provenance match semantics',()=>{
  const d=fixture();d.metrics.uploadedBytes.unit='ms';assert.ok(has(summarize(d),'UNIT'));
  const e=fixture();e.samples[70].cpuPercent=12;assert.ok(has(summarize(e),'CPU_NORMALIZATION'));
});
test('counter resets are explicit and deltas never invent negative work',()=>{
  const d=fixture();d.samples[75].values.geometryUploads=1;assert.ok(has(summarize(d),'COUNTER_RESET'));
  const e=fixture();for(let i=75;i<e.samples.length;i++){e.samples[i].generations.window=2;e.samples[i].values.geometryUploads=1;}
  const r=summarize(e);assert.equal(r.metricDeltas.geometryUploads.resetCount,1);assert.equal(r.metricDeltas.geometryUploads.delta,null);
});
test('all original scenarios, exact final states and correlated inputs are required',()=>{
  for(const mutate of [d=>d.scenarios.pop(),d=>d.scenarios[2].name='other',d=>d.scenarios[1].actualFinalState.viewRevision++,
    d=>d.inputs[0].renderState.viewRevision++,d=>d.inputs[0].matched=false,d=>d.inputs[0].stateChanged=false]){
    const d=fixture();mutate(d);const r=summarize(d);assert.equal(r.status,'FAIL');assert.ok(r.faults.some(f=>/^\[(SCENARIO|FINAL_STATE|INPUT_STATE)/.test(f)));
  }
});
test('stalls stay in distributions; empty frames and short long-run cannot pass',()=>{
  const d=fixture();d.inputs.filter(s=>s.scenarioId==='flat-pan').slice(0,10).forEach(s=>{s.latencyMs=1200;s.presentedNs=s.startedNs+1200e6;});
  const r=summarize(d);assert.equal(r.scenarios[1].inputP95Ms,1200);assert.ok(has(r,'STALL'));
  const e=fixture();e.presentationFrames=[];assert.ok(has(summarize(e),'FRAMES'));
  const f=fixture();f.repeatMs=30000;assert.ok(has(summarize(f),'LONG_RUN'));
});
test('empty production place blocks acceptance; malformed provenance fails',()=>{
  const d=fixture();d.measurementMode='acceptance';d.provenance.testFixture=false;
  d.preflight={status:'BLOCKED',missing:['production place runtime'],placeDatasetVersion:'empty-v1'};assert.equal(summarize(d).status,'BLOCKED');
  const e=fixture();e.provenance.binarySha256='bad';assert.ok(has(summarize(e),'PROVENANCE'));
});
test('legacy v1 is explicit and never upgraded to v2 acceptance',()=>{
  const r=summarize({schema:'pandoeditor-native-performance',version:1,samples:[],inputs:[],phaseNames:['idle']});
  assert.equal(r.status,'LEGACY_DIAGNOSTIC');assert.equal(r.sourceVersion,1);assert.equal(r.acceptanceEligible,false);
});
test('empty and nested wrong final state cannot authenticate a scenario',()=>{
 const d=fixture();d.scenarios[3].expectedFinalState={};d.scenarios[3].actualFinalState={};assert.ok(has(summarize(d),'FINAL_STATE'));
 const e=fixture();e.inputs[0].expectedState.editing={pending:false};e.inputs[0].renderState.editing={pending:true};assert.ok(has(summarize(e),'INPUT_STATE'));
});
test('cache plateau and missing pending work are required for long-run acceptance',()=>{
 const d=fixture();d.cycles.forEach((c,i)=>c.resourceCaches.terrain.residentBytes=i*10);assert.ok(has(summarize(d),'CACHE_PLATEAU'));
 const e=fixture();e.samples[70].pendingJobs=null;assert.ok(has(summarize(e),'MISSING_METRIC'));
});
test('settled return cycles must prove the same complete camera and interaction state',()=>{
 const d=fixture();delete d.cycles[1].returnState;assert.ok(has(summarize(d),'RETURN_STATE'));
 const e=fixture();e.cycles[2].returnState.scale=301;assert.ok(has(summarize(e),'RETURN_STATE'));
 const f=fixture();f.cycles.forEach(c=>delete c.returnState.selectedObjects);assert.ok(has(summarize(f),'RETURN_STATE'));
});
test('counter generation and input/frame identity cannot be missing or duplicated',()=>{
 const d=fixture();delete d.samples[3].generations.window;assert.ok(has(summarize(d),'COUNTER_RESET'));
 const e=fixture();e.inputs[1].inputSequence=e.inputs[0].inputSequence;assert.ok(has(summarize(e),'INPUT_STATE'));
});
test('three-run aggregation pins source/binary/fixture/device and preserves every failed run',()=>{
 assert.equal(typeof measurement.summarizeRuns,'function');
 const reports=Array.from({length:3},(_,i)=>{const d=fixture();d.provenance.runId=`synthetic-${i}`;return d;});
 const r=measurement.summarizeRuns(reports);assert.equal(r.status,'DIAGNOSTIC');assert.equal(r.runs.length,3);assert.equal(r.acceptedBy,null);assert.equal(r.acceptanceEligible,false);
 const bad=structuredClone(reports);bad[2].provenance.binarySha256='e'.repeat(64);assert.ok(has(measurement.summarizeRuns(bad),'PROVENANCE'));
 assert.ok(has(measurement.summarizeRuns(reports.slice(0,2)),'RUNS'));
});
test('supported but unobserved nullable metrics remain null, missing keys still fail',()=>{
 const d=fixture();d.metrics['editing.snapQueryMs'].supported=true;d.metrics['editing.snapQueryMs'].nullable=true;
 const r=summarize(d);assert.deepEqual(r.faults,[]);assert.equal(r.metricDeltas['editing.snapQueryMs'].delta,null);
 delete d.samples[3].values['editing.snapQueryMs'];assert.ok(has(summarize(d),'MISSING_METRIC'));
});
test('actual raw input and committed render ownership are required; event gaps are preserved failures',()=>{
 const d=fixture();d.rawInputEvents=[];assert.ok(has(summarize(d),'INPUT_STATE'));
 const e=fixture();e.inputs[0].renderUploadsPending=true;assert.ok(has(summarize(e),'INPUT_STATE'));
 const f=fixture();f.inputs[0].renderSnapshotMatched=false;assert.ok(has(summarize(f),'INPUT_STATE'));
 const g=fixture();g.editingEvents=[{eventSerial:1},{eventSerial:3}];assert.ok(has(summarize(g),'EVENT_GAP'));
});
test('scenario distributions use measured intervals and record metric deltas without crossing reset epochs',()=>{
 const d=fixture();for(let i=93;i<d.samples.length;i++)d.samples[i].values.geometryUploads=8;
 assert.equal(summarize(d).scenarios[1].metricDeltas.geometryUploads.delta,4);
 const e=fixture();e.presentationFrames[10].elapsedMs=1;assert.ok(has(summarize(e),'FRAMES'));
 const f=fixture();f.inputs[0].startedNs=1;assert.ok(has(summarize(f),'INPUT_STATE'));
 const g=fixture();g.samples[70].activeDownloads=1;assert.ok(has(summarize(g),'DOWNLOAD'));
});
