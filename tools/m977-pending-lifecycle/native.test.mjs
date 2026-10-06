import assert from 'node:assert/strict';
import test from 'node:test';
import {collectNative as collectFreshNative,verifyNativeLifecycleReport} from './compare.mjs';
import {readLifecycleSources} from './sources.mjs';
const binary=process.env.M977_PENDING_LIFECYCLE_PROBE;
let captured;const collectNative=(binary,bundle)=>structuredClone(captured??=collectFreshNative(binary,bundle));
test('native matched pending-input tail includes every real lifecycle stage and exact history snapshots',()=>{
 assert.ok(binary,'M977_PENDING_LIFECYCLE_PROBE is required; native coverage cannot be skipped');
 const bundle=readLifecycleSources(),report=collectNative(binary,bundle);verifyNativeLifecycleReport(bundle,report);
});
test('native lifecycle rejects missing events, detached export and missing history evidence',()=>{
 assert.ok(binary);const bundle=readLifecycleSources(),report=collectNative(binary,bundle);
 for(const mutate of [r=>r.cases[0].stages.ready.state.canUndoDraft=true,r=>r.cases[0].stages.ready.mapViewState.rotationRoll=7,r=>r.cases[0].events=[],r=>{r.cases[0].stages.preview.web=r.cases[0].stages.applied.web;},r=>delete r.cases[0].stages.undo,r=>{r.cases[0].stages.review.selection.parts[0].geometry={};},r=>r.cases[0].stages.undo.history.canUndo=true]){const bad=structuredClone(report);mutate(bad);assert.throws(()=>verifyNativeLifecycleReport(bundle,bad));}
});
test('plain comparison cannot relabel Node rows as actual browser evidence',async()=>{
 const {loadLifecycleSources}=await import('./sources.mjs'),{runPendingLifecycleCase}=await import('./runtime.mjs'),{compareLifecycle}=await import('./compare.mjs');const loaded=await loadLifecycleSources();try{const cases=[];for(const d of loaded.bundle.corpus.cases)cases.push(await runPendingLifecycleCase(loaded,d,loaded.bundle.corpus));const n=collectNative(binary,loaded.bundle);assert.equal(compareLifecycle(loaded.bundle,{runtime:{collector:'actual-chromium'},cases},n).actualBrowser,false);}finally{loaded.cleanup();}
});
test('paired interchange compares every exact geometry, identity, parent, reference and lifecycle stage',async()=>{
 const {loadLifecycleSources}=await import('./sources.mjs'),{runPendingLifecycleCase}=await import('./runtime.mjs'),{verifyPairedLifecycle}=await import('./compare.mjs');assert.equal(typeof verifyPairedLifecycle,'function');const l=await loadLifecycleSources();try{const native=collectNative(binary,l.bundle);for(const [i,d]of l.bundle.corpus.cases.entries()){const w=await runPendingLifecycleCase(l,d,l.bundle.corpus);verifyPairedLifecycle(l.bundle,w,native.cases[i]);}}
 finally{l.cleanup();}
});
test('paired contract rejects coherent one-runtime geometry, reference, owner order and history corruption',async()=>{
 const {loadLifecycleSources,sha256}=await import('./sources.mjs'),{runPendingLifecycleCase}=await import('./runtime.mjs'),{verifyPairedLifecycle,decodeBlob}=await import('./compare.mjs');const l=await loadLifecycleSources();try{
  const native=collectNative(binary,l.bundle),def=l.bundle.corpus.cases[0],web=await runPendingLifecycleCase(l,def,l.bundle.corpus);verifyPairedLifecycle(l.bundle,web,native.cases[0]);
  const blob=d=>{const raw=JSON.stringify(d);return {base64:Buffer.from(raw).toString('base64'),sha256:sha256(raw),bytes:Buffer.byteLength(raw)};};
  const changed=structuredClone(native);for(const stage of ['applied','redo'])for(const kind of ['document','web']){const doc=decodeBlob(changed.cases[0].stages[stage][kind]).document;for(const g of doc.geometries.filter(g=>g.version===2)){const walk=v=>{if(!Array.isArray(v))return v;return v.map(x=>typeof x==='number'&&x===4?4.000000000000001:walk(x));};g.geojson.coordinates=walk(g.geojson.coordinates);}changed.cases[0].stages[stage][kind]=blob(doc);}
  // Own-runtime history, references and real-change shape invariants still pass;
  // the exact shared boundary must reject the one-ULP drift.
  verifyNativeLifecycleReport(l.bundle,changed);assert.throws(()=>verifyPairedLifecycle(l.bundle,web,changed.cases[0]),/exact shared geometry boundary/);
  for(const mutate of [
   n=>{for(const stage of ['applied','redo']){const d=decodeBlob(n.stages[stage].web).document;d.timelineRecords.parentRelations[2].parentId='target';n.stages[stage].web=blob(d);}},
   n=>{for(const stage of ['applied','redo']){const d=decodeBlob(n.stages[stage].web).document;d.territorialEntities.reverse();n.stages[stage].web=blob(d);}},
   n=>{n.stages.applied.history.canRedo=true;},
   n=>{n.stages.preview.selection.parts[0].geometry.coordinates[0][0][0]=0.125;},
   n=>{n.inputs[0].screen[0]=-1;},
   n=>{n.inputs[0].roundTrip[0]+=0.000000000000001;},
  ]){const n=structuredClone(native.cases[0]);mutate(n);assert.throws(()=>verifyPairedLifecycle(l.bundle,web,n));}
 }finally{l.cleanup();}
});
