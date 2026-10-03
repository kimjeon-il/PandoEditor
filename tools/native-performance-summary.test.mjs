import {test} from 'node:test';
import assert from 'node:assert/strict';
import {summarize} from './summarize-native-performance.mjs';
function fixture(){return {
  schema:'pandoeditor-native-performance',recovery:true,warmupMs:60000,
  phaseNames:['idle','pan','zoom','hover','selection','menu'],
  samples:Array.from({length:31},(_,i)=>({phase:0,elapsedMs:30000+i*1000,exposed:true,gpuReady:true,
    mapWidth:1920,mapHeight:929,startupBusy:false,uploadsPending:false,viewportResourcePending:false,
    cpuPercent:.5,sceneFullBuildCount:2,scenePreparationCount:3,sceneGraphRebuildCount:9,geometryUploads:250,
    uploadedBytes:1024,uploadContinuationCount:8,cpuPaintCount:0,viewRevision:1})),
  inputs:[120,90,40,18,20].flatMap((length,i)=>Array.from({length},(_,j)=>({phase:i+1,ms:18,presented:true,selectedId:i===3?'DEU':'',viewRevision:j+1,menuVisible:i===4&&j%2===0}))),
  presentationFrames:Array.from({length:5},(_,i)=>Array.from({length:6},()=>({phase:i+1,ms:16.7}))).flat(),
  heartbeatEvents:[{elapsedMs:45000,ms:20}]
};}
test('requires native evidence, not just a successful process exit',()=>{
  assert.equal(summarize(fixture()).status,'PASS');
  for(const mutation of [d=>d.samples[10].exposed=false,d=>d.samples[20].geometryUploads++,
    d=>d.samples.at(-1).geometryUploads++,d=>d.inputs[0].presented=false,
    d=>d.heartbeatEvents.push({elapsedMs:50000,ms:900}),d=>d.samples=[],
    d=>d.inputs.forEach(s=>s.selectedId=''),d=>delete d.samples[0].cpuPaintCount,
    d=>d.samples[10].viewRevision++,d=>d.warmupMs=15000]){
    const data=fixture();mutation(data);assert.equal(summarize(data).status,'FAIL');
  }
});
test('500ms+ samples are never filtered out of p95 or stall assessment',()=>{
  const data=fixture();data.inputs.filter(s=>s.phase===1).forEach((s,i)=>{if(i<10)s.ms=1200;});
  const report=summarize(data);
  assert.equal(report.scenarios[0].inputP95Ms,1200);assert.equal(report.status,'FAIL');
});
