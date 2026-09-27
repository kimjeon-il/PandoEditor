#!/usr/bin/env node
// Aggregates actual device samples; no default thresholds or inferred PASS.
import {readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';

function value(flag) {const i=process.argv.indexOf(flag);return i<0?null:process.argv[i+1];}
function percentile(sorted,p) {
  if(!sorted.length)return null;
  const i=(sorted.length-1)*p,lo=Math.floor(i),hi=Math.ceil(i);
  return sorted[lo]+(sorted[hi]-sorted[lo])*(i-lo);
}
try {
  const input=value('--input'),output=value('--out');
  if(!input||!output)throw Error('usage: collect-render-metrics.mjs --input <device-samples.json> --out <summary.json>');
  const data=JSON.parse(readFileSync(resolve(input),'utf8'));
  if(data.schema!=='pandoeditor-device-samples'||data.version!==1||!data.device||
     !Array.isArray(data.scenarios)||!data.scenarios.length)throw Error('invalid device sample schema');
  for(const key of ['identifier','os','cpu','ramBytes','gpu','qtVersion','graphicsApi','resolution','dpr','buildType'])
    if(data.device[key]===undefined||data.device[key]===null||data.device[key]==='')throw Error(`missing device.${key}`);
  const summaries=data.scenarios.map(row=>{
    if(!row.id||!Array.isArray(row.frameTimesMs)||!row.frameTimesMs.length||
       row.frameTimesMs.some(n=>!Number.isFinite(n)||n<0))throw Error(`invalid frames in ${row.id??'scenario'}`);
    const frames=[...row.frameTimesMs].sort((a,b)=>a-b);
    return {id:row.id,sampleCount:frames.length,p50FrameMs:percentile(frames,.5),
      p95FrameMs:percentile(frames,.95),p99FrameMs:percentile(frames,.99),
      longFrameCount:row.longFrameCount??null,
      residentBytes:row.residentBytes??null,gpuResourceBytes:row.gpuResourceBytes??null,
      pickLatencyMs:row.pickLatencyMs??null,scenePatchMs:row.scenePatchMs??null,
      editPrepareMs:row.editPrepareMs??null,editCommitMs:row.editCommitMs??null};
  });
  const summary={schema:'pandoeditor-render-measurements',version:1,
    capturedAt:data.capturedAt??null,device:data.device,datasets:data.datasets??null,
    scenarios:summaries,assessment:'UNASSESSED'};
  writeFileSync(resolve(output),JSON.stringify(summary,null,2)+'\n');
  console.log(`Collected ${summaries.length} measured scenarios; no performance threshold applied`);
}catch(error){console.error(`Metric collection failed: ${error.message}`);process.exitCode=1;}
