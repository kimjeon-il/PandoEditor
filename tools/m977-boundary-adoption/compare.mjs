// Diagnostic comparison. A successful verifier confirms the stated divergence;
// it must never be promoted to a parity acceptance gate.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {boundaryTimingCases,verifyBoundaryTimingCase} from './runtime.mjs';
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
export function verifyNativeTiming(cases,receipt){
 assert.equal(receipt.schema,'pando-m977-native-boundary-timing');assert.equal(receipt.version,1);assert.deepEqual(receipt.rows.map(row=>row.case),cases.map(row=>row.id));
 for(const [i,definition]of cases.entries()){
  const row=receipt.rows[i];assert.deepEqual(row.completionBarrier,{workerPoolCompletedBeforeFinalSnapshot:true,ownerEventsProcessedAfterCompletion:true,timeoutMs:30000});assert.ok(!Object.hasOwn(row,'error'),row.error);assert.deepEqual(row.input,definition);const stages=row.stages,before=stages.before.state;
  const stageNames=['before','entered',...(definition.phase==='prepare-queued'?[]:['prepared',...(definition.phase==='move-queued'?['drag']:[])]),'interval','actionBoundary','afterAction','settled'];assert.deepEqual(Object.keys(stages).sort(),stageNames.sort());
  for(const [name,s]of Object.entries(stages)){
   if(name==='interval'&&definition.phase!=='prepare-queued'){assert.equal(s.observed,false);assert.match(s.reason,/no public|not a boundary-move/);continue;}
   assert.equal(s.observed,true);const bytes=Buffer.from(s.state.canonicalBytesBase64,'base64');assert.ok(bytes.length>0);assert.equal(hash(bytes),s.state.documentSha256,'exact native canonical hash '+name);assert.deepEqual(JSON.parse(bytes),s.state.nativeCanonicalDocument,'native raw canonical view '+name);assert.equal(s.state.canonicalBytesBase64,before.canonicalBytesBase64,'full native canonical byte equality '+name);assert.equal(s.state.unchangedFromBefore,true);assert.deepEqual(s.state.history,{canUndo:false,canRedo:false});assert.equal(s.revision,stages.before.revision);assert.ok(Array.isArray(s.draftPaths),'actual projected native draft paths');
  }
  assert.equal(row.limits.rawParity,false);assert.equal(row.limits.timingEquivalent,definition.phase==='prepare-queued');assert.equal(row.limits.workerCPUExecutionPhase,false);assert.equal(row.limits.renderAdoptionPending,false);assert.equal(row.limits.boundaryMoveWorkerInterval,false);
  if(definition.phase==='prepare-queued'){assert.equal(stages.interval.outcome.workerStarted,false);assert.equal(stages.interval.edit.calculating,true);assert.equal(stages.actionBoundary.outcome.workerQueued,true);}
  assert.equal(stages.afterAction.outcome.action,definition.action,'native actual public action');if(definition.action==='select')assert.deepEqual(stages.afterAction.selectionItems.map(ref=>ref.id),definition.selectionAfter,'immediate native public selection');if(definition.action==='cancel')assert.equal(stages.afterAction.edit.active,false,'immediate native Cancel');
  const end=stages.settled,e=end.edit;
  if(definition.action==='cancel'){assert.equal(e.active,false);assert.deepEqual(end.draftPaths,[]);}
  else if(definition.action==='select'){assert.equal(e.active,true);assert.equal(e.boundaryStatus,'error');assert.equal(e.previewReady,false);assert.equal(e.calculating,false);}
  else {assert.equal(e.active,true);assert.equal(e.boundaryStatus,'ready');assert.equal(e.previewReady,definition.phase==='move-queued');}
  assert.deepEqual(end.selectionItems.map(ref=>ref.id),definition.action==='select'?definition.selectionAfter:definition.selectedIds);assert.equal(end.selection.id,'C');
 }
 return receipt;
}
export function compareTimingObservations(web,native){
 const cases=boundaryTimingCases();assert.deepEqual(web.cases.map(row=>row.case),cases.map(row=>row.id));verifyNativeTiming(cases,native);
 const rows=cases.map((definition,i)=>{
  const w=verifyBoundaryTimingCase(definition,web.cases[i]),n=native.rows[i],ws=w.stages.settled,ns=n.stages.settled;
  const project=rows=>rows.map(({id,parentId,coverageMode,entityKind,properties,geometry})=>({id,parentId,coverageMode,entityKind,properties,geometry}));
  assert.deepEqual(project(JSON.parse(w.beforeCanonical).entities),project(n.stages.before.state.document.entities),'same exact territorial source input and metadata');
  assert.deepEqual(ws.selection.items.map(ref=>ref.id),ns.selectionItems.map(ref=>ref.id),'ordered settled selection');assert.equal(ws.selection.primaryKey.split(':').at(-1),ns.selection.id,'settled primary');
  const divergence=definition.action==='select';assert.equal(ws.preview!==!!ns.edit.previewReady,definition.phase==='move-queued'&&divergence);
  return {case:definition.id,publicAction:definition.action,publicSelectionEquivalent:true,initialTerritoriesEquivalent:true,canonicalBytesUnchangedOnBothSides:true,historyUnchangedOnBothSides:true,
   timingEquivalent:definition.phase==='prepare-queued',nativeIntervalObserved:n.stages.interval.observed,
   nativeActionBoundary:definition.phase==='render-adoption'?'after-atomic-native-adoption':definition.phase==='move-queued'?'after-synchronous-fan-out-before-downstream-preview':'preparation-queued-not-started',
   webBoundaryStatus:ws.preparation?.status||null,nativeBoundaryStatus:ns.edit.boundaryStatus||null,webPreview:ws.preview,nativePreview:!!ns.edit.previewReady,
   boundaryReadinessDivergence:divergence,previewDivergence:definition.phase==='move-queued'&&divergence,
   draftObservation:{webFreeDraftCoordinates:ws.draft.coords,nativeProjectedPathCount:ns.draftPaths.length,webGestureFeatureCount:ws.gesture?.features?.length||0,rawDraftRepresentationsComparable:false}};
 });
 return {schema:'pando-m977-boundary-timing-diagnostic-comparison',version:1,authoritativeBrowser:false,rawParity:false,parityAccepted:false,cases:rows,summary:{cases:9,publicSelectionCases:3,boundaryReadinessDivergences:3,previewDivergences:1,nativeAbsentIntervalCases:6,canonicalOrHistoryMutations:0},remaining:['Actual exact-commit Chromium capture and clean native rebuild','Public selection readiness divergence is unresolved','Native has no render-adoption or boundary-move worker interval','CPU-in-flight, real DOM input, GPU frames, raw draft packet parity and private native history depth remain unobserved']};
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const [webFile,nativeFile,output]=process.argv.slice(2);assert.ok(webFile&&nativeFile,'Node report and native report required');const web=JSON.parse(fs.readFileSync(webFile)),native=JSON.parse(fs.readFileSync(nativeFile));
 assert.equal(web.schema,'pando-m977-node-boundary-timing','CLI accepts Node diagnostics only; authoritative capture requires verifyCapture before comparison');assert.equal(web.authoritativeBrowserObservation,false);
 const report=compareTimingObservations(web,native),text=JSON.stringify(report,null,2)+'\n';if(output)fs.writeFileSync(output,text);else console.log(text);
}
