import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {readSourceHistorySources,verifySourceHistorySources} from '../m974-snap-boundary/source-history-sources.mjs';
import {sourceHistoryRuntimeSource,runtimePin} from '../m974-snap-boundary/source-history-suite.mjs';
import {readInputs} from '../m974-snap-boundary/protocol.mjs';
import {extractBoundaryCallbacks} from '../m974-snap-boundary/boundary-runtime.mjs';
import {sha256} from '../m974-snap-boundary/sources.mjs';
import {boundarySessionReplacementCases,runBoundarySessionReplacementCase,verifyBoundarySessionReplacementCase,sessionReplacementLimits} from './runtime.mjs';
export {runtimePin};
export function sessionReplacementRuntimeSource(){return `(()=>{${extractBoundaryCallbacks.toString()};${runBoundarySessionReplacementCase.toString()};return {runBoundarySessionReplacementCase};})()`;}
const hashRuntime=source=>({source,sha256:sha256(source)}),directory=fileURLToPath(new URL('./',import.meta.url)),root=path.resolve(directory,'../..');
// Bind the complete transitive local executable import graph, including the
// legacy raw-input verifier used by gate.mjs. A new deep import cannot silently
// escape provenance because its bytes become a required committed HEAD blob.
export function sessionReplacementHarnessHashes(harnessDirectory=directory,repositoryRoot=root){
 const pending=['runtime.mjs','suite.mjs','browser-runner.mjs','node-runner.mjs','compare.mjs','gate.mjs','../../tests/m977_boundary_session_replacement_probe.cpp','../../app/CMakeLists.txt','../../.github/workflows/m97-editing-parity.yml','../m974-snap-boundary/browser-runner.mjs','../m974-snap-boundary/worker-host.mjs','../m97/river/browser/package.json','../m97/river/browser/package-lock.json'].map(name=>path.resolve(harnessDirectory,name)),seen=new Map();
 while(pending.length){const file=pending.pop();if(seen.has(file))continue;const relative=path.relative(repositoryRoot,file);assert.ok(relative&&!relative.startsWith('..')&&!path.isAbsolute(relative),'Harness dependency stays inside repository');const bytes=fs.readFileSync(file);seen.set(file,sha256(bytes));
  if(file.endsWith('.mjs'))for(const match of bytes.toString().matchAll(/(?:from\s*|import\s*(?:\(\s*)?|new\s+URL\(\s*)['"](\.{1,2}\/[A-Za-z0-9_./-]+\.mjs)['"]/g))pending.push(path.resolve(path.dirname(file),match[1]));
  if(/\.(cpp|h)$/.test(file))for(const match of bytes.toString().matchAll(/^#include\s+"([A-Za-z0-9_./-]+)"/gm)){const dependency=path.resolve(path.dirname(file),match[1]);if(fs.existsSync(dependency))pending.push(dependency);}
 }
 return Object.fromEntries([...seen].map(([file,digest])=>[path.relative(harnessDirectory,file).split(path.sep).join('/'),digest]).sort(([a],[b])=>a.localeCompare(b)));
}
const legacyInputIdentity=()=>{const {manifest,inputs}=readInputs(),row=manifest.files.boundary;assert.equal(inputs.boundary.length,33);return {file:row.file,sha256:row.sha256,bytes:row.bytes,orderedCaseIds:row.orderedCaseIds,inputManifestSha256:sha256(JSON.stringify(manifest))};};
const identity=s=>({schema:s.schema,version:s.version,commit:s.commit,runId:s.runId,runtimePin:s.runtimePin,sourceManifestSha256:sha256(JSON.stringify(s.manifest)),sourcesSha256:sha256(JSON.stringify(s.sources)),inputSha256:sha256(JSON.stringify(s.cases)),runtimeSha256:s.runtime.sha256,sessionRuntimeSha256:s.sessionRuntime.sha256,harnessFiles:s.harnessFiles,legacyInputIdentity:s.legacyInputIdentity,orderedCases:s.cases.map(row=>row.id),limits:sessionReplacementLimits});
const exact=(object,keys,label)=>{assert.ok(object&&typeof object==='object'&&!Array.isArray(object),label);assert.deepEqual(Object.keys(object).sort(),[...keys].sort(),label+' exact fields');};
export function createSessionReplacementSuite({commit,runId}){
 const suite={schema:'pando-m977-boundary-session-replacement-suite',version:1,commit,runId,runtimePin,...readSourceHistorySources(),legacyInputIdentity:legacyInputIdentity(),cases:boundarySessionReplacementCases(),runtime:hashRuntime(sourceHistoryRuntimeSource()),sessionRuntime:hashRuntime(sessionReplacementRuntimeSource()),harnessFiles:sessionReplacementHarnessHashes()};suite.identity=identity(suite);return verifySessionReplacementSuite(suite);
}
export function verifySessionReplacementSuite(s){
 exact(s,['schema','version','commit','runId','runtimePin','manifest','sources','legacyInputIdentity','cases','runtime','sessionRuntime','harnessFiles','identity'],'suite');assert.equal(s.schema,'pando-m977-boundary-session-replacement-suite');assert.equal(s.version,1);assert.match(s.commit||'',/^[a-f0-9]{40}$/);assert.match(s.runId||'',/^\d+$/);assert.deepEqual(s.runtimePin,runtimePin);
 verifySourceHistorySources(s);assert.deepEqual(s.cases,boundarySessionReplacementCases());assert.deepEqual(s.legacyInputIdentity,legacyInputIdentity());assert.deepEqual(s.runtime,hashRuntime(sourceHistoryRuntimeSource()));assert.deepEqual(s.sessionRuntime,hashRuntime(sessionReplacementRuntimeSource()));assert.deepEqual(s.harnessFiles,sessionReplacementHarnessHashes());assert.deepEqual(s.identity,identity(s));return s;
}
export function verifySessionReplacementReport(suite,report){
 verifySessionReplacementSuite(suite);exact(report,['schema','version','state','identity','sourceHashes','runtime','cases','limits'],'report');assert.equal(report.schema,'pando-m977-boundary-session-replacement-web');assert.equal(report.version,1);assert.equal(report.state,'complete');assert.deepEqual(report.identity,suite.identity);assert.deepEqual(report.limits,sessionReplacementLimits);assert.deepEqual(report.sourceHashes,Object.fromEntries(Object.entries(suite.sources).map(([name,source])=>[name,sha256(source)])));
 const r=report.runtime;exact(r,['userAgent','node','document','worker','playwright','browserVersion','chromiumRevision','cdp','ci'],'runtime');assert.equal(r.node,false);assert.equal(r.document,true);assert.equal(r.worker,true);assert.ok(r.userAgent.includes('Chrome/'+runtimePin.chromium));assert.equal(r.playwright,runtimePin.playwright);assert.equal(r.browserVersion,runtimePin.chromium);assert.equal(r.chromiumRevision,runtimePin.revision);assert.equal(r.cdp.jsVersion,runtimePin.v8);assert.ok(['Chrome/','HeadlessChrome/'].some(prefix=>r.cdp.product===prefix+runtimePin.chromium));assert.deepEqual(r.ci,{commit:suite.commit,runId:suite.runId});
 assert.deepEqual(report.cases.map(row=>row.case),suite.cases.map(row=>row.id));const source=suite.sources['assets/js/modules/app-domain-assembly.js'],start=source.indexOf('        beginBoundaryGesture: event => {'),end=source.indexOf('        renderPacket: () => {');assert.ok(start>=0&&end>start);const extraction={sourcePath:'assets/js/modules/app-domain-assembly.js',names:['beginBoundaryGesture','moveBoundaryGesture','commitBoundaryGesture'],sourceSha256:sha256(source),sha256:sha256(source.slice(start,end)),byteStart:Buffer.byteLength(source.slice(0,start)),byteEnd:Buffer.byteLength(source.slice(0,end))};
 for(const [i,definition]of suite.cases.entries()){verifyBoundarySessionReplacementCase(definition,report.cases[i]);assert.deepEqual(report.cases[i].extractionEvidence,extraction,'exact production callback span');}return report;
}
export function captureBinding(suiteText,reportText){const suite=JSON.parse(suiteText),report=verifySessionReplacementReport(suite,JSON.parse(reportText));return {schema:'pando-m977-boundary-session-replacement-capture-binding',version:1,identity:suite.identity,suiteSha256:sha256(suiteText),suiteBytes:Buffer.byteLength(suiteText),reportSha256:sha256(reportText),reportBytes:Buffer.byteLength(reportText),cases:report.cases.length,rawParity:false};}
export function verifyCapture(suiteText,reportText,binding){assert.deepEqual(binding,captureBinding(suiteText,reportText));return JSON.parse(reportText);}
