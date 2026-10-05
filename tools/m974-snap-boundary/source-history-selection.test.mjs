import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {readPinnedSources} from './sources.mjs';
import {createSelectionRuntime} from '../m97/web-selection.mjs';

const selection=await import('./source-history-selection.mjs').catch(error=>{
  if(error.code!=='ERR_MODULE_NOT_FOUND')throw error;
  return {};
});
const assembly=readPinnedSources().sources['assets/js/modules/app-domain-assembly.js'];

test('cut stimulus extracts exact production assessDraft and cancellation callbacks',async()=>{
  assert.equal(typeof selection.extractSourceHistoryCutCallbacks,'function','Exact production cut callbacks are required');
  const result=await selection.extractSourceHistoryCutCallbacks(assembly);
  assert.equal(result.source,Buffer.from(assembly).subarray(result.byteStart,result.byteEnd).toString());
  assert.equal(result.sha256,createHash('sha256').update(result.source).digest('hex'));
  assert.equal(result.sourceSha256,createHash('sha256').update(assembly).digest('hex'));
  assert.deepEqual(result.names,['cancelPreparation','assessDraft']);
  assert.match(result.source,/rememberCutPreparation\(source, coords, response.result\)/);
});

test('ambiguous cut callback extraction fails closed',async()=>{
  assert.equal(typeof selection.extractSourceHistoryCutCallbacks,'function');
  await assert.rejects(selection.extractSourceHistoryCutCallbacks(''),/missing|source/i);
  await assert.rejects(selection.extractSourceHistoryCutCallbacks(assembly+'\n        cancelPreparation: () => {}'),/unique|ambiguous/i);
});

test('selection stimulus serializes browser-safe runtime functions without a Node loader',()=>{
  assert.equal(typeof selection.sourceHistorySelectionRuntimeSource,'function','Portable selection runtime is required');
  const portable=Function(`return (${selection.sourceHistorySelectionRuntimeSource()});`)();
  for(const name of ['runSourceHistorySelection','prepareSourceHistorySelection','extractSourceHistoryCutCallbacks'])assert.equal(typeof portable[name],'function',name);
});

async function fixture() {
  const {loadSourceHistoryNodeSources}=await import('./source-history-sources.mjs');
  const loaded=await loadSourceHistoryNodeSources(),{api}=loaded;
  loaded.selectionRuntime={createSelectionRuntime};
  loaded.assemblySource=loaded.sourceTexts['assets/js/modules/app-domain-assembly.js'];
  const runtime=loaded.runtime.createRuntime(api),{state,entityRepository,entityStore}=runtime;
  const square=(a,b,c,d)=>({type:'Polygon',coordinates:[[[a,b],[a,d],[c,d],[c,b],[a,b]]]});
  const features=[['target','',[-5,0,0,10]],['donor','',[0,0,10,10]],['parent','',[30,0,40,10]],['child-left','parent',[30,0,35,10]],['child-right','parent',[35,0,40,10]]].map(([id,parentId,box])=>api.createTerritorialFeature({id,name:id,entityKind:'general',parentId,coverageMode:parentId?'partition':'explicit',geometry:square(...box)}));
  entityStore.restoreProject(api.createStaticTerritorialSnapshot(features));
  Object.assign(state,{labels:[],distributionEntries:[],labelSettings:{},itemVisibility:{},layerPresentation:{schemaVersion:4,styles:{},objectStyles:{},objectOrder:[]}});
  const transport=[],requests=[],holds=new Set(),held=[],stages=[];
  const until=async predicate=>{const end=Date.now()+15000;while(!predicate()){if(Date.now()>end)throw Error('Selection fixture did not settle');await new Promise(resolve=>setTimeout(resolve,5));}};
  const actualClient=api.createMapEditWorkerClient({getEntities:entityRepository.list,getFeatureById:entityRepository.get,getEditSources:()=>[...entityRepository.list().map(feature=>({kind:'territorial',feature})),...state.genericFeatures.map(feature=>({kind:'generic',feature}))],getTargetRevision:()=>state.stateRevision,createWorker:()=>{
    const worker=loaded.createWorker(),operations=new Map(),adapter={onmessage:null,onerror:null,postMessage(message){transport.push({direction:'request',message:structuredClone(message)});if(message.type==='execute')operations.set(message.requestId,message.operation);worker.postMessage(message);},terminate(){transport.push({direction:'terminate'});return worker.terminate();}};
    worker.onmessage=event=>{const operation=operations.get(event.data.requestId);transport.push({direction:'incoming',operation,message:structuredClone(event.data)});if(event.data.type==='result'&&holds.has(operation))held.push({operation,event,adapter});else adapter.onmessage?.(event);};
    worker.onerror=error=>adapter.onerror?.(error);return adapter;
  }});
  let pending=0;
  const client={...actualClient,async execute(operation,...args){requests.push({operation});pending++;try{return await actualClient.execute(operation,...args);}finally{pending--;}}};
  runtime.ports.spatialQuery.mapEditClient=client;
  const context={api,loaded,runtime,client,transport,requests,hold:operation=>holds.add(operation),waitHeld:operation=>until(()=>held.some(row=>row.operation===operation)),release(operation){holds.delete(operation);for(const row of held.filter(row=>row.operation===operation)){row.adapter.onmessage?.(row.event);held.splice(held.indexOf(row),1);}},record(name,extra){stages.push({name,...extra});},until,settleRequests:()=>until(()=>pending===0)};
  client.rebase();await until(()=>client.stats().ready);
  return {...context,stages,cleanup(){context.selectionHarness?.workflow.clear();client.stop();loaded.cleanup();}};
}

test('production components READY and pending cancellation preserve the actual stop distinction',async()=>{
  assert.equal(typeof selection.runSourceHistorySelection,'function','Actual selection stimulus required');
  for(const id of ['annex-components-ready','annex-components-pending-stop']){
    const context=await fixture();
    try{
      const before=structuredClone(context.runtime.entityRepository.list());
      const result=await selection.runSourceHistorySelection(context,{id});
      assert.equal(result.cancelled,true);
      assert.equal(result.beforeCancel.workerRequests>0,id.endsWith('pending-stop'));
      assert.equal(context.client.stats().workerActive,!id.endsWith('pending-stop'));
      assert.ok(context.requests.some(row=>row.operation==='territory-components'));
      assert.deepEqual(context.runtime.entityRepository.list(),before);
      assert.equal(context.runtime.state.history.length,0);
    }finally{context.cleanup();}
  }
});

test('production method-switch cache remains neutral after unrelated generic revision changes',async()=>{
  assert.equal(typeof selection.prepareSourceHistorySelection,'function');
  const context=await fixture();
  try{
    await selection.prepareSourceHistorySelection(context,{id:'annex-components-cache-hit'});
    const offset=context.requests.length;
    context.runtime.state.stateRevision++;
    const result=await selection.runSourceHistorySelection(context,{id:'annex-components-cache-hit'});
    assert.equal(result.cacheHit,true);
    assert.deepEqual(context.requests.slice(offset),[]);
    assert.equal(context.client.stats().ready,true);
    assert.equal(result.beforeCancel.activeMethod,'components');
  }finally{context.cleanup();}
});

test('root and child cuts plus entity polygon reach real previews without canonical apply',async()=>{
  assert.equal(typeof selection.runSourceHistorySelection,'function');
  for(const [id,required]of [['root-line-cut-preview',['territorial-cut','territory-selection','new-country']],['child-line-cut-preview',['territorial-source','territorial-cut','territory-selection','territorial-edit']],['entity-polygon-preview',['territorial-source','territorial-drawn','territory-selection','territorial-edit']]]){
    const context=await fixture();
    try{
      const before=structuredClone(context.runtime.entityRepository.list());
      const result=await selection.runSourceHistorySelection(context,{id});
      assert.equal(result.previewReady,true,JSON.stringify(result));
      assert.deepEqual(context.stages.find(row=>row.name==='operation').diagnostics,result.errors,'Every workflow diagnostic must be observable in the enclosing source-history capture');
      if(id==='child-line-cut-preview')assert.ok(result.errors.some(row=>row.cancelled&&row.code==='PL-TERRITORIAL-SOURCE'),'Public child setup naturally records its coalesced territorial-source cancellation');
      for(const operation of required)assert.ok(context.requests.some(row=>row.operation===operation),`${id}: ${operation}`);
      assert.deepEqual(context.runtime.entityRepository.list(),before);
      assert.equal(context.runtime.state.history.length,0);
      assert.equal(context.runtime.state.geometryPreview.session,null);
      assert.equal(context.client.stats().ready,true);
    }finally{context.cleanup();}
  }
});

test('shared setup-only and split-components production entrypoints report their real client activity',async()=>{
  assert.equal(typeof selection.runSourceHistorySelection,'function');
  for(const id of ['split-setup-only','annex-setup-only','split-components-ready']){
    const context=await fixture();
    try{
      const result=await selection.runSourceHistorySelection(context,{id,rootTargetId:'target',sourceId:'donor'});
      assert.equal(result.cancelled,true);
      assert.deepEqual(context.requests.map(row=>row.operation),id.endsWith('setup-only')?[]:['territory-components']);
      assert.equal(context.client.stats().ready,true);
    }finally{context.cleanup();}
  }
});

test('component timer cancellation is neutral while actual pending selection cancellation stops the shared Worker',async()=>{
  assert.equal(typeof selection.prepareSourceHistorySelection,'function');
  for(const id of ['selection-timer-only-cancel','selection-request-pending-cancel']){
    const context=await fixture();
    try{
      const definition={id,rootTargetId:'target',sourceId:'donor'};
      await selection.prepareSourceHistorySelection(context,definition);
      const offset=context.requests.length;
      context.runtime.state.stateRevision++;
      const result=await selection.runSourceHistorySelection(context,definition);
      assert.equal(result.beforeCancel.computationPending,true);
      assert.equal(result.beforeCancel.workerRequests>0,id==='selection-request-pending-cancel');
      assert.deepEqual(context.requests.slice(offset).map(row=>row.operation),id==='selection-timer-only-cancel'?[]:['territory-selection']);
      assert.equal(context.client.stats().workerActive,id==='selection-timer-only-cancel');
    }finally{context.cleanup();}
  }
});

test('cold component activation distinguishes pre-execute cancellation from held real component results',async()=>{
  for(const id of ['component-timer-only-cancel','component-request-pending-cancel']){
    const context=await fixture();
    try{
      const definition={id,rootTargetId:'target',sourceId:'donor'};
      await selection.prepareSourceHistorySelection(context,definition);
      assert.equal(context.requests.length,0,'Cold component stimuli must not be prewarmed');
      const result=await selection.runSourceHistorySelection(context,definition);
      assert.equal(result.beforeCancel.activePhase,'preparing');
      assert.equal(result.beforeCancel.workerRequests>0,id==='component-request-pending-cancel');
      assert.deepEqual(context.requests.map(row=>row.operation),id==='component-timer-only-cancel'?[]:['territory-components']);
      assert.equal(context.client.stats().workerActive,id==='component-timer-only-cancel');
    }finally{context.cleanup();}
  }
});

test('portable selection stimuli preserve observed ga/gb history through production generic delete and Undo',async()=>{
  const {loadSourceHistoryNodeSources}=await import('./source-history-sources.mjs');
  const {runSourceHistoryCase}=await import('./source-history-runtime.mjs');
  const loaded=await loadSourceHistoryNodeSources();
  loaded.selectionRuntime={createSelectionRuntime,...Function(`return (${selection.sourceHistorySelectionRuntimeSource()});`)()};
  loaded.assemblySource=loaded.sourceTexts['assets/js/modules/app-domain-assembly.js'];
  try{
    for(const [id,winner]of [['split-setup-only','ga'],['annex-setup-only','ga'],['split-components-ready','gb'],['annex-components-ready','gb'],['component-timer-only-cancel','ga'],['component-request-pending-cancel','ga'],['annex-components-cache-hit','ga'],['root-line-cut-after-stop','gb'],['entity-polygon-after-stop','gb'],['annex-preview-after-stop','gb']]){
      const row=await runSourceHistoryCase(loaded,{id,scenario:'selection'});
      assert.equal(row.stages.warm.winner,'ga',id);
      assert.equal(row.stages.final.winner,winner,id);
      assert.equal(row.stages.restored.canonicalEqual,true,id);
      assert.equal(row.stages.selectionCancelled.worker.workerActive,id!=='component-request-pending-cancel',id);
      assert.equal(row.stages.operation.selection.workerRequests>0,id==='component-request-pending-cancel',id);
      assert.deepEqual(row.errors,[],id);
      if(id==='annex-components-cache-hit'){
        assert.equal(row.stages.operation.cacheHit,true);
        assert.deepEqual(row.requests.filter(request=>request.phase==='removed'),[],'Generic absence must remain unobserved during cached activation');
      }
      if(id.endsWith('-after-stop')){
        assert.equal(row.stages.operation.nativeCase,null);
        assert.equal(row.stages.operation.previewReady,true);
        assert.equal(row.stages.controlledStop.controlledStop.sourceRows.includes('generic:ga'),false);
        assert.equal(row.stages.controlledStop.controlledStop.sourceRows.includes('generic:gb'),true);
        assert.equal(row.stages.operation.restartEvidence.message.type,'rebase');
      }
    }
  }finally{loaded.cleanup();}
});

test('controlled stops restart real cut, drawn and annex preview operations without canonical apply',async()=>{
  for(const [id,operation,phase]of [['root-line-cut-after-stop','territorial-cut','method-ready-before-cut'],['entity-polygon-after-stop','territorial-drawn','method-ready-before-drawn'],['annex-preview-after-stop','annex','annex-preview-entry']]){
    const context=await fixture();
    try{
      const before=structuredClone(context.runtime.entityRepository.list());
      const result=await selection.runSourceHistorySelection(context,{id,nativeCase:null});
      assert.equal(result.previewReady,true,id);
      assert.equal(result.nativeCase,null);
      const stop=result.controlledStop;
      assert.equal(stop.injected,true);
      assert.equal(stop.phase,phase);
      assert.equal(stop.operation,operation);
      assert.equal(stop.workerBefore.ready,true);
      assert.equal(stop.workerAfter.workerActive,false);
      assert.ok(stop.sourceRows.includes('territorial:donor'));
      assert.deepEqual(context.stages.find(row=>row.name==='controlledStop').controlledStop,stop);
      const later=context.transport.slice(stop.transportEnd);
      const rebaseIndex=later.findIndex(row=>row.direction==='request'&&row.message.type==='rebase');
      const executeIndex=later.findIndex(row=>row.direction==='request'&&row.message.type==='execute');
      assert.ok(rebaseIndex>=0&&executeIndex>rebaseIndex,id);
      assert.equal(later[executeIndex].message.operation,operation,id);
      assert.equal(result.restartEvidence.message.type,'rebase');
      assert.equal(context.transport.filter(row=>row.direction==='terminate').length,1,'Only the explicitly injected stop terminates the Worker');
      assert.equal(context.client.stats().ready,true,'Cancellation of READY preview keeps the restarted Worker alive');
      assert.deepEqual(context.runtime.entityRepository.list(),before);
      assert.equal(context.runtime.state.history.length,0);
      assert.equal(context.runtime.state.geometryPreview.session,null);
      if(operation==='annex'){
        assert.ok(context.requests.some(row=>row.operation==='territory-selection'));
        assert.equal(context.requests.at(-1).operation,'annex');
      }
    }finally{context.cleanup();}
  }
});

test('deselecting the last component preserves held worker ownership until workflow clear stops it',async()=>{
  const context=await fixture();
  try{
    const result=await selection.runSourceHistorySelection(context,{id:'pending-selection-deselect-clear',nativeCase:null});
    assert.equal(result.nativeCase,null);
    assert.equal(result.beforeDeselect.workerRequests,1);
    assert.equal(result.beforeDeselect.selectedComponentKeys.length,1);
    assert.equal(result.afterDeselect.workerRequests,1);
    assert.deepEqual(result.afterDeselect.selectedComponentKeys,[]);
    assert.equal(result.afterDeselect.computationPending,false);
    assert.equal(result.afterMicrotasks.workerRequests,1);
    assert.deepEqual(result.afterMicrotasks.selectedComponentKeys,[]);
    assert.equal(result.ownerMicrotaskDrains,2);
    assert.equal(result.beforeCancel.workerRequests,1);
    assert.equal(result.cancelled,true);
    assert.equal(context.client.stats().workerActive,false);
    assert.deepEqual(context.requests.map(row=>row.operation),['territory-components','territory-selection']);
    const stage=context.stages.find(row=>row.name==='operation');
    assert.deepEqual(stage.beforeDeselect,result.beforeDeselect);
    assert.deepEqual(stage.afterMicrotasks,result.afterMicrotasks);
    assert.equal(stage.nativeCase,null);
    assert.equal(context.runtime.state.history.length,0);
    assert.equal(context.runtime.state.geometryPreview.session,null);
  }finally{context.cleanup();}
});

test('held selection deselect-clear restores ga through actual generic Undo and lazy Worker restart',async()=>{
  const {loadSourceHistoryNodeSources}=await import('./source-history-sources.mjs');
  const {runSourceHistoryCase}=await import('./source-history-runtime.mjs');
  const loaded=await loadSourceHistoryNodeSources();
  loaded.selectionRuntime={createSelectionRuntime,...Function(`return (${selection.sourceHistorySelectionRuntimeSource()});`)()};
  try{
    const row=await runSourceHistoryCase(loaded,{id:'pending-selection-deselect-clear',scenario:'selection',nativeCase:null});
    assert.equal(row.stages.operation.afterMicrotasks.workerRequests,1);
    assert.equal(row.stages.operation.selection.workerRequests,1);
    assert.equal(row.stages.selectionCancelled.worker.workerActive,false);
    assert.equal(row.stages.restored.canonicalEqual,true);
    assert.equal(row.stages.final.winner,'ga');
    assert.equal(row.requests.filter(request=>request.operation==='territory-selection').length,1);
    assert.equal(row.requests.find(request=>request.operation==='territory-selection').status,'rejected');
    assert.equal(row.transport.filter(event=>event.direction==='request'&&event.message?.type==='rebase').length,2);
  }finally{loaded.cleanup();}
});
