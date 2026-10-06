import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {boundarySessionReplacementCases,sessionReplacementStageNames,verifyBoundarySessionReplacementCase} from './runtime.mjs';
import {verifyNativeFixedBoundaryInput} from '../m977-boundary-adoption/compare.mjs';
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const refs=items=>items.map(({domain,id})=>({domain,id}));
export function verifyNativeSessionReplacement(receipt){
 assert.equal(receipt.schema,'pando-m977-boundary-session-replacement-native');assert.equal(receipt.version,1);const definitions=boundarySessionReplacementCases();assert.deepEqual(receipt.rows.map(r=>r.case),definitions.map(d=>d.id));
 for(const [i,d]of definitions.entries()){
  const row=receipt.rows[i],s=row.stages,t=row.events,preview=d.phase==='preview';assert.ok(!Object.hasOwn(row,'error'),row.error);assert.deepEqual(row.input,d);assert.deepEqual(row.stageOrder,sessionReplacementStageNames(d));assert.deepEqual(Object.keys(s).sort(),[...row.stageOrder].sort());
  assert.deepEqual(row.completionBarrier,{mechanism:'actual-old-worker-completed-owner-delivery-withheld',timeoutMs:30000,oldPoolCompleted:true,ownerEventsProcessedBeforeReplacement:false,replacementEntryCapturedBeforeDrain:true,finalPoolCompleted:true,ownerEventsProcessedAfterReplacement:true});
  assert.deepEqual(row.limits,{rawParity:false,workerCPUExecutionPhase:false,boundaryMoveWorkerInterval:false,privateRequestIds:false,privateResultPayload:false,nativeRawPreviewGeometry:false,historyDepth:false,fullCanonicalDocumentBytes:true,pointerPixels:false,gpuRendering:false,projectGenerationChange:false,ownerLockCoverage:false});
  if(preview){assert.deepEqual(row.moveInput.intended,d.move.coordinate);assert.deepEqual(row.moveInput.inverse,d.move.coordinate);assert.equal(row.moveInput.exact,true);assert.equal(row.moveInput.maximumUlpRadius,4);assert.equal(row.moveInput.chosen.length,2);assert.ok(row.moveInput.chosen.every(Number.isFinite));}else assert.equal(row.moveInput,null);
  const before=s.before;verifyNativeFixedBoundaryInput(before.state,d);assert.ok(Number.isSafeInteger(before.revision)&&before.revision>=0);let cursor=0;
  for(const name of row.stageOrder){
   const stage=s[name];assert.equal(stage.observed,true,name);const bytes=Buffer.from(stage.state.canonicalBytesBase64,'base64');assert.ok(bytes.length>0);assert.equal(bytes.toString('base64'),stage.state.canonicalBytesBase64);assert.equal(hash(bytes),stage.state.documentSha256);assert.deepEqual(JSON.parse(bytes),stage.state.nativeCanonicalDocument);assert.equal(stage.state.canonicalBytesBase64,before.state.canonicalBytesBase64,'native full canonical unchanged '+name);assert.equal(stage.state.unchangedFromBefore,true);assert.deepEqual(stage.state.document,before.state.document,'flattened native state remains authenticated baseline '+name);assert.deepEqual(stage.state.history,{canUndo:false,canRedo:false});assert.equal(stage.revision,before.revision);
   assert.ok(Number.isSafeInteger(stage.eventSequence)&&stage.eventSequence>=cursor&&stage.eventSequence<=t.length,'ordered native event cursor '+name);cursor=stage.eventSequence;
   assert.ok(Array.isArray(stage.draftPaths));const active=!['before','afterCancel','replacementSelected'].includes(name),replacement=['replacementSelected','replacementEntered','settled'].includes(name),ids=replacement?d.replacementIds:d.selectedIds;
   assert.equal(stage.edit.active,active);assert.deepEqual(refs(stage.selectionItems),ids.map(id=>({domain:'territorial',id})));
   assert.deepEqual(refs([stage.selection]),[{domain:'territorial',id:['before','replacementSelected'].includes(name)?'A':['replacementEntered','settled'].includes(name)?'B':'C'}]);
   if(!active){assert.deepEqual(stage.draftPaths,[]);continue;}
   assert.equal(stage.edit.tool,'boundary');assert.equal(stage.edit.previewReady,false,'no old preview publication '+name);assert.deepEqual(refs(stage.edit.targets),(replacement?d.replacementIds:d.selectedIds).map(id=>({domain:'territorial',id})));assert.deepEqual(refs([stage.edit.target]),[{domain:'territorial',id:'A'}]);
   const ready=name==='settled'||(preview&&['prepared','drag','oldCompleted'].includes(name));assert.equal(stage.edit.boundaryStatus,ready?'ready':'preparing');assert.equal(stage.edit.calculating,!ready||name==='oldCompleted');
   if(name==='settled'){assert.equal(stage.edit.canUndo,false);assert.equal(stage.edit.canRedo,false);const nodes=stage.draftPaths.flatMap(p=>p.vertices),junction=nodes.filter(n=>n.nodeKey==='1,1');assert.equal(junction.length,1);assert.equal(junction[0].fixed,true);assert.deepEqual(junction[0].ownerIds,['A','B','C']);assert.ok(Number.isFinite(junction[0].x)&&Number.isFinite(junction[0].y));}
   if(name==='prepared'){const junction=stage.draftPaths.flatMap(p=>p.vertices).find(n=>n.nodeKey==='1,1');assert.ok(junction);assert.equal(junction.fixed,false);assert.deepEqual(junction.ownerIds,['A','B','C']);}
  }
  assert.deepEqual(s.before.state.document.entities.map(({id,geometry})=>({id,geometry})),d.features.map(f=>({id:f.id,geometry:{type:'Polygon',coordinates:[f.ring]}})));
  t.forEach((e,i)=>{assert.equal(e.sequence,i);assert.ok(['geometry-signal','public-action','old-worker-completed','owner-completion-drain','owner-completion-drained'].includes(e.kind));});
  const events=kind=>t.filter(e=>e.kind===kind),one=kind=>{const list=events(kind);assert.equal(list.length,1,kind);return list[0];};
  const actions=events('public-action');assert.deepEqual(actions.map(e=>e.action),['enter-original',...(preview?['begin-move']:[]),'cancel','replace-selection','enter-replacement']);
  for(const [action,before,after]of [['enter-original','before','entered'],...(preview?[['begin-move','prepared','drag']]:[]),['cancel','oldCompleted','afterCancel'],['replace-selection','afterCancel','replacementSelected'],['enter-replacement','replacementSelected','replacementEntered']]){const e=actions.find(e=>e.action===action);assert.ok(e.sequence>=s[before].eventSequence&&e.sequence<s[after].eventSequence,'native action interval '+action);}
  const selected=actions.find(e=>e.action==='replace-selection');assert.deepEqual(selected.ids,d.replacementIds);assert.equal(selected.primary,d.replacementPrimary);
  if(preview){const e=actions.find(e=>e.action==='begin-move');assert.equal(e.nodeKey,d.move.nodeKey);assert.deepEqual(e.coordinate,d.move.coordinate);}
  const completed=one('old-worker-completed'),drain=one('owner-completion-drain'),drained=one('owner-completion-drained');assert.equal(completed.operation,preview?'downstream-canonical-preview':'boundary-preparation');assert.equal(completed.ownerEventsProcessed,false);assert.equal(completed.timeoutMs,30000);assert.equal(completed.sequence+1,s.oldCompleted.eventSequence);assert.ok(completed.sequence>=s[preview?'drag':'entered'].eventSequence);assert.equal(drain.sequence,s.replacementEntered.eventSequence);assert.ok(drain.sequence>completed.sequence);assert.equal(drained.poolCompleted,true);assert.equal(drained.calculating,false);assert.equal(drained.sequence+1,s.settled.eventSequence);assert.ok(drained.sequence>drain.sequence);assert.equal(s.settled.eventSequence,t.length);
  // Public signals between the old completion and final drain may only be the
  // synchronous Cancel/re-entry signals. No queued READY/preview adoption there.
  for(const e of events('geometry-signal').filter(e=>e.sequence>completed.sequence&&e.sequence<drain.sequence)){assert.ok(['cancel','enter-replacement'].includes(e.phase));if(e.edit.active){assert.equal(e.edit.boundaryStatus,'preparing');assert.equal(e.edit.previewReady,false);assert.deepEqual(refs(e.edit.targets),d.replacementIds.map(id=>({domain:'territorial',id})));}}
  const finalSignals=events('geometry-signal').filter(e=>e.sequence>drain.sequence);assert.ok(finalSignals.length>0,'actual replacement completion signal');for(const e of finalSignals){assert.equal(e.edit.active,true,'replacement cannot transiently disappear');assert.equal(e.edit.tool,'boundary');assert.equal(e.edit.previewReady,false,'old preview cannot transiently publish');assert.deepEqual(refs(e.edit.targets),d.replacementIds.map(id=>({domain:'territorial',id})),'every owner event retains replacement owners');assert.deepEqual(refs([e.edit.target]),[{domain:'territorial',id:'A'}]);assert.ok(['preparing','ready'].includes(e.edit.boundaryStatus));}assert.equal(finalSignals.at(-1).edit.boundaryStatus,'ready');assert.equal(finalSignals.at(-1).edit.previewReady,false);assert.deepEqual(refs(finalSignals.at(-1).edit.targets),d.replacementIds.map(id=>({domain:'territorial',id})));
 }
 return receipt;
}
export function compareSessionReplacement(web,native){
 const definitions=boundarySessionReplacementCases();assert.deepEqual(web.cases.map(r=>r.case),definitions.map(d=>d.id));verifyNativeSessionReplacement(native);
 const cases=definitions.map((d,i)=>{
  const w=verifyBoundarySessionReplacementCase(d,web.cases[i]),n=native.rows[i],ws=w.stages.settled,ns=n.stages.settled;
  const entities=rows=>rows.map(({id,parentId,coverageMode,entityKind,properties,geometry})=>({id,parentId,coverageMode,entityKind,properties,geometry}));assert.deepEqual(entities(JSON.parse(w.beforeCanonical).entities),entities(n.stages.before.state.document.entities),'same exact territorial input and metadata');
  const observed={web:{owners:ws.preparation.ownerIds,selected:refs(ws.selection.items),primary:ws.selection.primaryKey.split(':').at(-1),boundaryStatus:ws.preparation.status,preview:ws.preview,junctionFixed:ws.preparation.handles.find(h=>h.nodeKey==='1,1').fixed},native:{owners:ns.edit.targets.map(r=>r.id),selected:refs(ns.selectionItems),primary:ns.selection.id,boundaryStatus:ns.edit.boundaryStatus,preview:ns.edit.previewReady,junctionFixed:ns.draftPaths.flatMap(p=>p.vertices).find(v=>v.nodeKey==='1,1').fixed}};
  const differences=Object.keys(observed.web).filter(field=>JSON.stringify(observed.web[field])!==JSON.stringify(observed.native[field])).map(field=>({field,web:observed.web[field],native:observed.native[field]}));
  return {case:d.id,passed:differences.length===0,differences,observed,canonicalBytesUnchangedOnBothSides:true,publicHistoryUnchangedOnBothSides:true,replacementEntryCapturedBeforeOldDeliveryOnBothSides:true,limits:{fullDOM:false,gpuRendering:false,workerCPUExecutionPhase:false,nativeBoundaryMoveWorkerInterval:false,nativePrivateRequestIds:false,fullCanonicalCrossPlatformParity:false,ownerLockCoverage:false,projectGenerationChange:false}};
 });
 return {schema:'pando-m977-boundary-session-replacement-comparison',version:1,rawParity:false,parityAccepted:false,cases,summary:{cases:cases.length,matchedCases:cases.filter(c=>c.passed).length,differences:cases.reduce((sum,c)=>sum+c.differences.length,0)},passed:cases.every(c=>c.passed)};
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const [webFile,nativeFile,output]=process.argv.slice(2);assert.ok(webFile&&nativeFile,'Node diagnostic and native raw receipt required');const web=JSON.parse(fs.readFileSync(webFile)),native=JSON.parse(fs.readFileSync(nativeFile));assert.equal(web.schema,'pando-m977-boundary-session-replacement-node-diagnostic');assert.equal(web.authoritativeBrowserObservation,false);const report=compareSessionReplacement(web,native),text=JSON.stringify(report,null,2)+'\n';if(output)fs.writeFileSync(output,text);else console.log(text);
}
