import assert from 'node:assert/strict';
import test from 'node:test';
import {BOUNDARY_CASE_IDS} from '../m974-native-boundary.mjs';
import * as gate from './gate.mjs';

const expectedDiagnostics={
 'root-stale-preparation':[{field:'settled.outcome.ok',web:false,native:true},{field:'settled.nonEquivalentStimuli.boundaryStatus',web:'error',native:'ready'}],
 'root-stale-move':[{field:'settled.outcome.ok',web:false,native:true},{field:'settled.nonEquivalentStimuli.previewReady',web:false,native:true}],
 'root-stale-preview':[{field:'settled.outcome.ok',web:false,native:true},{field:'settled.nonEquivalentStimuli.previewReady',web:false,native:true}],
};
// Independent synthetic comparison reports exercise gate disposition, never captures.
function reports(){
 const legacy={schema:'pando-m974-native-boundary-comparison',version:1,passed:false,rawParity:false,coverage:{expectedCases:33,browserCases:33,nativeCases:33},differences:[],results:BOUNDARY_CASE_IDS.map(id=>({case:id,passed:!Object.hasOwn(expectedDiagnostics,id),rawParity:false,differences:structuredClone(expectedDiagnostics[id]||[]),unobserved:Object.hasOwn(expectedDiagnostics,id)?[{case:id,scope:'settled selection parity after different stale-response stimuli',reason:'independent synthetic gate fixture'}]:[]}))};
 const session={schema:'pando-m977-boundary-session-replacement-comparison',version:1,passed:true,rawParity:false,parityAccepted:false,cases:['preparation-completed-reenter','preview-completed-reenter'].map(id=>({case:id,passed:true,differences:[]}))};return {legacy,session};
}
test('explicit 30 plus 2 required coverage retains the failed raw 33-case comparison',()=>{
 const {legacy,session}=reports(),r=gate.evaluateMatchedBoundaryCoverage(legacy,session);assert.equal(r.requiredCoveragePassed,true);assert.equal(r.legacyComparison.passed,false);assert.equal(r.rawParity,false);assert.equal(r.fullParityAccepted,false);assert.equal(r.matchedLegacy.caseIds.length,30);assert.equal(r.nonEquivalentDiagnostics.caseIds.length,3);assert.equal(r.sessionReplacement.caseIds.length,2);
});
test('every matched legacy difference still fails required coverage',()=>{
 for(const id of BOUNDARY_CASE_IDS.filter(id=>!Object.hasOwn(expectedDiagnostics,id))){const {legacy,session}=reports(),row=legacy.results.find(r=>r.case===id);row.passed=false;row.differences.push({field:'mutated',web:1,native:2});assert.equal(gate.evaluateMatchedBoundaryCoverage(legacy,session).requiredCoveragePassed,false,id);}
});
test('missing diagnostic altered membership or masked raw difference cannot pass',()=>{
 for(const id of Object.keys(expectedDiagnostics))for(const mutate of [
  r=>r.results.splice(r.results.findIndex(row=>row.case===id),1),
  r=>r.results.find(row=>row.case===id).case='replacement-diagnostic',
  r=>r.results.find(row=>row.case===id).differences.pop(),
  r=>r.results.find(row=>row.case===id).differences.push({field:'unrelated corruption',web:1,native:2}),
  r=>r.results.find(row=>row.case===id).passed=true,
  r=>r.results.find(row=>row.case===id).unobserved=[],
 ]){const {legacy,session}=reports();mutate(legacy);assert.equal(gate.evaluateMatchedBoundaryCoverage(legacy,session).requiredCoveragePassed,false,id);}
});
test('session pair inventory and each required outcome are strict',()=>{
 for(const mutate of [r=>r.cases.pop(),r=>r.cases.reverse(),r=>r.passed=false,r=>r.cases[0].passed=false,r=>r.cases[1].differences.push({field:'late-preview',web:false,native:true}),r=>r.rawParity=true,r=>r.parityAccepted=true]){const {legacy,session}=reports();mutate(session);assert.equal(gate.evaluateMatchedBoundaryCoverage(legacy,session).requiredCoveragePassed,false);}
});
test('corpus integrity errors and caller-provided exclusion membership fail closed',()=>{
 for(const mutate of [r=>r.differences.push({field:'corpus',error:'missing'}),r=>r.results.reverse(),r=>r.passed=true,r=>r.coverage.nativeCases--,r=>r.rawParity=true]){const {legacy,session}=reports();mutate(legacy);assert.equal(gate.evaluateMatchedBoundaryCoverage(legacy,session).requiredCoveragePassed,false);}
 const {legacy,session}=reports();assert.equal(gate.evaluateMatchedBoundaryCoverage(legacy,session,{exclude:['root-triple']}).requiredCoveragePassed,false);
});

test('capture parser requires raw inputs and rejects omission duplicate and exclusion flags',()=>{
 const required=['legacy-browser-directory','legacy-native-file','session-suite','session-browser-report','session-binding','session-native-file','output-file','legacy-output-file','session-output-file','expected-commit','expected-run-id','native-commit','legacy-native-binary-sha256','session-native-binary-sha256'];
 const args=required.flatMap(key=>['--'+key,'value']);assert.equal(Object.keys(gate.parseMatchedBoundaryCaptureArgs(args)).length,required.length);
 for(const omit of required){const missing=required.filter(key=>key!==omit).flatMap(key=>['--'+key,'value']);assert.throws(()=>gate.parseMatchedBoundaryCaptureArgs(missing),/required/);}
 for(const extra of [['--exclude','root-triple'],['--legacy-comparison-file','trusted.json'],['--session-comparison-file','trusted.json'],['--legacy-native-file','duplicate']])assert.throws(()=>gate.parseMatchedBoundaryCaptureArgs([...args,...extra]),/unknown|duplicate/);
});

test('gate authenticates raw additive capture bytes and caller identity rather than serialized verdicts',async()=>{
 assert.equal(typeof gate.verifySessionCaptureForGate,'function','gate capture authentication entry point');
 const {createSessionReplacementSuite,captureBinding,runtimePin}=await import('./suite.mjs');
 const {runSessionReplacementDiscovery}=await import('./node-runner.mjs');
 const commit='1'.repeat(40),runId='123',suite=createSessionReplacementSuite({commit,runId}),node=await runSessionReplacementDiscovery();
 // Protocol unit fixture only, never written as a browser capture or acceptance.
 const report={schema:'pando-m977-boundary-session-replacement-web',version:1,state:'complete',identity:suite.identity,sourceHashes:node.sourceHashes,runtime:{userAgent:'Chrome/'+runtimePin.chromium,node:false,document:true,worker:true,playwright:runtimePin.playwright,browserVersion:runtimePin.chromium,chromiumRevision:runtimePin.revision,cdp:{jsVersion:runtimePin.v8,product:'Chrome/'+runtimePin.chromium},ci:{commit,runId}},cases:node.cases,limits:suite.identity.limits};
 const suiteText=JSON.stringify(suite),reportText=JSON.stringify(report),binding=captureBinding(suiteText,reportText),args={suiteText,reportText,binding,expectedCommit:commit,expectedRunId:runId};
 assert.equal((await gate.verifySessionCaptureForGate(args)).cases.length,2);
 for(const mutate of [a=>a.expectedCommit='2'.repeat(40),a=>a.expectedRunId='124',a=>a.suiteText+=' ',a=>a.reportText+=' ',a=>a.binding.reportSha256='0'.repeat(64),a=>a.binding.reportBytes--,a=>a.reportText=JSON.stringify(node)]){const bad=structuredClone(args);mutate(bad);await assert.rejects(()=>gate.verifySessionCaptureForGate(bad));}
 const verdict=structuredClone(report);verdict.passed=true;verdict.differences=[];await assert.rejects(()=>gate.verifySessionCaptureForGate({...args,reportText:JSON.stringify(verdict)}));
 await assert.rejects(()=>gate.compareMatchedBoundaryCaptures({expectedCommit:commit,expectedRunId:runId,nativeCommit:'2'.repeat(40)}),/native commit/);
});
