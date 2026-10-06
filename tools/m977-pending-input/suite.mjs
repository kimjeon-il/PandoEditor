import assert from 'node:assert/strict';
import fs from 'node:fs';
import {readPendingSources,verifyPendingSources,sha256,pendingProjection} from './sources.mjs';
import {runPendingInputCase,verifyWebCase} from './runtime.mjs';
import {sourceHistoryRuntimeSource} from '../m974-snap-boundary/source-history-suite.mjs';
import {runtimePin as sourceRuntimePin} from '../m97/river/suite.mjs';
export const runtimePin=Object.freeze({...sourceRuntimePin});

export const pendingObservationLimits=Object.freeze({rawParity:false,fullDOM:false,actualTouchDispatch:false,pixelParity:false,gpu:false,fullProjectSerialization:false,projectApplyUndoRedo:false,snapCandidates:false,heldResultsAreWorkerComplete:true,workerCPUExecutionPhase:false,stimulus:'actual handleMapClick -> actual createEditingDomain with actual toolDraftDefinition'});
const harnessFiles=()=>Object.fromEntries([
 'suite.mjs','sources.mjs','runtime.mjs','browser-runner.mjs',
 '../m974-snap-boundary/source-history-suite.mjs','../m974-snap-boundary/source-history-runtime.mjs',
 '../m974-snap-boundary/source-history-selection.mjs','../m974-snap-boundary/source-history-sources.mjs',
 '../m974-snap-boundary/sources.mjs','../m97/web-lifecycle.mjs','../m97/web-selection.mjs',
 '../m97/river/suite.mjs','../m97/river/browser/report-transfer.mjs',
 '../m97/river/browser/package.json','../m97/river/browser/package-lock.json',
 '../../tests/m977_pending_input_probe.cpp','../../app/CMakeLists.txt',
].map(name=>[name,sha256(fs.readFileSync(new URL(name,import.meta.url)))]));
const exact=(object,keys,label)=>{
 assert.ok(object&&typeof object==='object'&&!Array.isArray(object),label);
 assert.deepEqual(Object.keys(object).sort(),[...keys].sort(),label+' exact fields');
};
const runtimeSource=source=>({source,sha256:sha256(source)});
const stageCount=corpus=>corpus.cases.reduce((count,row)=>count+row.stages.length,0);
const identity=suite=>({schema:suite.schema,version:suite.version,commit:suite.commit,runId:suite.runId,
 runtimePin:suite.runtimePin,sourceManifestSha256:sha256(JSON.stringify(suite.manifest)),
 addendumSha256:sha256(JSON.stringify(suite.addendum)),sourceSha256:sha256(JSON.stringify(suite.sources)),
 corpusSha256:suite.addendum.corpusSha256,runtimeSha256:suite.runtime.sha256,pendingRuntimeSha256:suite.pendingRuntime.sha256,
 harnessFiles:suite.harnessFiles,orderedCaseIds:suite.corpus.cases.map(row=>row.id),stageCount:stageCount(suite.corpus),observationLimits:pendingObservationLimits});
const verifyCIIdentity=(commit,runId)=>{
 assert.match(commit||'',/^[a-f0-9]{40}$/,'Exact application commit required');
 assert.equal(typeof runId,'string');assert.match(runId,/^\d+$/,'Exact GitHub run identity required');
};
export async function createPendingSuite({commit,runId}={}){
 verifyCIIdentity(commit,runId);
 const suite={schema:'pando-m977-pending-input-suite',version:1,commit,runId,runtimePin:{...runtimePin},
  ...readPendingSources(),runtime:runtimeSource(sourceHistoryRuntimeSource()),pendingRuntime:runtimeSource(runPendingInputCase.toString()),harnessFiles:harnessFiles()};
 suite.identity=identity(suite);return verifyPendingSuite(suite);
}
export function verifyPendingSuite(suite){
 exact(suite,['schema','version','commit','runId','runtimePin','manifest','sources','addendum','corpus','runtime','pendingRuntime','harnessFiles','identity'],'pending suite');
 assert.equal(suite.schema,'pando-m977-pending-input-suite');assert.equal(suite.version,1);verifyCIIdentity(suite.commit,suite.runId);
 assert.deepEqual(suite.runtimePin,runtimePin);verifyPendingSources(suite);
 assert.equal(suite.corpus.cases.length,14);assert.equal(stageCount(suite.corpus),86);
 assert.deepEqual(suite.runtime,runtimeSource(sourceHistoryRuntimeSource()));assert.deepEqual(suite.pendingRuntime,runtimeSource(runPendingInputCase.toString()));
 assert.deepEqual(suite.harnessFiles,harnessFiles());assert.deepEqual(suite.identity,identity(suite));return suite;
}
export function verifyPendingReport(suite,report){
 verifyPendingSuite(suite);
 exact(report,['schema','version','state','identity','sourceHashes','runtime','contexts','cases','observationLimits'],'browser report');
 assert.equal(report.schema,'pando-m977-actual-chromium-pending-input');assert.equal(report.version,1);assert.equal(report.state,'complete');
 assert.deepEqual(report.identity,suite.identity);assert.deepEqual(report.observationLimits,pendingObservationLimits);
 assert.deepEqual(report.sourceHashes,Object.fromEntries(Object.entries(suite.sources).map(([name,source])=>[name,sha256(source)])));
 const runtime=report.runtime;
 exact(runtime,['collector','playwright','browserVersion','chromiumRevision','cdp','ci'],'actual runtime');
 assert.equal(runtime.collector,'actual-chromium');assert.equal(runtime.playwright,runtimePin.playwright);
 assert.equal(runtime.browserVersion,runtimePin.chromium);assert.equal(runtime.chromiumRevision,runtimePin.revision);
 assert.equal(runtime.cdp?.jsVersion,runtimePin.v8);assert.ok(['Chrome/','HeadlessChrome/'].some(prefix=>runtime.cdp?.product===prefix+runtimePin.chromium));
 assert.deepEqual(runtime.ci,{commit:suite.commit,runId:suite.runId});
 assert.deepEqual(report.contexts.map(row=>row.case),suite.corpus.cases.map(row=>row.id));
 assert.deepEqual(report.cases.map(row=>row.case),suite.corpus.cases.map(row=>row.id));
 for(const [index,definition] of suite.corpus.cases.entries()){
  const context=report.contexts[index];exact(context,['case','viewport','userAgent','devicePixelRatio','hasTouch','node','document','worker'],'browser context');
  assert.deepEqual(context.viewport,{width:definition.profile.width,height:definition.profile.height});
  assert.equal(typeof context.userAgent,'string');assert.ok(context.userAgent.includes('Chrome/'+runtimePin.chromium),'Actual pinned Chromium user agent');
  assert.equal(context.devicePixelRatio,1);assert.equal(context.hasTouch,definition.profile.pointerType==='touch');
  assert.equal(context.node,false);assert.equal(context.document,true);assert.equal(context.worker,true);
  const row=report.cases[index];exact(row,['case','input','stages','inputs','transport','errors','beforeCanonical','limits'],'raw case');
  verifyWebCase(definition,row,suite.corpus,pendingProjection(suite,definition));assert.deepEqual(row.limits,{...pendingObservationLimits,viewport:definition.profile});
  for(const input of row.inputs){
   const point=suite.corpus.points[suite.corpus.tapPointIndexes[input.stage]];
   assert.deepEqual(input.coordinate,point,'Declared input point');
   assert.ok(input.roundTrip.every((value,i)=>Math.abs(value-point[i])<=1e-10),'Public projection round-trip preserves declared point');
  }
 }
 return report;
}
export function createPendingCaptureBinding(suiteText,reportText){
 const suite=JSON.parse(suiteText),report=verifyPendingReport(suite,JSON.parse(reportText));
 return {schema:'pando-m977-pending-input-capture-verification',version:1,identity:suite.identity,passed:true,
  suiteDecompressedSha256:sha256(suiteText),suiteDecompressedBytes:Buffer.byteLength(suiteText),
  reportDecompressedSha256:sha256(reportText),reportDecompressedBytes:Buffer.byteLength(reportText),
  cases:report.cases.length,stages:stageCount(suite.corpus),rawParity:false,observationLimits:pendingObservationLimits};
}
export function verifyPendingCapture(suiteText,reportText,binding){
 assert.deepEqual(binding,createPendingCaptureBinding(suiteText,reportText),'Exact raw capture byte binding');
 return JSON.parse(reportText);
}
