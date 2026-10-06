import assert from 'node:assert/strict';
import test from 'node:test';
import {loadPendingNodeSources,pendingProjection} from './sources.mjs';
import {runPendingInputCase,verifyWebCase} from './runtime.mjs';
test('actual domain preserves empty-ready confirmation and pending suppression across the full corpus',async()=>{
 const loaded=await loadPendingNodeSources();
 try{for(const definition of loaded.bundle.corpus.cases){
  const row=await runPendingInputCase(loaded,definition,loaded.bundle.corpus);verifyWebCase(definition,row,loaded.bundle.corpus,pendingProjection(loaded.bundle,definition));
  if(definition.scenario==='held-back'){
   assert.equal(row.stages.afterLateCompletion.phase,'preparing','raw historical phase is retained');
   assert.equal(row.stages.afterLateCompletion.pending,false,'setup does not mean pending drawing preparation');
   assert.equal(row.stages.afterLateCompletion.preparationActive,false);
  }
  if(definition.scenario==='two-pending-empty-switch'){
   assert.equal(row.stages.ready.draftInputActive,true);assert.equal(row.stages.ready.coordinates.length,0);
   assert.equal(row.stages.switch.confirmation.method,'line');assert.equal(row.stages.switch.method,'polygon');
  }
  for(const mutate of [r=>delete r.stages.activation,r=>r.stages.activation.canonical+=' ',r=>r.stages.activation.projectUndo=1,r=>r.input.view.size.width++,r=>r.stages.activation.revision++,r=>r.transport=[],r=>{if(r.stages.firstReady)r.stages.firstReady.coordinates[0][0]++;else r.inputs.push({stage:'invented'});}]){
   const bad=structuredClone(row);mutate(bad);assert.throws(()=>verifyWebCase(definition,bad,loaded.bundle.corpus,pendingProjection(loaded.bundle,definition)));
  }
 }}finally{loaded.cleanup();}
});

test('case integrity rejects detached input, preparation and Worker delivery evidence',async t=>{
 const loaded=await loadPendingNodeSources();
 try{
  const corpus=loaded.bundle.corpus,rows=new Map();
  for(const definition of corpus.cases.filter(c=>c.profile.id==='desktop')){
   const row=await runPendingInputCase(loaded,definition,corpus);verifyWebCase(definition,row,corpus,pendingProjection(loaded.bundle,definition));rows.set(definition.scenario,row);
  }
  const reindex=row=>row.transport.forEach((event,index)=>{event.sequence=index;});
  const event=(row,kind)=>row.transport.find(e=>e.kind===kind&&(kind!=='response'&&kind!=='delivered'||e.message?.type==='result'));
  const mutations=[
   ['pending taps claim ready drawing','two-pending-empty-switch',r=>{r.inputs[0].prePhase='drawing';r.inputs[0].preDraftInputActive=true;}],
   ['preparation boundary invented','two-pending-empty-switch',r=>{r.inputs[0].prePreparation=true;}],
   ['detached screen coordinate','two-pending-empty-switch',r=>{r.inputs[0].screen=[99999,-99999];}],
   ['empty canonical evidence','two-pending-empty-switch',r=>{r.beforeCanonical='{}';for(const stage of Object.values(r.stages))stage.canonical='{}';}],
   ['missing canonical membership','two-pending-empty-switch',r=>{const doc=JSON.parse(r.beforeCanonical);doc.entities.pop();r.beforeCanonical=JSON.stringify(doc);for(const stage of Object.values(r.stages))stage.canonical=r.beforeCanonical;}],
   ['wrong declared coordinate','two-pending-empty-switch',r=>{r.inputs[0].coordinate=[99,88];}],
   ['pending tap accumulated draft history','two-pending-empty-switch',r=>{r.stages.firstPending.draftUndo=999;}],
   ['missing switch-again confirmation','two-pending-empty-switch',r=>{r.stages.switchAgain.confirmation=null;}],
   ['confirmation remains after confirm','two-pending-empty-switch',r=>{r.stages.confirmSwitch.confirmation={method:'polygon'};}],
   ['cancel mutated draft history','pending-ready-switch-history',r=>{r.stages.cancelSwitch.draftUndo++;}],
   ['undo removed wrong point','pending-ready-switch-history',r=>{r.stages.undoDraft.coordinates=[[99,88]];}],
   ['setup still claimed pending after Back','held-back',r=>{r.stages.afterLateCompletion.pending=true;}],
   ['Back tap unexpectedly stored','held-back',r=>{r.stages.tapAfterBack.coordinates=[[99,88]];}],
   ['result responses and deliveries omitted','held-clear',r=>{r.transport=r.transport.filter(e=>!['response','delivered'].includes(e.kind));reindex(r);}],
   ['orphan held request','held-clear',r=>{event(r,'held').requestId=444;}],
   ['orphan released request','held-clear',r=>{event(r,'released').requestId=999;}],
   ['delivery associated with wrong worker','held-clear',r=>{event(r,'delivered').worker=999;}],
   ['delivery changed original result payload','held-clear',r=>{event(r,'delivered').message.result={invented:true};}],
   ['result associated with wrong revision','held-clear',r=>{event(r,'response').message.targetRevision=999;}],
   ['result associated with wrong job','held-clear',r=>{event(r,'response').message.jobKey='other';}],
   ['result associated with wrong operation','held-clear',r=>{event(r,'response').operation='territory-selection';}],
   ['release moved before held','held-clear',r=>{const h=r.transport.indexOf(event(r,'held')),i=r.transport.indexOf(event(r,'released'));[r.transport[h],r.transport[i]]=[r.transport[i],r.transport[h]];reindex(r);}],
   ['late delivery before cancellation','held-clear',r=>{event(r,'delivered').phase='held';}],
   ['terminated result falsely marked live','held-clear',r=>{event(r,'delivered').terminated=false;}],
   ['cancel notification omitted','held-clear',r=>{r.transport=r.transport.filter(e=>e.message?.type!=='cancel');reindex(r);}],
   ['worker termination omitted','held-clear',r=>{r.transport=r.transport.filter(e=>e.kind!=='terminate');reindex(r);}],
   ['replacement Worker exchange omitted','held-supersede',r=>{r.transport=r.transport.filter(e=>e.worker===1);reindex(r);}],
   ['immediate supersession invents an old result','pending-switch',r=>{r.transport.push({sequence:r.transport.length,kind:'held',phase:'switchPending',worker:1,requestId:1});}],
  ];
  for(const [name,scenario,mutate]of mutations)await t.test(name,()=>{
   const row=structuredClone(rows.get(scenario));mutate(row);assert.throws(()=>verifyWebCase(row.input,row,corpus,pendingProjection(loaded.bundle,row.input)),undefined,name);
  });
 }finally{loaded.cleanup();}
});
