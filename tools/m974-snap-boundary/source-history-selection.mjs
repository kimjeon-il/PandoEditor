// Portable stimulus wiring. Geometry calculations and workflow transitions are
// exclusively the frozen production modules supplied by the verified loader.
export async function extractSourceHistoryCutCallbacks(source) {
  if (typeof source !== 'string' || !source) throw Error('Missing cut callback source');
  const markers=['        cancelPreparation: () => {','        requestFrame: callback => requestAnimationFrame(callback),'];
  const indexes=markers.map(marker=>{
    const index=source.indexOf(marker);
    if(index<0||source.indexOf(marker,index+marker.length)>=0)throw Error('Cut callback seam must be unique, not ambiguous');
    return index;
  });
  if(indexes[1]<=indexes[0])throw Error('Cut callback seam order changed');
  const text=source.slice(indexes[0],indexes[1]),names=['cancelPreparation','assessDraft'];
  if(names.some(name=>text.split(`${name}:`).length!==2))throw Error('Cut callback properties must be unique');
  const bytes=value=>new TextEncoder().encode(value);
  const hash=async value=>[...new Uint8Array(await crypto.subtle.digest('SHA-256',bytes(value)))].map(value=>value.toString(16).padStart(2,'0')).join('');
  return {sourcePath:'assets/js/modules/app-domain-assembly.js',names,source:text,sha256:await hash(text),sourceSha256:await hash(source),byteStart:bytes(source.slice(0,indexes[0])).length,byteEnd:bytes(source.slice(0,indexes[1])).length};
}

export async function prepareSourceHistorySelection(context,definition) {
  if(!['annex-components-cache-hit','selection-timer-only-cancel','selection-request-pending-cancel'].includes(definition.id))return null;
  return runSourceHistorySelection(context,{...definition,phase:'prepare'});
}

export async function runSourceHistorySelection(context,definition) {
  const {api,loaded,runtime,client}=context;
  const {state,entityRepository}=runtime;
  const clone=value=>structuredClone(value),noop=()=>{};
  const require=(condition,message)=>{if(!condition)throw Error(message);};
  const ids=['annex-components-ready','annex-components-pending-stop','annex-components-cache-hit','root-line-cut-preview','child-line-cut-preview','entity-polygon-preview','split-setup-only','annex-setup-only','split-components-ready','component-timer-only-cancel','component-request-pending-cancel','selection-timer-only-cancel','selection-request-pending-cancel','root-line-cut-after-stop','entity-polygon-after-stop','annex-preview-after-stop','pending-selection-deselect-clear'];
  require(ids.includes(definition.id),'Unknown production selection stimulus '+definition.id);
  const requestOffset=context.requests.length;
  const observe=h=>{
    const current=h.workflow.activeSession();
    if(!current)return null;
    return {id:current.id,kind:current.kind,stage:current.stage,activePhase:current.activePhase,activeMethod:current.activeMethod,sourceRevision:current.sourceRevision,componentKey:current.componentIndex?.key||null,componentCount:current.componentIndex?.items?.length??h.components.territoryComponentItems().length,workerRequests:current.workerRequests||0,computationPending:current.computationPending,preparationPending:!!current.preparation,previewPending:current.previewPending,previewReady:h.workflow.previewReady(),selectedCandidateIds:clone(current.selectedCandidateIds),selectedComponentKeys:clone(current.selectedComponentKeys),candidateCount:current.candidates.length};
  };
  const settle=async h=>{
    await context.until(()=>{
      if(h.errors.some(error=>!error.cancelled))throw Error('Production selection reported errors: '+JSON.stringify(h.errors));
      const current=h.workflow.activeSession();
      return !current||(!current.computationPending&&!current.previewPending&&!current.preparation&&!current.workerRequests&&!current.setupSourceCache?.pending);
    });
    await context.settleRequests();
  };
  let h=context.selectionHarness;
  if(!h){
    require(typeof loaded.selectionRuntime?.createSelectionRuntime==='function','Missing verified selection runtime ports');
    h=loaded.selectionRuntime.createSelectionRuntime(api,{lifecycle:runtime,features:entityRepository.list()});
    context.selectionHarness=h;
    // The original helper's calculator-only adapter is never invoked: all
    // selection and preview operations share the source-history real client.
    h.ports.spatialQuery.mapEditClient=client;
    h.ports.feedback.reportOperationError=(error,message,code)=>{h.errors.push({code,message:error.message,cancelled:error.cancelled===true});return message;};
    const validation=api.createCountryValidation();validation.connect(h.ports);h.ports.countryValidation={...h.ports.countryValidation,...validation};
    const picking=api.createObjectPicking();picking.connect(h.ports);h.ports.objectPicking=picking;
    h.ports.territorySelectionA={...h.ports.territorySelectionA,startTerritorySelection:h.workflow.start,resetTerritorySelection:h.workflow.resetSelection};
    h.ports.territorySelectionB={territorySelectionBack:h.workflow.back,territorySelectionPreviewIsCurrent:h.workflow.previewIsCurrent};
    h.ports.applicationServicesB={...api,sphericalGeometryAreaKm2:api.geometryAreaKm2};
    h.ports.platform.$=()=>({select:noop});
    h.ports.projectRestore={openConfirmModal(){throw Error('Read-only selection stimulus unexpectedly requested Apply confirmation');}};
    const cut=api.createCutGeometry();cut.connect(h.ports);h.ports.cutOperations=cut;
    const drafts=api.createTerritorialDrafts();drafts.connect(h.ports);h.ports.territorialEditingA=drafts;h.ports.territorialEditingB=drafts;h.sourceHistoryDrafts=drafts;
    globalThis.requestAnimationFrame ||= callback=>{callback();return 0;};
  }
  const deselectClear=definition.id==='pending-selection-deselect-clear';
  const annex=definition.id.startsWith('annex-')||deselectClear;
  const pending=['annex-components-pending-stop','component-request-pending-cancel','selection-request-pending-cancel','pending-selection-deselect-clear'].includes(definition.id);
  const cache=definition.id==='annex-components-cache-hit';
  const timer=definition.id==='selection-timer-only-cancel';
  const selecting=timer||definition.id==='selection-request-pending-cancel';
  const coldComponents=definition.id.startsWith('component-');
  const coldTimer=definition.id==='component-timer-only-cancel';
  const setupOnly=definition.id.endsWith('setup-only');
  const controlled=definition.id.endsWith('-after-stop');
  const annexAfterStop=definition.id==='annex-preview-after-stop';
  const targetId=definition.rootTargetId||'target',sourceId=definition.sourceId||'donor';
  const pendingOperation=selecting||deselectClear?'territory-selection':'territory-components';
  const start=async()=>{
    if(annex)require(h.workflow.start('annex',{targetCountryId:targetId,sourceCountryIds:[sourceId]}),'Annex setup did not start');
    else{
      require(h.sourceHistoryDrafts.enterTerritorialUnitSplitMode(sourceId),'Split setup did not start');
      require(h.workflow.toggleSourceCountry(sourceId),'Split root source picking failed');
    }
    require(await h.workflow.advance(),'Selection setup did not advance');
  };
  let extraction=null,assessment=null,callbacks=null,beforeCancel=null,previewReady=false;
  let controlledStop=null,restartEvidence=null,originalGeometryOperations=null;
  const injectControlledStop=(phase,operation)=>{
    require(controlled&&!controlledStop,'Controlled lifecycle diagnostic must inject exactly one stop');
    const workerBefore=clone(client.stats());
    require(workerBefore.ready&&workerBefore.workerActive,'Controlled stop must follow a READY real Worker operation');
    const sourceRows=[...entityRepository.list().map(feature=>'territorial:'+feature.id),...state.genericFeatures.map(feature=>'generic:'+feature.id),...state.hydroEdits.map(feature=>'hydro:'+feature.id)];
    const transportStart=context.transport.length;
    client.stop();
    controlledStop={injected:true,phase,operation,sourceRows,workerBefore,workerAfter:clone(client.stats()),transportStart,transportEnd:context.transport.length};
    require(!controlledStop.workerAfter.workerActive,'Controlled stop did not retire the real Worker');
    context.record('controlledStop',{controlledStop:clone(controlledStop),nativeCase:null});
  };
  if(definition.phase==='prepare'){
    require(cache||selecting,'Only cached/timer/component-selection stimuli have a pre-deletion setup phase');
    require(!h.workflow.activeSession(),'Cache setup must start with no active selection');
    await start();
    require(await h.workflow.selectMethod('components'),'Annex component setup failed');
    await settle(h);
    h.sourceHistoryComponentIndex=h.workflow.activeSession().componentIndex;
    require(h.sourceHistoryComponentIndex?.key,'Component cache was not prepared');
    const prepared=observe(h);context.record('selectionCachePrepared',{selection:prepared});
    return prepared;
  }
  try {
    if(deselectClear){
      await start();
      require(await h.workflow.selectMethod('components'),'Deselect-clear component activation failed');
      await settle(h);
      const component=h.components.territoryComponentItems()[0];
      require(component,'No production component for deselect-clear diagnostic');
      context.hold('territory-selection');
      require(h.workflow.toggleComponent(component.key),'Initial component selection failed');
      await context.waitHeld('territory-selection');
      const beforeDeselect=observe(h);
      require(beforeDeselect.workerRequests===1&&beforeDeselect.selectedComponentKeys.length===1,'Deselect-clear requires exactly one held selection request and selected component');
      require(h.workflow.toggleComponent(component.key),'Last-component deselection failed');
      const afterDeselect=observe(h);
      require(afterDeselect.workerRequests===1&&afterDeselect.selectedComponentKeys.length===0&&!afterDeselect.computationPending,'Empty selection must retain ownership of its held worker request');
      await Promise.resolve();
      await Promise.resolve();
      const afterMicrotasks=observe(h);
      require(afterMicrotasks.workerRequests===1&&afterMicrotasks.selectedComponentKeys.length===0,'Held worker ownership was lost while draining owner microtasks');
      require(client.stats().workerActive,'Deselection alone unexpectedly stopped the real Worker');
      beforeCancel=afterMicrotasks;
      const evidence={beforeDeselect,afterDeselect,afterMicrotasks,ownerMicrotaskDrains:2,nativeCase:null};
      context.record('operation',{selection:beforeCancel,...clone(evidence),heldOperation:'territory-selection',heldResultIsWorkerComplete:true,diagnostics:clone(h.errors)});
      require(h.workflow.clear(),'Deselect-clear production workflow cancellation failed');
      require(!client.stats().workerActive,'Workflow clear must stop the still-owned held selection request');
      context.record('selectionCancelled',{selection:observe(h),workerStopped:true});
      context.release('territory-selection');
      await settle(h);
      return {kind:'production-selection',cancelled:true,beforeCancel,previewReady:false,cacheHit:false,operations:context.requests.slice(requestOffset).map(row=>row.operation),errors:clone(h.errors),...evidence};
    }else if(cache){
      require(h.workflow.activeSession()?.componentIndex===h.sourceHistoryComponentIndex&&!!h.sourceHistoryComponentIndex?.key,'Component cache-hit stimulus requires pre-deletion prepareSourceHistorySelection');
      require(await h.workflow.selectMethod('line'),'Cached component→line activation failed');
      require(await h.workflow.selectMethod('components'),'Cached line→component activation failed');
      await settle(h);
      require(h.workflow.activeSession().componentIndex===h.sourceHistoryComponentIndex,'Production component cache identity changed');
      require(context.requests.length===requestOffset,'Intended component cache hit emitted an actual client request');
      beforeCancel=observe(h);
    }else if(annex||definition.id.startsWith('split-')||selecting||coldComponents){
      if(selecting){
        require(h.workflow.activeSession()?.componentIndex===h.sourceHistoryComponentIndex&&!!h.sourceHistoryComponentIndex?.key,'Selection timer/request stimulus requires prepared components');
      }else await start();
      if(setupOnly){beforeCancel=observe(h);}
      else{
      if(pending)context.hold(pendingOperation);
      let activation;
      if(selecting){
        const item=h.components.territoryComponentItems()[0];require(item,'No production component to toggle');
        require(h.workflow.toggleComponent(item.key),'Production component toggle failed');
        activation=Promise.resolve(true);
      }else activation=h.workflow.selectMethod('components');
      if(coldTimer){
        beforeCancel=observe(h);
        require(beforeCancel.activePhase==='preparing'&&beforeCancel.workerRequests===0&&context.requests.length===requestOffset,'Cold activation cancellation must precede client execute');
        const cancellationStage='production activation awaiting source preparation; no browser timer or client execute';
        context.record('operation',{selection:beforeCancel,cancellationStage,diagnostics:clone(h.errors)});
        h.workflow.clear();
        require(await activation===false,'Cancelled production method activation unexpectedly succeeded');
        await settle(h);
        require(context.requests.length===requestOffset,'Pre-execute cancellation emitted a client request afterward');
        context.record('selectionCancelled',{selection:observe(h),workerStopped:!client.stats().workerActive});
        return {kind:'production-selection',cancelled:true,beforeCancel,previewReady:false,cacheHit:false,cancellationStage,operations:[],errors:clone(h.errors)};
      }
      if(pending){
        await context.waitHeld(pendingOperation);
        beforeCancel=observe(h);
        require(beforeCancel.workerRequests>0&&(selecting?beforeCancel.computationPending:beforeCancel.preparationPending),'Pending cancellation must observe the real counted request');
        context.record('operation',{selection:beforeCancel,heldOperation:pendingOperation,heldResultIsWorkerComplete:true,diagnostics:clone(h.errors)});
        h.workflow.clear();context.record('selectionCancelled',{selection:observe(h),workerStopped:!client.stats().workerActive});
        context.release(pendingOperation);await activation;await settle(h);
        require(!client.stats().workerActive,'Pending production cancellation did not stop the Worker');
        return {kind:'production-selection',cancelled:true,beforeCancel,previewReady:false,cacheHit:false,operations:context.requests.slice(requestOffset).map(row=>row.operation),errors:clone(h.errors)};
      }
      require(await activation,'Component activation failed');
      if(!timer)await settle(h);
      if(annexAfterStop){
        originalGeometryOperations=h.ports.geometryOperations;
        h.ports.geometryOperations={...originalGeometryOperations,async beginWorkerGeometryPreview(request){
          require(request.operation==='annex','Controlled annex stop must wrap the actual annex preview');
          const current=h.workflow.activeSession();
          require(current?.combinedGeometry&&!current.computationPending&&!current.workerRequests,'Annex controlled stop must follow accepted territory-selection completion');
          require(context.requests.slice(requestOffset).some(row=>row.operation==='territory-selection'),'Annex controlled stop requires an actual territory-selection request');
          injectControlledStop('annex-preview-entry','annex');
          return originalGeometryOperations.beginWorkerGeometryPreview(request);
        }};
        const component=h.components.territoryComponentItems()[0];require(component,'No annex component to select');
        require(h.workflow.toggleComponent(component.key),'Annex component selection failed');
        await settle(h);
        previewReady=h.workflow.previewReady();
        require(previewReady&&!!state.geometryPreview.session,'Annex controlled-stop workflow did not reach a READY preview');
      }
      beforeCancel=observe(h);
      if(timer)require(beforeCancel.computationPending&&beforeCancel.workerRequests===0&&context.requests.length===requestOffset,'Timer-only cancellation must precede actual client execute');
      }
    }else{
      const root=definition.id.startsWith('root-line-cut-'),polygon=definition.id.startsWith('entity-polygon-');
      const sourceId=definition.sourceId||(root?'donor':'child-left'),source=entityRepository.get(sourceId);
      require(source?.geometry,'Missing stimulus source '+sourceId);
      const bounds=[Infinity,Infinity,-Infinity,-Infinity];
      const visit=value=>{if(Array.isArray(value)&&typeof value[0]==='number'){bounds[0]=Math.min(bounds[0],value[0]);bounds[1]=Math.min(bounds[1],value[1]);bounds[2]=Math.max(bounds[2],value[0]);bounds[3]=Math.max(bounds[3],value[1]);}else if(Array.isArray(value))value.forEach(visit);};
      visit(source.geometry.coordinates);
      if(polygon){require(h.sourceHistoryDrafts.enterTerritorialCreateWorkflow({parentId:source.properties.parentId}),'Entity create entry failed');h.sourceHistoryDrafts.updateTerritorialCreateSource(sourceId);}
      else require(h.sourceHistoryDrafts.enterTerritorialUnitSplitMode(sourceId),'Entity split entry failed');
      if(root)require(h.workflow.toggleSourceCountry(sourceId),'Root source picking failed');
      // Child setup may send multiple actual territorial-source requests while
      // its setup cache is invalidated. Preserve those observations in the trace.
      await settle(h);
      require(await h.workflow.advance(),'Entity setup did not advance');
      require(await h.workflow.selectMethod(polygon?'polygon':'line'),'Entity selection method activation failed');
      await settle(h);
      if(controlled)injectControlledStop(polygon?'method-ready-before-drawn':'method-ready-before-cut',polygon?'territorial-drawn':'territorial-cut');
      const [x0,y0,x1,y1]=bounds,dx=x1-x0,dy=y1-y0;
      const coords=polygon?[[x0+dx*.2,y0+dy*.2],[x0+dx*.8,y0+dy*.2],[x0+dx*.8,y0+dy*.6],[x0+dx*.2,y0+dy*.6]]:[[x0-1,(y0+y1)/2],[x1+1,(y0+y1)/2]];
      h.setDraft(coords);
      if(!polygon){
        require(typeof api.rememberCutPreparation==='function'&&typeof api.freezeEditingGeometry==='function','Missing actual cut cache and geometry-freezing exports');
        extraction=await extractSourceHistoryCutCallbacks(loaded.assemblySource||loaded.sourceTexts['assets/js/modules/app-domain-assembly.js']);
        const view=h.cutView,projection=api.d3.geo.equirectangular().scale(view.scale).translate(view.translate).rotate(view.rotate).center(view.center);
        state.projection=view.kind;state.size=clone(view.size);
        const dependencies={projectState:{state},draftPresentation:{activeCutDraftSourceGeometry:()=>h.workflow.activeSession()?.workingSourceGeometry},mapView:{activeProjection:()=>projection},geometryValidation:{CUT_ENDPOINT_SNAP_DISTANCE:clone(view.snapDistance)},spatialQuery:{mapEditClient:client}};
        callbacks=Function('dependencies','projectDomain','geometryRevision','freezeEditingGeometry','rememberCutPreparation',`const cutSources=new WeakMap();let cutSourceSequence=0,cutRequest=null,confirmedCutSource=null;return ({${extraction.source}});`)(dependencies,runtime.domains.projectDomain,api.geometryRevision,api.freezeEditingGeometry,api.rememberCutPreparation);
        assessment=await callbacks.assessDraft({tool:state.tool,coords,buildPreview:true});
        require(assessment?.valid&&assessment.split?.candidates?.length,'Production cut assessment did not prepare split candidates');
        // Enforce the cache-only browser contract even in Node: the real cut
        // consumer must retrieve this exact result, never calculate locally.
        require(api.preparedCut(h.workflow.activeSession().workingSourceGeometry,coords)===assessment,'Production cut preparation cache did not retain the actual response');
      }
      require(await h.workflow.finishDraft(),'Production draft did not finish');
      await settle(h);
      previewReady=h.workflow.previewReady();beforeCancel=observe(h);
      require(previewReady&&!!state.geometryPreview.session,'Production draft did not reach a READY geometry preview');
    }
    if(controlled){
      require(controlledStop,'Controlled lifecycle stop was not reached');
      const index=context.transport.findIndex((row,index)=>index>=controlledStop.transportEnd&&row.direction==='request'&&row.message?.type==='rebase');
      require(index>=0,'Next actual operation after stop did not send a real rebase');
      const event=context.transport[index];
      restartEvidence={transportIndex:index,worker:event.worker??null,message:clone(event.message)};
      const nextExecute=context.transport.slice(controlledStop.transportEnd).find(row=>row.direction==='request'&&row.message?.type==='execute');
      require(nextExecute?.message.operation===controlledStop.operation,'Unexpected first actual operation after controlled stop');
    }
    const controlledEvidence=controlled?{controlledStop,restartEvidence,nativeCase:null}:{};
    context.record('operation',{selection:beforeCancel,previewReady,cacheHit:cache,diagnostics:clone(h.errors),...clone(controlledEvidence),...(extraction?{cutExtraction:extraction,assessment:clone(assessment)}:{})});
    callbacks?.cancelPreparation();
    require(h.workflow.clear(),'Production workflow cancellation failed');
    await settle(h);
    if(controlled){
      require(client.stats().ready&&client.stats().workerActive,'READY cancellation unexpectedly stopped the restarted Worker');
      require(context.transport.slice(controlledStop.transportStart).filter(row=>row.direction==='terminate').length===1,'Controlled lifecycle stimulus retired more than one Worker');
    }
    context.record('selectionCancelled',{selection:observe(h),workerStopped:!client.stats().workerActive});
    return {kind:'production-selection',cancelled:true,beforeCancel,previewReady,cacheHit:cache,operations:context.requests.slice(requestOffset).map(row=>row.operation),errors:clone(h.errors),...controlledEvidence,...(extraction?{cutExtraction:extraction,assessment:clone(assessment)}:{})};
  }finally{
    if(originalGeometryOperations)h.ports.geometryOperations=originalGeometryOperations;
    if(h.workflow.activeSession())h.workflow.clear();
    if(pending)context.release(pendingOperation);
  }
}

export function sourceHistorySelectionRuntimeSource() {
  return `(()=>{${[extractSourceHistoryCutCallbacks,prepareSourceHistorySelection,runSourceHistorySelection].map(fn=>fn.toString()).join('\n')};return {extractSourceHistoryCutCallbacks,prepareSourceHistorySelection,runSourceHistorySelection};})()`;
}
