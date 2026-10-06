// Diagnostic comparison. A successful verifier authenticates the observations;
// it must never be promoted to a parity acceptance gate.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {boundaryTimingCases,verifyBoundaryTimingCase} from './runtime.mjs';
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
// These diagnostic corpora use the same fixed root fixture. Authenticate the
// declared source against the canonical storage graph, not only its flattened
// display receipt. Exact rings/order/versions remain observable here.
export function verifyNativeFixedBoundaryInput(state,definition){
 const ids=definition.features.map(f=>f.id),canonical=state.nativeCanonicalDocument;
 assert.deepEqual(canonical.units,ids.map(id=>({id,kind:'general',name:id,baseName:id,nameExplicit:true,locked:false,libraryOrigin:null,metadata:{},notes:'',sourceFolderId:'',sourceGeometryVersion:'',sourceLibraryId:''})),'canonical source unit identities and metadata');
 assert.deepEqual(canonical.timelineRecords,{schemaVersion:1,
  lifetimes:ids.map(id=>({id:'lifetime:'+id,entityId:id,validFrom:null,validTo:null})),
  parentRelations:ids.map(id=>({id:'parent:'+id,entityId:id,parentId:'',coverageMode:'explicit',validFrom:null,validTo:null})),
  geometryBindings:ids.map(id=>({id:'geometry:'+id,entityId:id,geometryRef:{id,version:1},validFrom:null,validTo:null})),
 },'canonical active source lifetime, parent and geometry bindings');
 const geometry=f=>({type:'Polygon',coordinates:[f.ring]});
 assert.deepEqual(canonical.geometries,definition.features.map(f=>({id:f.id,version:1,geojson:geometry(f)})),'exact canonical source geometry records');
 assert.deepEqual(canonical.presentation.objectStyles,{territorial:Object.fromEntries(ids.map(id=>[id,{color:null,opacity:1}]))},'native source styles');
 assert.deepEqual(state.document,{entities:definition.features.map(f=>({id:f.id,parentId:'',coverageMode:'explicit',entityKind:'general',geometry:geometry(f),properties:{schemaVersion:5,entityKind:'general',name:f.id,parentId:'',coverageMode:'explicit',style:{},locked:false,validFrom:null,validTo:null,notes:'',metadata:{},sourceFolderId:'',sourceLibraryId:'',sourceGeometryVersion:''}})),distributionEntries:[]},'flattened source view matches authenticated input');
 assert.deepEqual(canonical.content.distributionEntries,[],'no hidden source distribution entries');
 return state;
}

export function verifyNativeTiming(cases,receipt){
 assert.equal(receipt.schema,'pando-m977-native-boundary-timing');assert.equal(receipt.version,1);assert.deepEqual(receipt.rows.map(row=>row.case),cases.map(row=>row.id));
 for(const [i,definition]of cases.entries()){
  const row=receipt.rows[i];assert.deepEqual(row.completionBarrier,{workerPoolCompletedBeforeFinalSnapshot:true,ownerEventsProcessedAfterCompletion:true,timeoutMs:30000});assert.ok(!Object.hasOwn(row,'error'),row.error);assert.deepEqual(row.input,definition);const stages=row.stages,before=stages.before.state;verifyNativeFixedBoundaryInput(before,definition);assert.ok(Number.isSafeInteger(stages.before.revision)&&stages.before.revision>=0,'actual native baseline revision');
  const stageNames=['before','entered',...(definition.phase==='prepare-queued'?[]:['prepared',...(definition.phase==='move-queued'?['drag']:[])]),'interval','actionBoundary','afterAction','settled'];assert.deepEqual(Object.keys(stages).sort(),stageNames.sort());
  for(const [name,s]of Object.entries(stages)){
   if(name==='interval'&&definition.phase!=='prepare-queued'){assert.equal(s.observed,false);assert.match(s.reason,/no public|not a boundary-move/);continue;}
   assert.equal(s.observed,true);const bytes=Buffer.from(s.state.canonicalBytesBase64,'base64');assert.ok(bytes.length>0);assert.equal(bytes.toString('base64'),s.state.canonicalBytesBase64,'canonical base64 encoding '+name);assert.equal(hash(bytes),s.state.documentSha256,'exact native canonical hash '+name);assert.deepEqual(JSON.parse(bytes),s.state.nativeCanonicalDocument,'native raw canonical view '+name);assert.equal(s.state.canonicalBytesBase64,before.canonicalBytesBase64,'full native canonical byte equality '+name);assert.equal(s.state.unchangedFromBefore,true);assert.deepEqual(s.state.document,before.document,'native flattened document remains bound to exact source '+name);assert.deepEqual(s.state.history,{canUndo:false,canRedo:false});assert.equal(s.revision,stages.before.revision);assert.ok(Array.isArray(s.draftPaths),'actual projected native draft paths');
   assert.equal(s.edit.active,name!=='before'&&!(definition.action==='cancel'&&['afterAction','settled'].includes(name)),'actual native session lifecycle '+name);
   if(s.edit.active){assert.deepEqual(s.edit.targets.map(ref=>({domain:ref.domain,id:ref.id})),definition.selectedIds.map(id=>({domain:'territorial',id})),'fixed native boundary owners '+name);assert.deepEqual({domain:s.edit.target.domain,id:s.edit.target.id},{domain:'territorial',id:definition.seedId},'fixed native boundary seed '+name);}
   const ids=definition.action==='select'&&['afterAction','settled'].includes(name)?definition.selectionAfter:definition.selectedIds;
   assert.deepEqual(s.selectionItems.map(ref=>({domain:ref.domain,id:ref.id})),ids.map(id=>({domain:'territorial',id})),'exact native selected refs '+name);
   assert.deepEqual({domain:s.selection.domain,id:s.selection.id},{domain:'territorial',id:name==='before'?definition.seedId:'C'},'exact native primary '+name);
  }
  assert.deepEqual(row.limits,{rawParity:false,workerCPUExecutionPhase:false,renderAdoptionPending:false,boundaryMoveWorkerInterval:false,timingEquivalent:definition.phase==='prepare-queued',nativeRawPreviewGeometry:false,historyDepth:false,fullCanonicalDocumentBytes:true,pointerPixels:false,gpuRendering:false},'complete native observation limits');
  if(definition.phase==='prepare-queued'){assert.equal(stages.interval.outcome.workerStarted,false);assert.equal(stages.interval.edit.calculating,true);assert.equal(stages.actionBoundary.outcome.workerQueued,true);}
  assert.deepEqual(stages.actionBoundary.outcome,{timingEquivalent:definition.phase==='prepare-queued',workerQueued:definition.phase!=='render-adoption'},'observed native public action boundary');
  assert.equal(stages.actionBoundary.edit.calculating,definition.phase!=='render-adoption','actual queued worker at public action boundary');
  assert.equal(stages.actionBoundary.edit.boundaryStatus,definition.phase==='prepare-queued'?'preparing':'ready','actual preparation status at public action boundary');
  assert.equal(stages.afterAction.outcome.action,definition.action,'native actual public action');if(definition.action==='select')assert.deepEqual(stages.afterAction.selectionItems.map(ref=>ref.id),definition.selectionAfter,'immediate native public selection');if(definition.action==='cancel')assert.equal(stages.afterAction.edit.active,false,'immediate native Cancel');
  assert.equal(stages.before.selection.id,definition.seedId,'initial requested primary');assert.equal(stages.entered.selection.id,'C','entry primary fallback');assert.equal(stages.actionBoundary.selection.id,'C','request-time primary before public action');
  const end=stages.settled,e=end.edit;
  if(definition.action==='cancel'){assert.equal(e.active,false);assert.equal(!!e.previewReady,false,'Cancel cannot resurrect preview');assert.deepEqual(end.draftPaths,[]);}
  else {
   assert.equal(e.active,true);assert.equal(e.calculating,false);assert.equal(typeof e.previewReady,'boolean');assert.ok(['ready','error'].includes(e.boundaryStatus),'observed settled boundary status');
   // Read both historical and corrected selection outcomes. The comparison below
   // computes their actual differences; the live product regression requires zero.
   if(definition.action==='none'){assert.equal(e.boundaryStatus,'ready');assert.equal(e.previewReady,definition.phase==='move-queued');}
  }
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
  const refs=items=>items.map(({domain,id})=>({domain,id}));assert.deepEqual(refs(ws.selection.items),refs(ns.selectionItems),'ordered settled selection');assert.equal(ws.selection.primaryKey.split(':').at(-1),ns.selection.id,'settled primary');
  if(ws.preparation?.status==='ready')assert.deepEqual(ws.preparation.ownerIds,definition.selectedIds,'fixed web boundary owners');
  const webBoundaryStatus=ws.preparation?.status||null,nativeBoundaryStatus=ns.edit.boundaryStatus||null,webPreview=ws.preview,nativePreview=!!ns.edit.previewReady;
  const differences=[];if(webBoundaryStatus!==nativeBoundaryStatus)differences.push({field:'boundaryStatus',web:webBoundaryStatus,native:nativeBoundaryStatus});if(webPreview!==nativePreview)differences.push({field:'preview',web:webPreview,native:nativePreview});
  const webStages=Object.values(w.stages).filter(s=>s.observed),nativeStages=Object.values(n.stages).filter(s=>s.observed);
  const canonicalBytesUnchangedOnBothSides=webStages.every(s=>s.canonical===w.beforeCanonical)&&nativeStages.every(s=>s.state.canonicalBytesBase64===n.stages.before.state.canonicalBytesBase64);
  const historyUnchangedOnBothSides=webStages.every(s=>s.history.undo===w.stages.before.history.undo&&s.history.redo===w.stages.before.history.redo)&&nativeStages.every(s=>s.state.history.canUndo===n.stages.before.state.history.canUndo&&s.state.history.canRedo===n.stages.before.state.history.canRedo);
  return {case:definition.id,publicAction:definition.action,publicSelectionEquivalent:true,initialTerritoriesEquivalent:true,canonicalBytesUnchangedOnBothSides,historyUnchangedOnBothSides,
   timingEquivalent:definition.phase==='prepare-queued',nativeIntervalObserved:n.stages.interval.observed,
   nativeActionBoundary:definition.phase==='render-adoption'?'after-atomic-native-adoption':definition.phase==='move-queued'?'after-synchronous-fan-out-before-downstream-preview':'preparation-queued-not-started',
   webBoundaryStatus,nativeBoundaryStatus,webPreview,nativePreview,differences,
   boundaryReadinessDivergence:(webBoundaryStatus==='ready')!==(nativeBoundaryStatus==='ready'),previewDivergence:webPreview!==nativePreview,
   draftObservation:{webFreeDraftCoordinates:ws.draft.coords,nativeProjectedPathCount:ns.draftPaths.length,webGestureFeatureCount:ws.gesture?.features?.length||0,rawDraftRepresentationsComparable:false}};
 });
 const summary={cases:rows.length,publicSelectionCases:rows.filter(r=>r.publicAction==='select').length,boundaryReadinessDivergences:rows.filter(r=>r.boundaryReadinessDivergence).length,previewDivergences:rows.filter(r=>r.previewDivergence).length,nativeAbsentIntervalCases:rows.filter(r=>!r.nativeIntervalObserved).length,canonicalOrHistoryMutations:rows.filter(r=>!r.canonicalBytesUnchangedOnBothSides||!r.historyUnchangedOnBothSides).length};
 return {schema:'pando-m977-boundary-timing-diagnostic-comparison',version:1,authoritativeBrowser:false,rawParity:false,parityAccepted:false,cases:rows,summary,remaining:['Actual exact-commit Chromium capture and clean native rebuild',...(summary.boundaryReadinessDivergences||summary.previewDivergences?['Public selection readiness or preview divergence remains in the supplied observations']:[]),'Native has no render-adoption or boundary-move worker interval','CPU-in-flight, real DOM input, GPU frames, raw draft packet parity and private native history depth remain unobserved']};
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const [webFile,nativeFile,output]=process.argv.slice(2);assert.ok(webFile&&nativeFile,'Node report and native report required');const web=JSON.parse(fs.readFileSync(webFile)),native=JSON.parse(fs.readFileSync(nativeFile));
 assert.equal(web.schema,'pando-m977-node-boundary-timing','CLI accepts Node diagnostics only; authoritative capture requires verifyCapture before comparison');assert.equal(web.authoritativeBrowserObservation,false);
 const report=compareTimingObservations(web,native),text=JSON.stringify(report,null,2)+'\n';if(output)fs.writeFileSync(output,text);else console.log(text);
}
