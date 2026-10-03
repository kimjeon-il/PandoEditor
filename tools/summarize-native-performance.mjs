// Real-window measurements only. A missing/unexposed/short run cannot pass.
import {readFileSync, writeFileSync} from 'node:fs';
import {pathToFileURL} from 'node:url';

const percentile=(values,p)=>{
  const sorted=[...values].sort((a,b)=>a-b);
  return sorted.length?sorted[Math.ceil(sorted.length*p)-1]:null;
};
export function summarize(data) {
  if(data.schema!=='pandoeditor-native-performance')throw Error('Unexpected measurement schema');
  const samples=data.samples??[], inputs=data.inputs??[], frames=data.presentationFrames??[];
  const idle=samples.filter(s=>s.phase===0&&s.elapsedMs>=30000&&s.elapsedMs<=61000);
  const faults=[];
  const check=(ok,message)=>{if(!ok)faults.push(message);};
  check(!data.ablation&&!data.brief,'Ablation/brief diagnostic is not acceptance evidence');
  check(data.warmupMs>=60000,'Acceptance requires at least 60 seconds of warm-up');
  check(idle.length>=28&&idle.at(-1)?.elapsedMs-idle[0]?.elapsedMs>=28000,'Insufficient settled idle coverage');
  check(samples.length>0&&samples.every(s=>s.exposed&&s.mapWidth===1920&&s.mapHeight===929)&&idle.every(s=>s.gpuReady),
    'Native GPU window must remain exposed at 1920x929 throughout');
  check(idle.every(s=>!s.startupBusy&&!s.uploadsPending&&!s.viewportResourcePending&&!s.activeDownloads&&!s.queuedDownloads), 'Idle contains loading/pending uploads');
  const counters=['sceneFullBuildCount','scenePreparationCount','sceneGraphRebuildCount','geometryUploads','uploadedBytes','uploadContinuationCount','cpuPaintCount','viewRevision'];
  check(idle.every(s=>counters.every(key=>Number.isFinite(s[key]))),'Required idle counters are missing');
  const idleDeltas=Object.fromEntries(counters.map(key=>[key,idle.length&&idle[0][key]!==undefined?idle.at(-1)[key]-idle[0][key]:null]));
  for(const [key,delta] of Object.entries(idleDeltas))if(delta!==null)check(delta===0,`Idle ${key} increased by ${delta}`);
  for(const key of counters)if(idle[0]?.[key]!==undefined)
    check(idle.every(s=>s[key]===idle[0][key]),`Idle ${key} changed within the interval`);
  const cpu=idle.length?idle.reduce((sum,s)=>sum+s.cpuPercent,0)/idle.length:null;
  check(cpu!==null&&cpu<=2,'Idle CPU exceeds 2%');
  const scenarios=data.phaseNames.slice(1).map((name,i)=>{
    const phase=i+1, latency=inputs.filter(s=>s.phase===phase), presented=frames.filter(s=>s.phase===phase);
    const row={name,inputs:latency.length,inputP95Ms:percentile(latency.map(s=>s.ms),.95),
      inputMaxMs:latency.length?Math.max(...latency.map(s=>s.ms)):null,
      frames:presented.length,frameP95Ms:percentile(presented.map(s=>s.ms),.95)};
    check(latency.length>=[120,90,40,18,20][i]&&latency.every(s=>s.presented&&s.ms>=0),`${name}: insufficient/unpresented inputs`);
    check(row.inputP95Ms!==null&&row.inputP95Ms<=100,`${name}: input p95 exceeds 100ms`);
    check(row.inputMaxMs!==null&&row.inputMaxMs<500,`${name}: stall >=500ms`);
    if(phase<=2)check(row.frames>=5&&row.frameP95Ms<=33,`${name}: continuous frame p95 exceeds 33ms`);
    return row;
  });
  for(const phase of [1,2]) {
    const changes=new Set(inputs.filter(s=>s.phase===phase).map(s=>s.viewRevision));
    if(inputs.some(s=>s.viewRevision!==undefined))check(changes.size>=2,`Phase ${phase}: input did not change the camera`);
  }
  check(inputs.some(s=>s.phase===4&&s.selectedId),'Selection inputs never selected an object');
  if(inputs.some(s=>s.menuVisible!==undefined))
    check(inputs.some(s=>s.phase===5&&s.menuVisible)&&inputs.some(s=>s.phase===5&&!s.menuVisible),'Menu never opened and closed');
  const settledBeats=(data.heartbeatEvents??[]).filter(s=>s.elapsedMs>=30000);
  const stalls=settledBeats.filter(s=>s.ms>=500);
  check(stalls.length===0,'UI heartbeat stalled >=500ms after warm-up');
  return {schema:'pandoeditor-native-performance-assessment',version:1,
    status:faults.length?'FAIL':'PASS',recovery:data.recovery,idleCpuPercent:cpu,idleDeltas,
    scenarios,uiStalls:stalls,faults,
    limitations:['Qt pointer/wheel events and real frame presentation; not OS remote-control timing',
      'CPU time is process-wide, normalized by logical CPU count; GPU frame intervals are presentation intervals, not timestamp-query GPU durations']};
}
if(process.argv[1]&&import.meta.url===pathToFileURL(process.argv[1]).href) {
  try {
    const input=process.argv[2];if(!input)throw Error('Usage: node summarize-native-performance.mjs report.json [summary.json]');
    const result=summarize(JSON.parse(readFileSync(input,'utf8')));
    if(process.argv[3])writeFileSync(process.argv[3],JSON.stringify(result,null,2)+'\n');
    console.log(JSON.stringify(result,null,2));
    if(result.status!=='PASS')process.exitCode=1;
  }catch(error){console.error(error.message);process.exitCode=2;}
}
