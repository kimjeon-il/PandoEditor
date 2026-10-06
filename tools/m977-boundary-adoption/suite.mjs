import assert from 'node:assert/strict';
import fs from 'node:fs';
import {readSourceHistorySources,verifySourceHistorySources} from '../m974-snap-boundary/source-history-sources.mjs';
import {sourceHistoryRuntimeSource,runtimePin} from '../m974-snap-boundary/source-history-suite.mjs';
import {extractBoundaryCallbacks} from '../m974-snap-boundary/boundary-runtime.mjs';
import {sha256} from '../m974-snap-boundary/sources.mjs';
import {boundaryTimingCases,runBoundaryTimingCase,verifyBoundaryTimingCase} from './runtime.mjs';
export {runtimePin};
export const boundaryTimingLimits=Object.freeze({rawParity:false,fullDOM:false,pointerPixels:false,gpuRendering:false,fullCanonicalProject:false,workerCPUExecutionPhase:false,nativeRenderAdoptionInterval:false,nativeBoundaryMoveWorkerInterval:false,renderAdoptionPending:true,selectionDivergenceUnresolved:true});
export function timingRuntimeSource(){return `(()=>{${extractBoundaryCallbacks.toString()};${runBoundaryTimingCase.toString()};return {runBoundaryTimingCase};})()`;}
const hashRuntime=source=>({source,sha256:sha256(source)});
const harnessFiles=()=>Object.fromEntries(['runtime.mjs','suite.mjs','browser-runner.mjs','native-probe.cpp','compare.mjs','compare-browser.mjs',
 '../m974-snap-boundary/source-history-suite.mjs','../m974-snap-boundary/source-history-sources.mjs','../m974-snap-boundary/source-history-runtime.mjs','../m974-snap-boundary/source-history-selection.mjs','../m974-snap-boundary/sources.mjs','../m974-snap-boundary/boundary-runtime.mjs',
 '../m97/web-lifecycle.mjs','../m97/web-selection.mjs','../m97/river/suite.mjs','../m97/river/browser/package.json','../m97/river/browser/package-lock.json','../m97/river/browser/report-transfer.mjs','../../tests/m974_boundary_probe.cpp',
].map(name=>[name,sha256(fs.readFileSync(new URL(name,import.meta.url)))]));
const identity=suite=>({schema:suite.schema,version:suite.version,commit:suite.commit,runId:suite.runId,runtimePin:suite.runtimePin,
 sourceManifestSha256:sha256(JSON.stringify(suite.manifest)),sourcesSha256:sha256(JSON.stringify(suite.sources)),inputSha256:sha256(JSON.stringify(suite.cases)),runtimeSha256:suite.runtime.sha256,timingRuntimeSha256:suite.timingRuntime.sha256,harnessFiles:suite.harnessFiles,orderedCases:suite.cases.map(row=>row.id),limits:boundaryTimingLimits});
const exact=(object,keys,label)=>assert.deepEqual(Object.keys(object).sort(),[...keys].sort(),label+' exact fields');
export function createBoundaryTimingSuite({commit,runId}){
 const suite={schema:'pando-m977-boundary-timing-suite',version:1,commit,runId,runtimePin,...readSourceHistorySources(),cases:boundaryTimingCases(),runtime:hashRuntime(sourceHistoryRuntimeSource()),timingRuntime:hashRuntime(timingRuntimeSource()),harnessFiles:harnessFiles()};
 suite.identity=identity(suite);return verifyBoundaryTimingSuite(suite);
}
export function verifyBoundaryTimingSuite(suite){
 exact(suite,['schema','version','commit','runId','runtimePin','manifest','sources','cases','runtime','timingRuntime','harnessFiles','identity'],'suite');
 assert.equal(suite.schema,'pando-m977-boundary-timing-suite');assert.equal(suite.version,1);assert.match(suite.commit||'',/^[a-f0-9]{40}$/);assert.match(suite.runId||'',/^\d+$/);assert.deepEqual(suite.runtimePin,runtimePin);
 verifySourceHistorySources(suite);assert.deepEqual(suite.cases,boundaryTimingCases());assert.equal(suite.cases.length,9);
 assert.deepEqual(suite.runtime,hashRuntime(sourceHistoryRuntimeSource()));assert.deepEqual(suite.timingRuntime,hashRuntime(timingRuntimeSource()));assert.deepEqual(suite.harnessFiles,harnessFiles());assert.deepEqual(suite.identity,identity(suite));return suite;
}
export function verifyBoundaryTimingReport(suite,report){
 verifyBoundaryTimingSuite(suite);exact(report,['schema','version','state','identity','sourceHashes','runtime','cases','limits'],'report');
 assert.equal(report.schema,'pando-m977-actual-chromium-boundary-timing');assert.equal(report.version,1);assert.equal(report.state,'complete');assert.deepEqual(report.identity,suite.identity);assert.deepEqual(report.limits,boundaryTimingLimits);
 assert.deepEqual(report.sourceHashes,Object.fromEntries(Object.entries(suite.sources).map(([name,source])=>[name,sha256(source)])));
 const r=report.runtime;exact(r,['userAgent','node','document','worker','playwright','browserVersion','chromiumRevision','cdp','ci'],'runtime');
 assert.equal(r.node,false);assert.equal(r.document,true);assert.equal(r.worker,true);assert.ok(r.userAgent.includes('Chrome/'+runtimePin.chromium));assert.equal(r.playwright,runtimePin.playwright);assert.equal(r.browserVersion,runtimePin.chromium);assert.equal(r.chromiumRevision,runtimePin.revision);assert.equal(r.cdp.jsVersion,runtimePin.v8);assert.ok(['Chrome/','HeadlessChrome/'].some(prefix=>r.cdp.product===prefix+runtimePin.chromium));assert.deepEqual(r.ci,{commit:suite.commit,runId:suite.runId});
 assert.deepEqual(report.cases.map(row=>row.case),suite.cases.map(row=>row.id));
 const source=suite.sources['assets/js/modules/app-domain-assembly.js'],start=source.indexOf('        beginBoundaryGesture: event => {'),end=source.indexOf('        renderPacket: () => {'),callbacks=source.slice(start,end);
 const extraction={sourcePath:'assets/js/modules/app-domain-assembly.js',names:['beginBoundaryGesture','moveBoundaryGesture','commitBoundaryGesture'],sourceSha256:sha256(source),sha256:sha256(callbacks),byteStart:Buffer.byteLength(source.slice(0,start)),byteEnd:Buffer.byteLength(source.slice(0,end))};
 for(const [i,definition]of suite.cases.entries()){verifyBoundaryTimingCase(definition,report.cases[i]);assert.deepEqual(report.cases[i].extractionEvidence,extraction,'exact committed production gesture callbacks');}return report;
}
export function captureBinding(suiteText,reportText){const suite=JSON.parse(suiteText),report=verifyBoundaryTimingReport(suite,JSON.parse(reportText));return {schema:'pando-m977-boundary-timing-capture-binding',version:1,identity:suite.identity,suiteSha256:sha256(suiteText),suiteBytes:Buffer.byteLength(suiteText),reportSha256:sha256(reportText),reportBytes:Buffer.byteLength(reportText),cases:report.cases.length,rawParity:false};}
export function verifyCapture(suiteText,reportText,binding){assert.deepEqual(binding,captureBinding(suiteText,reportText));return JSON.parse(reportText);}
