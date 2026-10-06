import assert from 'node:assert/strict';import test from 'node:test';
import {loadLifecycleSources} from './sources.mjs';import {runPendingLifecycleCase} from './runtime.mjs';import {verifyWebLifecycleCase} from './contract.mjs';
const loaded=await loadLifecycleSources();let row;try{row=await runPendingLifecycleCase(loaded,loaded.bundle.corpus.cases[0],loaded.bundle.corpus);}finally{loaded.cleanup();}
const check=r=>verifyWebLifecycleCase(loaded.bundle,loaded.bundle.corpus.cases[0],r);
const alter=(r,stage,fn)=>{const value=JSON.parse(r.stages[stage].canonical);fn(value);r.stages[stage].canonical=JSON.stringify(value);};
for(const [name,mutate]of [
 ['changed projected screen point',r=>{r.inputs[2].screen[0]++;}],
 ['changed actual inverse',r=>{r.inputs[2].roundTrip[0]+=0.000000000000001;}],
 ['leaked ready draft history',r=>{r.stages.ready.draftUndo=1;}],
 ['held result mislabeled after owner delivery',r=>{const h=r.transport.find(e=>e.kind==='held'),release=r.transport.find(e=>e.kind==='released'&&e.worker===h.worker&&e.requestId===h.requestId);r.transport=r.transport.filter(e=>e!==h&&e!==release);const i=r.transport.findIndex(e=>e.kind==='delivered'&&e.worker===h.worker&&e.message.requestId===h.requestId);r.transport.splice(i+1,0,h,release);r.transport.forEach((e,i)=>e.sequence=i);}],
 ['first held result released under retry label',r=>{r.transport.find(e=>e.kind==='released').phase='retryHeld';}],
 ['missing transport',r=>{r.transport=[];}],
 ['fake preview',r=>{r.stages.preview.preview={madeUp:true};}],
 ['empty Finish candidates',r=>{r.stages.finished.session.candidates=[];}],
 ['changed archive geometry',r=>{r.stages.preview.session.parts[0].geometry={type:'Polygon',coordinates:[]};}],
 ['missing Apply',r=>{delete r.stages.applied;}],
 ['unobserved Undo',r=>{r.stages.undo={observed:false};}],
 ['pending replayed point',r=>{r.stages.ready.coordinates=[[2,2]];}],
 ['changed geometry reference',r=>alter(r,'undo',p=>p.timelineRecords.geometryBindings[0].geometryRef.version++)],
 ['changed archive coordinate',r=>alter(r,'undo',p=>p.geometries[0].geojson.coordinates[0][0][0]++)],
 ['changed parent',r=>alter(r,'undo',p=>p.timelineRecords.parentRelations[2].parentId='target')],
 ['changed entity order',r=>alter(r,'undo',p=>p.territorialEntities.reverse())],
 ['changed presentation outside raw delta',r=>alter(r,'undo',p=>p.layerPresentation.objectOrder.reverse())],
 ['unexpected empty group',r=>alter(r,'undo',p=>p.itemVisibility.unexpected={})],
 ['changed preserved visibility',r=>alter(r,'undo',p=>p.itemVisibility.subunits['child-left']=true)],
 ['missing result event',r=>{r.transport.splice(r.transport.findIndex(e=>e.kind==='response'&&e.message.type==='result'),1);r.transport.forEach((e,i)=>e.sequence=i);}],
 ['altered result delivery',r=>{r.transport.find(e=>e.kind==='delivered'&&e.message.type==='result').message.result={};}],
])test('rejects '+name,()=>{const r=structuredClone(row);mutate(r);assert.throws(()=>check(r));});
