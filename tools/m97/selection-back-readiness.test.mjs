import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {productionModules,createRuntime,observe,workerFactory} from './web-lifecycle.mjs';
import {loadSelectionModules,createSelectionRuntime,seedSelectionFeatures,settle,defaultSelectionSourceRoot} from './web-selection.mjs';
import {prepareCorrectedSelectionSources} from './web-selection-correction.mjs';

// Actual pinned workflow + worker/client/store/history execution. This Node
// regression is not browser evidence and does not change the browser corpus.
for(const corrected of [false,true])for(const method of ['polygon','line']){
  test(`${corrected?'12cd8c8 correction':'53dbd3c original'} ${method}: setup hides readiness while retaining the same preview receipt`,async()=>{
    const overlay=corrected?prepareCorrectedSelectionSources():null;
    const loaded=await productionModules(),api={...loaded.api,...await loadSelectionModules(overlay?.root||defaultSelectionSourceRoot)};
    const runtime=createRuntime(api),features=seedSelectionFeatures(api,{remote:false});
    runtime.entityStore.restoreProject(api.createStaticTerritorialSnapshot(features));
    Object.assign(runtime.state,{distributionEntries:[],labels:[],labelSettings:{},itemVisibility:{},layerPresentation:{schemaVersion:4,styles:{},objectStyles:{},objectOrder:[]}});
    runtime.state.historyDirtyEntityIds.clear();
    const client=api.createMapEditWorkerClient({createWorker:workerFactory(loaded.root,loaded.manifest),getEntities:runtime.entityRepository.list,getFeatureById:runtime.entityRepository.get,getTargetRevision:()=>runtime.state.stateRevision});
    runtime.ports.spatialQuery.mapEditClient=client;
    const h=createSelectionRuntime(api,{lifecycle:runtime,features});
    const canonical=()=>JSON.stringify({document:observe(runtime).document,labels:runtime.state.labels,presentation:observe(runtime).presentation});
    const before=canonical(),beforeHistory=JSON.stringify({history:runtime.state.history,future:runtime.state.future}),beforeRevision=runtime.state.stateRevision;
    try {
      assert.ok(h.workflow.start('annex',{targetCountryId:'target',sourceCountryIds:['donor']}));
      assert.equal(await h.workflow.advance(),true);assert.equal(await h.workflow.selectMethod(method),true);await settle(h);
      h.setDraft(method==='line'?[[-1,5],[11,5]]:[[-1,0],[4,0],[4,10],[-1,10]]);
      assert.equal(h.workflow.finishDraft(),true);await settle(h);assert.equal(h.workflow.previewReady(),true);
      assert.equal(h.workflow.addPart(),true);await settle(h);
      const receipt=runtime.state.geometryPreview.session,receiptBytes=JSON.stringify(receipt),readyKey=h.workflow.activeSession().previewReadyKey;
      assert.ok(receipt?.workerRequestId,'Actual worker-backed preview required');assert.ok(readyKey);
      const stages=[];
      const capture=(name,stage,ready)=>{
        assert.equal(h.workflow.activeSession().stage,stage);assert.equal(h.workflow.previewReady(),ready);
        assert.strictEqual(runtime.state.geometryPreview.session,receipt,'Same live preview session object');
        assert.equal(JSON.stringify(runtime.state.geometryPreview.session),receiptBytes,'Complete preview receipt remains unchanged');
        assert.equal(h.workflow.activeSession().previewReadyKey,readyKey,'Ready key preserved across navigation');
        assert.equal(canonical(),before);assert.equal(JSON.stringify({history:runtime.state.history,future:runtime.state.future}),beforeHistory);assert.equal(runtime.state.stateRevision,beforeRevision);
        stages.push({name,stage,previewReady:h.workflow.previewReady(),samePreviewSession:true,sameReceipt:true,workerRequestId:receipt.workerRequestId,previewSessionId:receipt.sessionId,previewReadyKey:readyKey,canonicalUnchanged:true,historyUnchanged:true,revisionUnchanged:true});
      };
      capture('ready-selection','selection',true);
      assert.equal(await h.workflow.advance(),true);await settle(h);capture('review','review',true);
      assert.equal(h.workflow.back(),true);await settle(h);capture('back-to-selection','selection',true);
      assert.equal(h.workflow.back(),true);await settle(h);capture('back-to-setup','setup',false);
      assert.equal(await h.workflow.advance(),true);await settle(h);capture('advance-to-selection','selection',true);
      console.log(JSON.stringify({diagnostic:'nonriver-back-readiness',behavioralCommit:corrected?overlay.manifest.behavioralCommit:loaded.manifest.behavioralCommit,method,canonicalSha256:createHash('sha256').update(before).digest('hex'),stages}));
    }finally{h.workflow.clear();client.stop();overlay?.cleanup();}
  });
}
