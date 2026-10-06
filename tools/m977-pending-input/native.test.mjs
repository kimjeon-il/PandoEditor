import assert from 'node:assert/strict';
import test from 'node:test';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {readPendingSources} from './sources.mjs';
const probe=process.env.PANDO_M977_NATIVE_PROBE;
const bundle=readPendingSources();
let report;
export function collectNative(){
 if(report)return structuredClone(report);
 assert.ok(probe,'PANDO_M977_NATIVE_PROBE must name the actual compiled public-controller probe');
 const run=spawnSync(probe,[fileURLToPath(new URL('../../tests/fixtures/web-m977-pending-input/corpus.json',import.meta.url))],{encoding:'utf8',timeout:120000,maxBuffer:8*1024*1024,env:{...process.env,QT_QPA_PLATFORM:'offscreen',QT_QUICK_BACKEND:'software'}});
 assert.ifError(run.error);assert.equal(run.status,0,run.stderr);report=JSON.parse(run.stdout);return structuredClone(report);
}
test('actual native stimulus uses every declared tap index across both profiles',{skip:!probe},()=>{
 for(const row of collectNative().cases){
  assert.equal(row.error,undefined);assert.deepEqual(row.input,bundle.corpus.cases.find(c=>c.id===row.case));
  for(const tap of row.inputObservations){assert.deepEqual(tap.intended,bundle.corpus.points[bundle.corpus.tapPointIndexes[tap.stage]],row.case+' '+tap.stage);}
 }
});
test('native reports the actual public affine projection parameters',{skip:!probe},()=>{
 for(const row of collectNative().cases){
  assert.equal(typeof row.projectionParameters,'object','public projection parameters required');
  for(const field of ['cosLatitude','minX','maxLatitude'])assert.ok(Number.isFinite(row.projectionParameters[field]),field);
  assert.ok(row.projectionParameters.cosLatitude>0);
 }
});
test('native pending and ready observations preserve complete byte-bound evidence',{skip:!probe},async()=>{
 const {verifyNativeReport}=await import('./compare.mjs');const native=collectNative();assert.equal(verifyNativeReport(bundle,native),native);
 for(const mutate of [n=>n.cases.pop(),n=>n.runtime.qt='6.9.0',n=>n.corpusSha256='0'.repeat(64),n=>n.cases[0].error='failed',n=>delete n.cases[0].stages.activation,n=>n.cases[0].stages.ready.canonicalBytesBase64+='AA==',n=>n.cases[0].baseline.canonicalSha256='0'.repeat(64),n=>n.cases[0].stages.ready.revision++,n=>n.cases[0].inputObservations[0].intended=[4,4],n=>n.cases[0].deliveryBarrier.globalPoolCompleted=false,n=>n.cases[0].stages.cancelSwitch={observed:true},n=>n.cases[1].stages.ready={observed:false,reason:'missing'}]){
  const bad=structuredClone(native);mutate(bad);assert.throws(()=>verifyNativeReport(bundle,bad));
 }
});

test('native public observations reject detached duplicate evidence',{skip:!probe},async t=>{
 const {verifyNativeReport}=await import('./compare.mjs');const native=collectNative();
 const mutations=[
  ['polygon mechanism falsely claims private coordinates',n=>{n.cases[1].stages.firstReady.coordinateObservation.mechanism='raw private storage';}],
  ['line mechanism falsely claims projected coordinates',n=>{n.cases[1].stages.confirmSwitch.coordinateObservation.mechanism='geometryDraftPaths.vertices + MapProjection.unproject';}],
  ['raw flag contradicts active line method',n=>{const s=n.cases[1].stages.confirmSwitch;s.coordinateObservation.raw=false;s.coordinateObservation.roundTripCanLosePrecision=true;}],
  ['projected vertices detached from public paths',n=>{n.cases[1].stages.firstReady.coordinateObservation.projectedVertices=[[999,999]];}],
  ['public path vertices detached from observation',n=>{n.cases[1].stages.firstReady.draftPaths[0].vertices[0].x=999;}],
  ['all observed signal states missing',n=>{n.cases[0].events=[{sequence:0,state:{active:false}}];}],
  ['public signal order reversed',n=>{n.cases[1].events.reverse();n.cases[1].events.forEach((e,i)=>{e.sequence=i;});}],
  ['public projection parameters missing',n=>{delete n.cases[1].projectionParameters;}],
  ['zero projection scale',n=>{n.cases[1].projectionParameters={cosLatitude:0,minX:0,maxLatitude:10};}],
  ['nonfinite projection parameter',n=>{n.cases[1].projectionParameters={cosLatitude:1,minX:Infinity,maxLatitude:10};}],
  ['input map coordinate detached from intent',n=>{n.cases[1].inputObservations[1].mapCoordinate[0]++;}],
  ['input inverse detached from map coordinate',n=>{const tap=n.cases[1].inputObservations[0];tap.inverseBeforeInput[0]++;tap.exactInputRoundTrip=false;}],
  ['reported polygon coordinates detached from projected paths',n=>{for(const s of Object.values(n.cases[1].stages))if(s.observed&&s.coordinates?.length)s.coordinates=s.coordinates.map((_,i)=>[99+i,88+i]);}],
  ['internally consistent draft detached from accepted input',n=>{
   const row=n.cases[1],{cosLatitude,minX,maxLatitude}=row.projectionParameters;
   for(const stage of Object.values(row.stages))if(stage.observed&&stage.coordinates?.length){
    stage.draftPaths[0].vertices[0].x+=1;
    stage.coordinateObservation.projectedVertices=stage.draftPaths.flatMap(path=>path.vertices.map(v=>[v.x,v.y]));
    stage.coordinates=stage.coordinateObservation.projectedVertices.map(p=>[(p[0]+minX)/cosLatitude,maxLatitude-p[1]]);
   }
  }],
  ['Redo public paths no longer preserve snapshot',n=>{n.cases[1].stages.redoDraft.draftPaths[0].path+=' ';}],
  ['switch public paths no longer preserve snapshot',n=>{n.cases[1].stages.switch.draftPaths[0].path+=' ';}],
  ['same-method public paths no longer preserve snapshot',n=>{n.cases[2].stages.sameMethod.draftPaths[0].path+=' ';}],
 ];
 for(const [name,mutate]of mutations)await t.test(name,()=>{
  const bad=structuredClone(native);mutate(bad);assert.throws(()=>verifyNativeReport(bundle,bad),undefined,name);
 });
});
