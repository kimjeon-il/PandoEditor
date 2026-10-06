// This is explicitly bounded matched-controller/session coverage, never raw parity.
// The capture entry point recomputes both comparisons from authenticated raw inputs.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {gunzipSync} from 'node:zlib';
import {createHash} from 'node:crypto';
import {BOUNDARY_CASE_IDS,compareBoundaryCapture} from '../m974-native-boundary.mjs';

const DIAGNOSTIC_DIFFERENCES=Object.freeze({
 'root-stale-preparation':[{field:'settled.outcome.ok',web:false,native:true},{field:'settled.nonEquivalentStimuli.boundaryStatus',web:'error',native:'ready'}],
 'root-stale-move':[{field:'settled.outcome.ok',web:false,native:true},{field:'settled.nonEquivalentStimuli.previewReady',web:false,native:true}],
 'root-stale-preview':[{field:'settled.outcome.ok',web:false,native:true},{field:'settled.nonEquivalentStimuli.previewReady',web:false,native:true}],
});
export const SESSION_CASE_IDS=Object.freeze(['preparation-completed-reenter','preview-completed-reenter']);
const DIAGNOSTIC_CASE_IDS=Object.freeze(Object.keys(DIAGNOSTIC_DIFFERENCES));
const MATCHED_CASE_IDS=Object.freeze(BOUNDARY_CASE_IDS.filter(id=>!Object.hasOwn(DIAGNOSTIC_DIFFERENCES,id)));

// Pure classification for tests and already recomputed comparisons. This helper
// does not authenticate captures; only compareMatchedBoundaryCaptures does that.
export function evaluateMatchedBoundaryCoverage(legacy,session,options={}){
 const failures=[];
 const check=(scope,fn)=>{try{fn();return true;}catch(error){failures.push({scope,error:error.message});return false;}};
 check('options',()=>assert.deepEqual(options,{},'no caller exclusions or replacement inventory'));
 const legacyInventory=check('legacy.inventory',()=>{
  assert.equal(legacy.schema,'pando-m974-native-boundary-comparison');assert.equal(legacy.version,1);assert.equal(legacy.rawParity,false);assert.equal(legacy.passed,false,'raw legacy comparison must retain all three non-equivalent failures');assert.deepEqual(legacy.differences,[],'no corpus integrity errors');
  for(const key of ['expectedCases','browserCases','nativeCases'])assert.equal(legacy.coverage[key],BOUNDARY_CASE_IDS.length,key);
  assert.deepEqual(legacy.results.map(row=>row.case),BOUNDARY_CASE_IDS,'exact original ordered 33-case inventory');
 });
 let matchedPassed=legacyInventory,diagnosticsValid=legacyInventory;
 for(const id of MATCHED_CASE_IDS)if(!check('legacy.matched.'+id,()=>{const row=legacy.results.find(r=>r.case===id);assert.ok(row,'missing matched case');assert.equal(row.passed,true);assert.equal(row.rawParity,false);assert.deepEqual(row.differences,[]);})){matchedPassed=false;}
 for(const id of DIAGNOSTIC_CASE_IDS)if(!check('legacy.nonEquivalent.'+id,()=>{
  const row=legacy.results.find(r=>r.case===id);assert.ok(row,'missing non-equivalent diagnostic');assert.equal(row.passed,false,'diagnostic cannot be relabeled a parity pass');assert.equal(row.rawParity,false);assert.deepEqual(row.differences,DIAGNOSTIC_DIFFERENCES[id],'exact source-backed diagnostic differences; no integrity errors or extra failures');assert.ok(row.unobserved.some(r=>r.case===id&&r.scope==='settled selection parity after different stale-response stimuli'),'non-equivalent stimulus label required');
 })){diagnosticsValid=false;}
 const sessionPassed=check('sessionReplacement',()=>{
  assert.equal(session.schema,'pando-m977-boundary-session-replacement-comparison');assert.equal(session.version,1);assert.equal(session.rawParity,false);assert.equal(session.parityAccepted,false);assert.equal(session.passed,true);assert.deepEqual(session.cases.map(row=>row.case),SESSION_CASE_IDS,'exact two public-session replacement cases');
  for(const row of session.cases){assert.equal(row.passed,true,row.case);assert.deepEqual(row.differences,[],row.case);}
 });
 return {schema:'pando-m977-boundary-matched-coverage-gate',version:1,scope:'30 legacy observable contracts within their recorded limits plus 2 matched public-session replacement cases',requiredCoveragePassed:failures.length===0,rawParity:false,fullParityAccepted:false,
  matchedLegacy:{caseIds:[...MATCHED_CASE_IDS],passed:matchedPassed},nonEquivalentDiagnostics:{caseIds:[...DIAGNOSTIC_CASE_IDS],expectedDifferencesValidated:diagnosticsValid,rawPassed:false},sessionReplacement:{caseIds:[...SESSION_CASE_IDS],passed:sessionPassed},failures,
  remaining:['Matched public owner-lock/project-stale coverage remains separate and unaccepted by this gate.','Native asynchronous boundary-move, full DOM input, GPU frames and private native history depth remain unobserved.'],
  legacyComparison:legacy,sessionReplacementComparison:session};
}

export async function verifySessionCaptureForGate({suiteText,reportText,binding,expectedCommit,expectedRunId}){
 const {verifyCapture}=await import('./suite.mjs');
 const web=verifyCapture(suiteText,reportText,binding);assert.equal(web.identity.commit,expectedCommit,'session browser commit');assert.equal(web.identity.runId,expectedRunId,'session browser run');return web;
}

export async function compareMatchedBoundaryCaptures(options){
 const {compareSessionReplacement}=await import('./compare.mjs');
 const {legacyBrowserDirectory,legacyNativeFile,sessionSuite,sessionBrowserReport,sessionBinding,sessionNativeFile,expectedCommit,expectedRunId,nativeCommit,legacyNativeBinarySha256,sessionNativeBinarySha256}=options;
 assert.match(expectedCommit??'',/^[a-f0-9]{40}$/,'explicit expected commit required');assert.match(expectedRunId??'',/^\d+$/,'explicit expected run required');assert.equal(nativeCommit,expectedCommit,'exact native commit required');
 for(const hash of [legacyNativeBinarySha256,sessionNativeBinarySha256])assert.match(hash??'',/^[a-f0-9]{64}$/,'both native binary hashes required');
 const legacy=compareBoundaryCapture({browserDirectory:legacyBrowserDirectory,nativeFile:legacyNativeFile,expectedCommit,expectedRunId,nativeCommit,nativeBinarySha256:legacyNativeBinarySha256,outputFile:options.legacyOutputFile});
 const suiteText=gunzipSync(fs.readFileSync(sessionSuite)).toString('utf8'),reportText=fs.readFileSync(sessionBrowserReport,'utf8'),binding=JSON.parse(fs.readFileSync(sessionBinding));
 const web=await verifySessionCaptureForGate({suiteText,reportText,binding,expectedCommit,expectedRunId});
 const sessionNativeBytes=fs.readFileSync(sessionNativeFile),sessionNativeReceiptSha256=createHash('sha256').update(sessionNativeBytes).digest('hex');
 const session=compareSessionReplacement(web,JSON.parse(sessionNativeBytes));
 const report=evaluateMatchedBoundaryCoverage(legacy,session);report.provenance={expectedCommit,expectedRunId,nativeCommit,legacyNativeBinarySha256,sessionNativeBinarySha256,sessionNativeReceiptSha256,sessionCaptureBinding:binding,comparisonsRecomputedFromAuthenticatedRawInputs:true,nativeBuildAuthentication:'Exact-checkout CI build and supplied binary identities require separate artifact provenance; this gate does not infer them.'};
 return report;
}

export function parseMatchedBoundaryCaptureArgs(args){
 const names={'--legacy-browser-directory':'legacyBrowserDirectory','--legacy-native-file':'legacyNativeFile','--session-suite':'sessionSuite','--session-browser-report':'sessionBrowserReport','--session-binding':'sessionBinding','--session-native-file':'sessionNativeFile','--output-file':'outputFile','--legacy-output-file':'legacyOutputFile','--session-output-file':'sessionOutputFile','--expected-commit':'expectedCommit','--expected-run-id':'expectedRunId','--native-commit':'nativeCommit','--legacy-native-binary-sha256':'legacyNativeBinarySha256','--session-native-binary-sha256':'sessionNativeBinarySha256'},options={};
 for(let i=0;i<args.length;i+=2){const key=names[args[i]],value=args[i+1];assert.ok(key,'unknown gate argument '+args[i]);assert.ok(!Object.hasOwn(options,key),'duplicate gate argument '+args[i]);assert.ok(value&&!value.startsWith('--'),'missing gate argument '+args[i]);options[key]=value;}
 for(const key of Object.values(names))assert.ok(options[key],'required gate argument '+key);return options;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const options=parseMatchedBoundaryCaptureArgs(process.argv.slice(2)),report=await compareMatchedBoundaryCaptures(options);for(const [file,value]of [[options.outputFile,report],[options.legacyOutputFile,report.legacyComparison],[options.sessionOutputFile,report.sessionReplacementComparison]])fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');console.log(JSON.stringify({scope:report.scope,requiredCoveragePassed:report.requiredCoveragePassed,legacyRawComparisonPassed:report.legacyComparison.passed,rawParity:false,fullParityAccepted:false,failures:report.failures}));if(!report.requiredCoveragePassed)process.exitCode=1;}catch(error){console.error(error.stack);process.exitCode=1;}
}
