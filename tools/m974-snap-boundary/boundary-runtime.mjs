// Browser-safe fixture wiring. All geometry, eligibility and publication remain
// in the verified production modules, including the exact drag callbacks below.
export const boundaryEntrypoints = ['app-country-modes', 'app-object-commands',
  'selection-domain', 'selection-ui-controller', 'object-selection-controller',
  'editing-domain', 'territorial-interaction-policy', 'boundary-topology'];

export async function extractBoundaryCallbacks(source) {
  if (typeof source !== 'string' || !source) throw new Error('Missing boundary callback source');
  const markers = ['        beginBoundaryGesture: event => {', '        renderPacket: () => {'];
  const indexes = markers.map(marker => {
    const index = source.indexOf(marker);
    if (index < 0 || source.indexOf(marker, index + marker.length) >= 0) throw new Error('Boundary callback source seam must be unique, not ambiguous');
    return index;
  });
  if (indexes[1] <= indexes[0]) throw new Error('Boundary callback source seam order changed');
  const text = source.slice(indexes[0], indexes[1]);
  const names = ['beginBoundaryGesture', 'moveBoundaryGesture', 'commitBoundaryGesture'];
  if (names.some(name => text.split(`${name}:`).length !== 2)) throw new Error('Boundary callback properties must be unique');
  const bytes = text => new TextEncoder().encode(text);
  const hash = async text => [...new Uint8Array(await crypto.subtle.digest('SHA-256', bytes(text)))].map(value => value.toString(16).padStart(2, '0')).join('');
  return { sourcePath: 'assets/js/modules/app-domain-assembly.js', names, source: text,
    sourceSha256: await hash(source), sha256: await hash(text),
    byteStart: bytes(source.slice(0, indexes[0])).length,
    byteEnd: bytes(source.slice(0, indexes[1])).length };
}

export async function runBoundaryCase(loaded, definition) {
  const { api } = loaded;
  const clone = value => structuredClone(value);
  const noop = () => {};
  const runtime = loaded.runtime.createRuntime(api);
  const { state, entityStore, entityRepository, ports, domains, snapshots, geometryPreview } = runtime;
  entityStore.restoreProject(api.createStaticTerritorialSnapshot(clone(definition.features)));
  state.historyDirtyEntityIds.clear();
  // Fixture setup replaces the lifecycle fixture's unrelated references before
  // observation. Subsequent changes are performed only by production services.
  state.distributionEntries = api.normalizeDistributionEntries((definition.features.filter(feature => feature.properties.parentId)).map(feature => ({
    id: `entry-${feature.id}`, schemaVersion: 3, layerId: 'distribution', mode: 'territorial', territorialUnitId: feature.id, value: 1,
  })), { layerExists: id => id === 'distribution' });
  state.itemVisibility = ports.layerTree.normalizeLayerItemState({ subunits: Object.fromEntries(definition.features.filter(feature => feature.properties.parentId).map(feature => [feature.id, false])) });
  state.labelSettings = Object.fromEntries(definition.features.filter(feature => feature.properties.parentId).map(feature => [`territorial:${feature.id}`, {visible:false}]));
  state.layerPresentation = { schemaVersion: 4, styles: {}, objectStyles: Object.fromEntries(definition.features.filter(feature => feature.properties.parentId).map(feature => [`territorial:entity:${feature.id}`, {opacity:0.5}])), objectOrder: [] };
  let generation = 1;
  domains.projectDomain.getGeneration = () => generation;
  const diagnostics = [], visualEvents = [], workerTrace = [], modalDecisions = [];
  ports.feedback.setActionStatus = (...args) => diagnostics.push({kind:'status',args:clone(args)});
  ports.feedback.reportOperationError = (error, message, code) => {
    diagnostics.push({kind:'error',code,message:error.message});
    return message;
  };
  const selection = api.createSelectionDomain({ projectDomain:domains.projectDomain,
    onSelectionChanged: snapshot => {
      state.selected = snapshot.selection.items.find(ref => ref.key === snapshot.selection.primaryKey) || null;
    },
  });
  domains.selectionDomain = selection;
  domains.selectionUiController = api.createSelectionUiController({selectionDomain:selection,resolveRef:api.normalizeObjectRef});
  domains.editingDomain = api.createEditingDomain({ projectDomain:domains.projectDomain, selectionDomain:selection,
    toolController: {
      getGeometryPreviewSession: () => state.geometryPreview.session,
      discardGeometryPreview: geometryPreview.discardActiveGeometryPreview,
    },
    onEditingStateChanged: snapshot => { state.tool = snapshot.activeTool; },
  });
  const refs = definition.selectedRefs || definition.selectedIds.map(id => ({domain:'territorial',type:'entity',id}));
  selection.setMany(refs, {primary:refs.find(ref => ref.id === definition.seedId) || refs[0]});
  const held = [], heldWaiters = [];
  const holdOperations = new Set(['pending-cancel','stale-preparation'].includes(definition.scenario) ? ['boundary-prepare']
    : definition.scenario === 'stale-move' ? ['boundary-move']
      : ['stale-preview','pending-preview-cancel'].includes(definition.scenario) ? ['territorial-edit'] : []);
  const waitHeld = operation => {
    if (held.some(row => row.operation === operation)) return Promise.resolve();
    return new Promise((resolve,reject) => {
      const timeout = setTimeout(() => reject(new Error(`No actual ${operation} worker result arrived to delay`)), 20000);
      heldWaiters.push({operation,resolve:() => {clearTimeout(timeout);resolve();}});
    });
  };
  const release = operation => {
    holdOperations.delete(operation);
    for (let index = held.length - 1; index >= 0; index--) if (held[index].operation === operation) {
      const row = held.splice(index,1)[0];
      workerTrace.push({direction:'released-result',operation,ok:row.event.data.ok});
      row.adapter.onmessage?.(row.event);
    }
  };
  const createWorker = () => {
    const actual = loaded.createWorker(), operations = new Map();
    const adapter = {onmessage:null,onerror:null,
      postMessage(message) {
        if (message.type === 'execute') {
          operations.set(message.requestId,message.operation);
          workerTrace.push({direction:'request',operation:message.operation,requestId:message.requestId,payload:clone(message.payload)});
        }
        actual.postMessage(message);
      },
      terminate() { workerTrace.push({direction:'terminate'}); return actual.terminate(); },
    };
    actual.onmessage = event => {
      const operation = operations.get(event.data.requestId);
      if (event.data.type === 'result') {
        workerTrace.push({direction:'result',operation,requestId:event.data.requestId,ok:event.data.ok,
          ...(event.data.message ? {message:event.data.message}:{}),
          ...(event.data.result ? {result:clone(event.data.result)}:{})});
      }
      if (event.data.type === 'result' && holdOperations.has(operation)) {
        held.push({operation,event,adapter});
        workerTrace.push({direction:'held-result',operation,ok:event.data.ok});
        for (const waiter of heldWaiters.filter(row => row.operation === operation)) waiter.resolve();
      } else adapter.onmessage?.(event);
    };
    actual.onerror = error => adapter.onerror?.(error);
    return adapter;
  };
  const client = api.createMapEditWorkerClient({createWorker,getEntities:entityRepository.list,getFeatureById:entityRepository.get,getTargetRevision:() => state.stateRevision});
  ports.spatialQuery.mapEditClient = client;
  ports.geometryPreview = geometryPreview;
  const objectPresentation = api.createObjectPresentation();
  objectPresentation.connect({...ports,objectCatalog:api});
  objectPresentation.initializeObjectPresentationModel();
  ports.objectPresentation = objectPresentation;
  const objectCommands = api.createObjectCommands();
  objectCommands.connect({...ports,selectionServices:api});
  ports.objectOperationsA = objectCommands;
  const drafts = api.createTerritorialDrafts();
  let pendingModal = null;
  const applyWithDecision = async decision => {
    if (pendingModal) throw new Error('A prior impact decision is unresolved');
    let settled = false;
    const applying = geometryPreview.applyActiveGeometryPreview().finally(() => { settled = true; });
    const deadline = Date.now() + 20000;
    while (!pendingModal && !settled) { if (Date.now() > deadline) throw new Error('Impact decision or Apply did not settle'); await new Promise(resolve => setTimeout(resolve, 1)); }
    if (pendingModal) {
      const {modal,record} = pendingModal;pendingModal = null;
      record.stateBeforeDecision = loaded.runtime.observe(runtime);record.decision = decision;
      if (decision === 'cancel') modal.onCancel();else if (decision === 'confirm') modal.onConfirm();else throw new Error('Explicit fixture impact decision required');
    }
    return await applying;
  };
  // app-runtime-dependencies aliases this exact production metric under this port.
  drafts.connect({...ports,snapshots,geometryOperations:geometryPreview,applicationServicesB:{sphericalGeometryAreaKm2:api.geometryAreaKm2},
    projectRestore:{openConfirmModal:modal => {
      if (pendingModal) throw new Error('Concurrent impact modal is unsupported');
      const record = {title:modal.title,impacts:clone(modal.impacts || []),decision:'pending',stateWhenOpened:loaded.runtime.observe(runtime)};
      modalDecisions.push(record);pendingModal = {modal,record};
    }},countryValidation:{refreshCountryCentroids:noop},
  });
  const modes = api.createCountryModes();
  modes.connect({...ports,selectionServices:api,objectOperationsB:objectCommands,
    readinessUi:{clearNotification:noop},geometryOperations:geometryPreview,
    territorialEditingA:drafts,territorialEditingB:drafts,
  });
  ports.countryEditingB = modes;
  const extraction = await extractBoundaryCallbacks(loaded.sourceTexts['assets/js/modules/app-domain-assembly.js']);
  const dependencies = {...ports,objectOperationsB:objectCommands,territorialEditingB:drafts,
    gpuRenderingA:{beginActiveEditPreview:value => visualEvents.push({kind:'begin',value:clone(value)}),clearActiveEditPreview:reason => visualEvents.push({kind:'clear',reason})},
    gpuRenderingB:{updateActiveEditPreview:value => visualEvents.push({kind:'move',value:clone(value)})},
  };
  const callbacks = Function('dependencies','territorialEntityRepository','boundaryTouchesGeometry',`return ({${extraction.source}});`)(dependencies,entityRepository,api.boundaryTouchesGeometry);
  const preparation = () => {
    const current = state.boundaryPreparation;
    return current ? {status:current.status,current:current.current(),message:current.message || '',
      valid:current.result?.valid ?? false,selectedIds:clone(current.result?.selectedIds || []),
      isolatedIds:clone(current.result?.isolatedIds || []),handles:clone(current.result?.handles || []),segments:clone(current.result?.segments || [])} : null;
  };
  const stage = outcome => ({observed:true,state:loaded.runtime.observe(runtime),preparation:preparation(),
    selection:clone(selection.snapshot().selection),tool:state.tool,revision:state.stateRevision,outcome});
  const unobserved = reason => ({observed:false,reason});
  const stages = Object.fromEntries(['prepared','drag','preview','cancel','impactCancel','confirm','undo','redo','settled'].map(name => [name,unobserved('The production workflow has not reached this stage.')]));
  const output = {case:definition.id,input:clone(definition),entrypoint:'app-country-modes.enterTerritorialBorderEditFromSelection → app-geometry-preview.rebuildBoundaryTopology → map-edit-worker boundary-prepare → app-domain-assembly exact begin/move/commitBoundaryGesture → app-territorial-drafts.previewTerritorialEdit → app-geometry-preview apply/discard → app-project-snapshots history',
    extractionEvidence:Object.fromEntries(Object.entries(extraction).filter(([key]) => key !== 'source')),stages,workerTrace,diagnostics,visualEvents,modalDecisions,movedOwnerIds:[],
    observationLimits:{browserPointerProjection:false,reason:'The harness calls the exact production gesture callbacks with geographic coordinates. Pixel hit testing, pointer projection and GPU rendering are not observed.',draftUndoRedo:false,draftUndoRedoReason:'The observed web shared-boundary gesture opens a canonical preview on release; no separate multi-owner draft undo/redo API is exposed.'}};
  const finish = () => {
    output.finalPreparation = preparation();
    if (stages.confirm.observed) output.referenceEffects = loaded.runtime.referenceEffects(stages.before.state.document,stages.confirm.state.document);
    return output;
  };
  try {
    stages.before = stage(null);
    stages.cold = {...stage(null),worker:clone(client.stats()),analysis:geometryPreview.boundaryEditSelectionAnalysis(definition.selectedIds)};
    output.entry = {ok:definition.entryMode === 'auto-child' ? drafts.enterTerritorialUnitRedrawMode(definition.seedId)
      : modes.enterTerritorialBorderEditFromSelection()};
    output.entry.path = state.boundaryEditSeedEntityId ? (entityRepository.get(state.boundaryEditSeedEntityId)?.properties.parentId ? 'child':'root') : 'invalid';
    stages.pending = stage(output.entry);
    if (!output.entry.ok) {
      for (const key of ['prepared','drag','preview','cancel','confirm','undo','redo']) stages[key] = unobserved('The production entrypoint rejected this selection.');
      stages.settled = stage({ok:false});
      return finish();
    }
    const pending = state.boundaryPreparation.promise;
    if (['pending-cancel','stale-preparation'].includes(definition.scenario)) {
      await waitHeld('boundary-prepare');
      stages.delayed = stage({actualWorkerResultHeld:true});
      if (definition.scenario === 'pending-cancel') modes.cancelActiveMode(false); else generation += 1;
      release('boundary-prepare');
      const result = await pending;
      stages.settled = stage({ok:!!result});
      for (const key of ['drag','preview','confirm','undo','redo']) stages[key] = unobserved('A real worker result was rejected after preparation cancellation or project generation change.');
      return finish();
    }
    await pending;
    output.preparation = preparation();
    stages.prepared = stage({ok:state.boundaryPreparation?.status === 'ready'});
    if (state.boundaryPreparation?.status !== 'ready') {
      for (const key of ['drag','preview','cancel','confirm','undo','redo']) stages[key] = unobserved('Production boundary preparation rejected the selected geometry or hierarchy.');
      stages.settled = stage({ok:false});
      return finish();
    }
    const begin = () => callbacks.beginBoundaryGesture({vertexKey:definition.move.nodeKey,targetRef:{domain:'territorial',type:'entity',id:definition.seedId}});
    let session = begin();
    output.gesture = {ok:!!session};
    if (!session) {
      for (const key of ['drag','preview','cancel','confirm','undo','redo']) stages[key] = unobserved('The exact production beginBoundaryGesture callback rejected this handle.');
      stages.settled = stage({ok:false});
      return finish();
    }
    callbacks.moveBoundaryGesture(session,definition.move.coordinate);
    stages.drag = stage({ok:true,changed:session.changed,affectedIds:[...session.affectedIds],coordinate:clone(session.coordinate)});
    const committing = callbacks.commitBoundaryGesture(session);
    if (['stale-move','stale-preview','pending-preview-cancel'].includes(definition.scenario)) {
      const operation = definition.scenario === 'stale-move' ? 'boundary-move':'territorial-edit';
      await waitHeld(operation);
      stages.delayed = stage({actualWorkerResultHeld:true});
      if (definition.scenario === 'pending-preview-cancel') modes.cancelActiveMode(false);
      else if (definition.scenario === 'stale-preview') state.stateRevision += 1;
      else generation += 1;
      release(operation);
      stages.settled = stage({ok:await committing});
      for (const key of ['preview','confirm','undo','redo']) stages[key] = unobserved('A real worker reply was rejected after cancellation or a stale session/revision.');
      return finish();
    }
    const previewOk = await committing;
    output.movedOwnerIds = session.features ? [...session.features.keys()] : [];
    output.movedFeatures = session.features ? clone([...session.features.values()]) : [];
    stages.preview = stage({ok:previewOk});
    if (!state.geometryPreview.session) {
      stages.preview = unobserved(previewOk ? 'No canonical preview session exists.' : 'The production callback rejected a no-op, invalid move, or territorial plan.');
      for (const key of ['cancel','confirm','undo','redo']) stages[key] = unobserved('No canonical preview was created.');
      stages.settled = stage({ok:previewOk});
      return finish();
    }
    stages.cancel = stage({ok:geometryPreview.discardActiveGeometryPreview()});
    session = begin();
    if (!session) throw new Error('Production callback did not allow the same drag after preview cancellation');
    callbacks.moveBoundaryGesture(session,definition.move.coordinate);
    if (!await callbacks.commitBoundaryGesture(session)) throw new Error('Production callback did not rebuild the canceled preview');
    if (definition.impactCancel) {
      stages.impactCancel = stage({ok:await applyWithDecision('cancel')});
      session = begin();
      if (!session) throw new Error('Production gesture did not reopen after impact cancellation');
      callbacks.moveBoundaryGesture(session,definition.move.coordinate);
      if (!await callbacks.commitBoundaryGesture(session)) throw new Error('Production preview did not reopen after impact cancellation');
    }
    stages.confirm = stage({ok:await applyWithDecision('confirm')});
    if (stages.confirm.outcome.ok) {
      stages.undo = stage({ok:snapshots.historyService.undo()});
      stages.redo = stage({ok:snapshots.historyService.redo()});
    } else {
      stages.undo = unobserved('No successful canonical commit was available to undo.');
      stages.redo = unobserved('No successful canonical commit was available to redo.');
    }
    stages.settled = stage({ok:stages.confirm.outcome.ok});
    return finish();
  } finally {
    for (const waiter of heldWaiters) waiter.resolve();
    client.stop();
    domains.selectionUiController.dispose();
    domains.editingDomain.dispose();
  }
}

export function boundaryCases(api) {
  const clone = value => structuredClone(value);
  const polygon = ring => ({type:'Polygon',coordinates:[ring]});
  const rectangle = (x0,y0,x1,y1) => polygon([[x0,y0],[x1,y0],[x1,y1],[x0,y1],[x0,y0]]);
  const feature = (id,geometry,parentId = '',extra = {}) => api.createTerritorialFeature({id,name:id,geometry,parentId,entityKind:'general',coverageMode:parentId ? 'partition':'explicit',...extra});
  const triple = () => [feature('A',polygon([[0,0],[1,0],[1,1],[1,2],[0,2],[0,0]])),
    feature('B',rectangle(1,0,2,1)),feature('C',rectangle(1,1,2,2))];
  const asChildren = rows => [feature('P',rectangle(0,0,6,2)),...rows.map(row => feature(row.id,row.geometry,'P',{...row.properties,parentId:'P',coverageMode:'partition'}))];
  const positive = {entry:true,prepared:true,gesture:true,confirm:true};
  const base = (id,features = triple(),extra = {}) => ({id,features,selectedIds:['A','B','C'],seedId:'A',move:{nodeKey:'1,1',coordinate:[1,1.1]},expectedPath:'root',expected:{...positive,movedOwnerIds:['A','B','C']},...extra});
  const rows = [base('root-triple'),
    base('root-two-fixed',triple(),{selectedIds:['A','B'],expected:{entry:true,prepared:true,gesture:false,confirm:false}}),
    base('root-no-op',triple(),{move:{nodeKey:'1,1',coordinate:[1,1]},expected:{entry:true,prepared:true,gesture:true,confirm:false,movedOwnerIds:[]}}),
    base('root-coast-union-change',triple(),{move:{nodeKey:'1,0',coordinate:[0.9,-0.1]},expected:{entry:true,prepared:true,gesture:true,confirm:false}}),
    base('root-invalid-latitude',triple(),{move:{nodeKey:'1,1',coordinate:[1,91]},expected:{entry:true,prepared:true,gesture:true,confirm:false}}),
  ];
  const uneven = [feature('A',{type:'MultiPolygon',coordinates:[
    [rectangle(0,0,1,2).coordinates[0],rectangle(0.2,0.2,0.4,0.4).coordinates[0]],
    rectangle(-3,0,-2,1).coordinates,
  ]}),feature('B',polygon([[1,0],[2,0],[2,2],[1,2],[1,1],[1,0]]))];
  rows.push(base('root-uneven-multipolygon',uneven,{selectedIds:['A','B'],move:{nodeKey:'1,1',coordinate:[1.1,1]},expected:{...positive,movedOwnerIds:['A','B']}}));
  const onlyTwo = triple();
  onlyTwo[0].geometry.coordinates[0].splice(2,0,[1,0.5]);
  rows.push(base('root-selected-three-move-two',onlyTwo,{move:{nodeKey:'1,0.5',coordinate:[1.1,0.5]},expected:{...positive,movedOwnerIds:['A','B'],unchangedIds:['C']}}));
  const negativeHalf = triple();
  for (const row of negativeHalf) for (const point of row.geometry.coordinates[0]) point[0] = point[0] === 1 ? -0.00000005 : point[0] - 1;
  rows.push(base('root-negative-half-rounding',negativeHalf,{move:{nodeKey:'0,1',coordinate:[0.1,1.1]}}));
  const children = asChildren(triple());
  rows.push(base('child-triple',children,{expectedPath:'child',expected:{...positive,movedOwnerIds:['A','B','C'],unchangedIds:['P']}}));
  rows.push(base('child-parent-fixed',children,{expectedPath:'child',move:{nodeKey:'1,0',coordinate:[1.1,0]},expected:{entry:true,prepared:true,gesture:false,confirm:false}}));
  rows.push(base('child-two-fixed',children,{expectedPath:'child',selectedIds:['A','B'],expected:{entry:true,prepared:true,gesture:false,confirm:false}}));
  const pair = (a,b,offset) => [feature(a,rectangle(offset,0,offset+1,2)),feature(b,polygon([[offset+1,0],[offset+2,0],[offset+2,2],[offset+1,2],[offset+1,1],[offset+1,0]]))];
  const disjoint = [...pair('A','B',0),...pair('D','E',4)];
  rows.push(base('root-disjoint-pairs',disjoint,{selectedIds:['A','B','D','E'],move:{nodeKey:'1,1',coordinate:[1.1,1]},expected:{...positive,movedOwnerIds:['A','B'],unchangedIds:['D','E']}}));
  rows.push(base('child-disjoint-pairs',asChildren(disjoint),{expectedPath:'child',selectedIds:['A','B','D','E'],move:{nodeKey:'1,1',coordinate:[1.1,1]},expected:{...positive,movedOwnerIds:['A','B']}}));
  rows.push(base('child-auto-seed',asChildren(disjoint),{expectedPath:'child',entryMode:'auto-child',selectedIds:['A'],move:{nodeKey:'1,1',coordinate:[1.1,1]},expected:{...positive,movedOwnerIds:['A','B']}}));
  rows.push(base('root-isolated-owner',[...triple(),feature('D',rectangle(5,0,6,1))],{selectedIds:['A','B','C','D'],expected:{entry:true,prepared:false,confirm:false}}));
  const locked = triple(); locked[0].properties.locked = true;
  rows.push(base('root-selected-locked',locked,{expectedPath:'invalid',expected:{entry:false,confirm:false}}));
  const lockedParent = clone(children); lockedParent[0].properties.locked = true;
  rows.push(base('child-locked-parent',lockedParent,{expectedPath:'child',expected:{entry:true,prepared:false,confirm:false}}));
  const lockedAncestor = [...clone(children),feature('G',rectangle(0,0,6,2),'',{locked:true})];
  lockedAncestor[0].properties.parentId = 'G';
  rows.push(base('child-locked-ancestor',lockedAncestor,{expectedPath:'child',expected:{entry:true,prepared:false,confirm:false}}));
  rows.push(base('mixed-root-child',children,{selectedIds:['A','P'],expectedPath:'invalid',expected:{entry:false,confirm:false}}));
  const mixed = clone(children); mixed.push(feature('Q',rectangle(0,0,6,2))); mixed.find(row=>row.id==='B').properties.parentId = 'Q';
  rows.push(base('mixed-parents',mixed,{selectedIds:['A','B'],expectedPath:'invalid',expected:{entry:false,confirm:false}}));
  const regional = clone(children); regional[regional.findIndex(row=>row.id==='A')] = feature('A',regional.find(row=>row.id==='A').geometry,'',{entityKind:'regional'});
  rows.push(base('regional-selection',regional,{selectedIds:['A','B'],expectedPath:'invalid',expected:{entry:false,confirm:false}}));
  rows.push(base('missing-selected-owner',triple(),{selectedIds:['A','missing'],expectedPath:'invalid',expected:{entry:false,confirm:false}}));
  rows.push(base('mixed-domain-selection',triple(),{selectedIds:['A','B'],selectedRefs:[{domain:'territorial',type:'entity',id:'A'},{domain:'generic',type:'feature',id:'B'}],expectedPath:'invalid',expected:{entry:false,confirm:false}}));
  const descendants = () => [
    feature('whole',rectangle(1.04,1.04,1.1,1.1),'C'),
    feature('whole-deep',rectangle(1.05,1.05,1.08,1.08),'whole'),
    feature('cut',rectangle(1.3,1.2,1.5,1.4),'C'),
    feature('cut-deep',rectangle(1.32,1.22,1.38,1.38),'cut'),
    feature('removed',rectangle(1.2,1.05,1.4,1.1),'C'),
    feature('removed-deep',rectangle(1.25,1.06,1.35,1.09),'removed'),
  ];
  const hierarchyExpected = {...positive,movedOwnerIds:['A','B','C'],reparentedIds:['whole'],deletedIds:['removed','removed-deep'],clippedIds:['cut','cut-deep']};
  rows.push(base('root-descendant-effects',[...triple(),...descendants()],{move:{nodeKey:'1,1',coordinate:[1.3,1.3]},impactCancel:true,expected:hierarchyExpected}));
  rows.push(base('child-descendant-effects',[...children,...descendants()],{expectedPath:'child',move:{nodeKey:'1,1',coordinate:[1.3,1.3]},impactCancel:true,expected:hierarchyExpected}));
  rows.push(base('root-locked-touching',[...triple(),feature('touching',rectangle(1,0.8,1.2,1),'B',{locked:true})],{expected:{entry:true,prepared:true,gesture:false,confirm:false}}));
  rows.push(base('root-locked-unrelated',[...triple(),feature('unrelated',rectangle(1.7,1.7,1.9,1.9),'C',{locked:true})]));
  rows.push(base('root-locked-cut-child',[...triple(),feature('cut',rectangle(1.3,1.2,1.5,1.4),'C',{locked:true})],{move:{nodeKey:'1,1',coordinate:[1.3,1.3]},expected:{entry:true,prepared:true,gesture:true,confirm:false}}));
  for (const scenario of ['pending-cancel','stale-preparation','stale-move','stale-preview','pending-preview-cancel']) rows.push(base(`root-${scenario}`,triple(),{scenario,expected:{entry:true,confirm:false}}));
  return clone(rows);
}

export function boundaryRuntimeSource() {
  return `(()=>{${[extractBoundaryCallbacks, runBoundaryCase, boundaryCases].map(fn => fn.toString()).join('\n')};return {runBoundaryCase,boundaryCases,extractBoundaryCallbacks};})()`;
}
