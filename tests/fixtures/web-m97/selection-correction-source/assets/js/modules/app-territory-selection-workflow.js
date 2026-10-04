import { freezeEditingGeometry } from './editing-render-packet.js';
/** Shared territory selection for general objects, independent regions and annexation. */
export function createTerritorySelectionWorkflow() {
  let dependencies;
  let adapters;
  let previewTimer = null;
  let previewGeneration = 0;
  const text = value => String(value ?? '').trim();

  function connect(ports) {
    if (dependencies) throw new Error('territory-selection-workflow already connected');
    dependencies = ports;
  }

  function session() {
    return dependencies.projectState.state?.territorySelectionSession || null;
  }

  function createSession(kind, options = {}) {
    const definition = adapters?.get(kind);
    if (!definition) return null;
    const sourceCountryIds = [...new Set((options.sourceCountryIds || options.referenceCountryIds || []).map(text).filter(Boolean))];
    const current = {
      ...options,
      id: options.id || (0, dependencies.surfaces.uid)('territory-selection'),
      kind,
      entityKind: options.entityKind || 'general',
      tool: options.tool || definition.tool,
      taskLabel: definition.label,
      setupStageLabel: definition.setupStageLabel,
      setupCountryPicking: definition.setupCountryPicking,
      methodsRequiringSources: [...definition.methodsRequiringSources],
      unboundedMethods: [...definition.unboundedMethods],
      draftInstructions: definition.draftInstructions,
      componentLabel: definition.componentLabel,
      riverComponentLabel: definition.riverComponentLabel,
      showRiverFailureSources: definition.showRiverFailureSources,
      targetHighlightRole: definition.targetHighlightRole,
      sourceHighlightRole: definition.sourceHighlightRole,
      actionButtonId: definition.actionButtonId,
      stage: 'setup',
      activePhase: null,
      activeMethod: null,
      requestedMethod: null,
      methodChangeConfirmation: null,
      name: String(options.name ?? definition.defaultName),
      generatedId: options.generatedId || (definition.generatedIdPrefix ? (0, dependencies.surfaces.uid)(definition.generatedIdPrefix) : ''),
      targetCountryId: text(options.targetCountryId),
      parentId: text(options.parentId),
      sourceKey: text(options.sourceKey || definition.defaultSourceKey),
      componentIndex: null,
      computationPending: false,
      computationEpoch: 0,
      computationTimer: null,
      preparation: null,
      candidates: [],
      selectedCandidateIds: [],
      selectedComponentKeys: [],
      componentFeatures: [],
      hoveredComponentKey: null,
      parts: [],
      componentSnapshots: [],
      baseSourceFeatures: [],
      currentGeometry: null,
      combinedGeometry: null,
      baseSourceGeometry: null,
      workingSourceGeometry: null,
      remainingGeometry: null,
      sourceInfo: null,
      setupSourceCache: null,
      useRiverBoundaries: false,
      riverPartitionStatus: 'idle',
      riverPartitionCandidates: [],
      riverPartitionDonorResults: [],
      previewPending: false,
      previewReadyKey: null,
      settingsRevision: 0,
      selectionRevision: 0,
      sourceRevision: 0,
      projectGeneration: dependencies.domains.projectDomain?.getGeneration?.() ?? 0,
      applying: false,
      sourceCountryIds,
    };
    configureCreationPolicy(current);
    return current;
  }

  function configureCreationPolicy(current) {
    if (current.kind !== 'entity') return;
    const regional = current.entityKind === 'regional';
    current.setupCountryPicking = !regional && !current.parentId;
    current.methodsRequiringSources = regional ? ['line', 'components'] : [];
    current.unboundedMethods = regional ? ['polygon'] : [];
    current.sourceHighlightRole = regional ? 'reference' : 'selected-provider';
  }

  function initializeTerritorySelectionWorkflow() {
    adapters = Object.freeze(new Map([
      ['annex', Object.freeze({
        tool: 'annex-territory',
        label: '영토 편입',
        setupStageLabel: '가져올 국가',
        defaultName: '',
        referenceLabel: '',
        generatedIdPrefix: '',
        supportsName: false,
        showSetup: false,
        setupCountryPicking: true,
        methodsRequiringSources: [],
        unboundedMethods: [],
        draftInstructions: Object.freeze({
          line: '가져올 영토를 가로질러 선을 그리세요.',
          polygon: '가져올 영역을 지도에 그리세요.',
          components: '가져올 영토 조각을 선택하세요.',
        }),
        componentLabel: '가져올 영토 조각',
        riverComponentLabel: '하천으로 나뉜 영토 조각',
        showRiverFailureSources: true,
        targetHighlightRole: 'annex-target',
        sourceHighlightRole: 'selected-provider',
        actionButtonId: 'annexEntityBtn',
        defaultSourceKey: '',
        showReference: () => false,
        finalLabel: count => `편입 (${count})`,
        sourceInstruction: '영토를 가져올 국가를 선택할 수 없습니다. 국가 영토 안쪽을 선택하세요.',
        canUseSource: (current, id) => id !== current.targetCountryId,
        sourceRejectedMessage: '편입 주체와 다른 국가를 선택하세요.',
        validateSetup: dependencies.countryEditingC.validateAnnexSelectionSetup,
        prepareSelection: dependencies.countryEditingB.prepareAnnexSelection,
        finishDraft: dependencies.countryCommitFlow.finishAnnexSelectionDraft,
        preview: dependencies.countryCommitFlow.prepareAnnexSelectionPreview,
      })],
      ['entity', Object.freeze({
        tool: 'draw-territorial-unit', label: '객체 추가', setupStageLabel: '객체 정보',
        defaultName: '새 객체', referenceLabel: '기준 객체', generatedIdPrefix: 'entity',
        supportsName: true, showSetup: true, setupCountryPicking: false,
        methodsRequiringSources: [], unboundedMethods: [],
        draftInstructions: Object.freeze({ line: '기준 영역을 가로질러 선을 그리세요.', polygon: '추가할 영역을 지도에 그리세요.', components: '새 객체로 만들 조각을 선택하세요.' }),
        componentLabel: '새 객체로 만들 조각', riverComponentLabel: '하천으로 나뉜 조각',
        showRiverFailureSources: false, targetHighlightRole: '', sourceHighlightRole: 'reference',
        actionButtonId: '', defaultSourceKey: '',
        showReference: current => !current.parentId && (current.setupCountryPicking || current.activePhase === 'source'),
        finalLabel: () => '생성', sourceInstruction: '기준으로 사용할 객체 안쪽을 선택하세요.',
        canUseSource: current => !current.parentId,
        validateSetup: current => current.entityKind === 'general' && !current.parentId
          ? dependencies.countryEditingC.validateNewCountrySelectionSetup(current)
          : dependencies.territorialEditingB.territorialCreateSetupValid(current),
        prepareSelection: current => current.entityKind === 'general' && !current.parentId
          ? dependencies.countryEditingB.prepareNewCountrySelection(current)
          : dependencies.territorialEditingA.prepareTerritorialCreateSelection(current),
        finishDraft: current => current.entityKind === 'general' && !current.parentId
          ? dependencies.countryCommitFlow.finishNewCountrySelectionDraft(current)
          : dependencies.territorialEditingA.finishTerritorialUnitDirectDraft(),
        preview: (current, key) => current.entityKind === 'general' && !current.parentId
          ? dependencies.countryCommitFlow.prepareNewCountrySelectionPreview(current, key)
          : dependencies.territorialEditingA.prepareTerritorialSelectionPreview(current, key),
      })],
    ]));
    cancelSelectionComputation(session());
    dependencies.projectState.state.territorySelectionSession = null;
  }

  function adapterFor(current = session()) {
    return current ? adapters?.get(current.kind) || null : null;
  }

  function refresh(reason) {
    const current = session();
    if (current) dependencies.domains.editingDomain?.refreshTerritorySelection?.({ tool: current.tool, reason });
    dependencies.domains.renderingDomain?.invalidateEditingOverlays?.(reason);
    const presentationOnly = reason.startsWith('territory-selection-component')
      || ['territory-selection-calculated', 'territory-selection-preview-pending', 'territory-selection-preview-ready'].includes(reason);
    if (!presentationOnly) {
      dependencies.domains.renderingDomain?.invalidateCountryPatch?.(reason);
    }
    (0, dependencies.taskUi.updateModeButtons)();
  }

  function start(kind, options = {}) {
    if (!adapters?.has(kind)) return false;
    clear({ discardPreview: true, refreshUi: false });
    const current = createSession(kind, options);
    dependencies.projectState.state.territorySelectionSession = current;
    dependencies.domains.editingDomain?.setTool(current.tool, { announce: false });
    dependencies.projectState.state.territorySelectionSession = current;
    (0, dependencies.taskUi.setModeBanner)('');
    refresh('territory-selection-start');
    return current;
  }

  function cancelPreview({ discard = true, preserveReady = false } = {}) {
    previewGeneration += 1;
    if (previewTimer !== null) {
      clearTimeout(previewTimer);
      previewTimer = null;
    }
    const current = session();
    if (current) {
      current.previewPending = false;
      if (!preserveReady) current.previewReadyKey = null;
    }
    if (discard) (0, dependencies.geometryOperations.discardActiveGeometryPreview)({ announce: false });
    (0, dependencies.taskUi.updateModeButtons)();
  }

  function clear({ discardPreview = true, refreshUi = true } = {}) {
    const current = session();
    cancelSelectionComputation(current);
    cancelPreview({ discard: discardPreview });
    dependencies.domains.editingDomain?.clearDraft?.({ reason: 'territory-selection-clear', render: false });
    dependencies.projectState.state.territorySelectionSession = null;
    (0, dependencies.riverCandidates.resetRiverPartitionState)();
    if (refreshUi) refresh('territory-selection-clear');
    return !!current;
  }

  function previewKey(current = session()) {
    if (!current) return '';
    return [current.id, current.projectGeneration, current.settingsRevision, current.sourceRevision, current.selectionRevision].join(':');
  }

  function previewIsCurrent(current, key) {
    return current === session()
      && current.projectGeneration === (dependencies.domains.projectDomain?.getGeneration?.() ?? current.projectGeneration)
      && key === previewKey(current);
  }

  function touchSelection(current = session(), { discard = true } = {}) {
    if (!current) return;
    cancelPreview({ discard });
    current.computationEpoch += 1;
    clearTimeout(current.computationTimer);
    current.computationTimer = null;
    current.computationPending = false;
    current.selectionRevision += 1;
    current.previewReadyKey = null;
  }

  function clearCurrentSelection(current = session(), { clearDraft = true, refreshUi = false } = {}) {
    if (!current) return false;
    touchSelection(current);
    if (clearDraft) dependencies.domains.editingDomain?.clearDraft?.({ reason: 'territory-selection-current-clear', render: false });
    current.activePhase = null;
    current.activeMethod = null;
    current.requestedMethod = null;
    current.methodChangeConfirmation = null;
    cancelSelectionComputation(current);
    current.candidates = [];
    current.selectedCandidateIds = [];
    current.selectedComponentKeys = [];
    current.hoveredComponentKey = null;
    current.currentGeometry = null;
    current.useRiverBoundaries = false;
    current.riverPartitionStatus = 'idle';
    current.riverPartitionCandidates = [];
    current.riverPartitionDonorResults = [];
    (0, dependencies.riverCandidates.resetRiverPartitionState)();
    refreshCombinedGeometry(current);
    if (refreshUi) refresh('territory-selection-current-clear');
    return true;
  }

  function resetSelection(current = session(), { keepRequestedMethod = true, refreshUi = true } = {}) {
    if (!current) return false;
    configureCreationPolicy(current);
    const requestedMethod = current.requestedMethod;
    clearCurrentSelection(current, { refreshUi: false });
    current.requestedMethod = keepRequestedMethod ? requestedMethod : null;
    current.parts = [];
    current.componentIndex = null;
    current.preparation = null;
    current.componentSnapshots = [];
    current.baseSourceFeatures = [];
    current.combinedGeometry = null;
    current.baseSourceGeometry = null;
    current.workingSourceGeometry = null;
    current.remainingGeometry = null;
    current.sourceInfo = null;
    if (refreshUi) refresh('territory-selection-reset');
    return true;
  }

  function setupValid(current = session()) {
    return !!current && adapterFor(current)?.validateSetup?.(current) === true;
  }

  function draftCoordinates() {
    return (0, dependencies.countryEditingA.editingDraftCoordinates)() || [];
  }

  function activeCurrentWork(current = session()) {
    return !!current && (!!current.currentGeometry || current.candidates.length > 0
      || current.selectedComponentKeys.length > 0 || draftCoordinates().length > 0
      || dependencies.domains.editingDomain?.draftInputActive?.());
  }

  function currentSelectionSnapshot(current) {
    return {
      activePhase: current.activePhase,
      activeMethod: current.activeMethod,
      requestedMethod: current.requestedMethod,
      methodChangeConfirmation: current.methodChangeConfirmation,
      componentIndex: current.componentIndex,
      candidates: current.candidates,
      selectedCandidateIds: [...current.selectedCandidateIds],
      selectedComponentKeys: [...current.selectedComponentKeys],
      componentFeatures: current.componentFeatures,
      hoveredComponentKey: current.hoveredComponentKey,
      currentGeometry: current.currentGeometry,
      useRiverBoundaries: current.useRiverBoundaries,
      riverPartitionStatus: current.riverPartitionStatus,
      riverPartitionCandidates: current.riverPartitionCandidates,
      riverPartitionDonorResults: current.riverPartitionDonorResults,
      draftCoords: structuredClone(draftCoordinates()),
    };
  }

  function restoreCurrentSelection(current, snapshot) {
    Object.assign(current, {
      activePhase: snapshot.activePhase,
      activeMethod: snapshot.activeMethod,
      requestedMethod: snapshot.requestedMethod,
      methodChangeConfirmation: snapshot.methodChangeConfirmation,
      componentIndex: snapshot.componentIndex,
      candidates: snapshot.candidates,
      selectedCandidateIds: [...snapshot.selectedCandidateIds],
      selectedComponentKeys: snapshot.selectedComponentKeys,
      componentFeatures: snapshot.componentFeatures,
      hoveredComponentKey: snapshot.hoveredComponentKey,
      currentGeometry: snapshot.currentGeometry,
      useRiverBoundaries: snapshot.useRiverBoundaries,
      riverPartitionStatus: snapshot.riverPartitionStatus,
      riverPartitionCandidates: snapshot.riverPartitionCandidates,
      riverPartitionDonorResults: snapshot.riverPartitionDonorResults,
    });
    dependencies.domains.editingDomain?.replaceDraftCoordinates?.(snapshot.draftCoords, { record: false, inputPhase: 'refine' });
    refreshCombinedGeometry(current);
  }

  function componentPreparationKey(current) {
    return [current.id, current.projectGeneration, current.sourceRevision,
      current.parentId, current.sourceKey, ...current.sourceCountryIds,
      ...current.parts.map(part => part.id)].join(':');
  }

  function cancelSelectionComputation(current) {
    if (!current) return;
    current.computationEpoch += 1;
    clearTimeout(current.computationTimer);
    current.computationTimer = null;
    current.operationAbort?.abort();
    current.operationAbort = null;
    if (current.workerRequests > 0) dependencies.spatialQuery.mapEditClient.stop();
    current.computationPending = false;
    current.preparation = null;
  }

  async function executeSelectionOperation(current, operation, payload) {
    current.workerRequests = (current.workerRequests || 0) + 1;
    current.operationAbort ||= new AbortController();
    try {
      const response = await dependencies.spatialQuery.mapEditClient.execute(operation, { payload }, {
        jobKey: `${current.id}:${operation}`,
        signal: current.operationAbort.signal,
      });
      return response.result;
    } finally { current.workerRequests -= 1; }
  }

  function prepareComponentSource(current) {
    const key = componentPreparationKey(current);
    if (current.componentIndex?.key === key) return Promise.resolve(true);
    if (current.preparation?.key === key) return current.preparation.promise;
    const preparation = { key, promise: null };
    current.preparation = preparation;
    preparation.promise = executeSelectionOperation(current, 'territory-components', {
      features: current.baseSourceFeatures, baseGeometry: current.baseSourceGeometry,
      parts: current.parts.map(part => part.geometry),
    }).then(result => {
      if (current !== session() || current.preparation !== preparation
        || key !== componentPreparationKey(current)
        || current.projectGeneration !== (dependencies.domains.projectDomain?.getGeneration?.() ?? current.projectGeneration)) return false;
      dependencies.territoryComponents.installComponentIndex(current, result, key);
      current.componentFeatures = result.componentFeatures;
      current.baseSourceGeometry = result.baseSourceGeometry;
      current.workingSourceGeometry = result.workingSourceGeometry;
      current.remainingGeometry = result.workingSourceGeometry;
      current.archivedGeometry = result.archivedGeometry;
      return true;
    }).finally(() => {
      if (current.preparation === preparation) current.preparation = null;
    });
    return preparation.promise;
  }

  function sourceFeaturesFromPreparedSession(current) {
    if (current.baseSourceFeatures.length) return;
    const supplied = Array.isArray(current.componentFeatures) ? current.componentFeatures.filter(feature => feature?.geometry) : [];
    if (supplied.length) current.baseSourceFeatures = supplied;
    else if (current.baseSourceGeometry) current.baseSourceFeatures = [{
      type: 'Feature',
      id: String(current.sourceInfo?.source?.id || current.sourceInfo?.feature?.id || current.sourceCountryIds[0] || 'territory-source'),
      properties: {}, geometry: current.baseSourceGeometry,
    }];
  }

  async function activateMethod(current, method) {
    if (!current || !['line', 'polygon', 'components'].includes(method)) return false;
    const previous = currentSelectionSnapshot(current);
    clearCurrentSelection(current, { refreshUi: false });
    current.activeMethod = method;
    current.activePhase = 'preparing';
    clearTimeout(current.computationTimer);
    current.computationTimer = null;
    current.computationPending = false;
    dependencies.taskUi.setModeBanner('영토 조각을 준비하는 중입니다.');
    refresh('territory-selection-components-preparing');
    const methodEpoch = current.computationEpoch;
    let prepared = false;
    try { prepared = await adapterFor(current)?.prepareSelection?.(current); }
    catch (error) { dependencies.feedback.reportOperationError(error, '기준 영역을 준비하지 못했습니다.', 'PL-TERRITORY-SOURCE', 3800); }
    if (current !== session() || methodEpoch !== current.computationEpoch) return false;
    if (!prepared) {
      restoreCurrentSelection(current, previous);
      refresh('territory-selection-prepare-failed');
      return false;
    }
    sourceFeaturesFromPreparedSession(current);
    const activationEpoch = current.computationEpoch;
    let sourceReady = false;
    try { sourceReady = await prepareComponentSource(current); }
    catch (error) {
      if (current === session() && activationEpoch === current.computationEpoch) dependencies.feedback.reportOperationError(error, '영토 조각을 준비하지 못했습니다. 방식을 다시 선택해 재시도하세요.', 'PL-TERRITORY-COMPONENTS', 3800);
    }
    if (current !== session() || activationEpoch !== current.computationEpoch) return false;
    if (!sourceReady) {
      restoreCurrentSelection(current, previous);
      refresh('territory-selection-source-rebuild-failed');
      return false;
    }
    current.activePhase = method === 'components' ? 'components' : 'drawing';
    if (current.activePhase === 'drawing') {
      dependencies.domains.editingDomain?.startDraft?.({ coords: [] });
      (0, dependencies.taskUi.setModeBanner)((0, dependencies.interactionPresentation.defaultDraftInstruction)());
    } else {
      (0, dependencies.territoryComponentUi.updateTerritoryComponentSelectionFeedback)();
      dependencies.domains.editingDomain?.refreshTerritorySelection?.({
        tool: current.tool,
        reason: 'territory-selection-components-ready',
      });
    }
    refresh('territory-selection-ready');
    return true;
  }

  async function advance() {
    const current = session();
    if (!current) return false;
    if (current.stage === 'selection' && current.computationError) {
      refreshCombinedGeometry(current);
      refresh('territory-selection-components-retry');
      return true;
    }
    if (current.stage === 'setup') {
      if (!setupValid(current)) return false;
      current.name = current.name.trim();
      current.stage = 'selection';
      (0, dependencies.taskUi.setModeBanner)('');
      if (current.activePhase === 'preparing') {
        const method = current.activeMethod;
        current.activeMethod = null;
        return activateMethod(current, method);
      }
      if (!current.combinedGeometry && (current.selectedComponentKeys.length || current.currentGeometry || current.parts.length)) refreshCombinedGeometry(current);
      if (!dependencies.projectState.state.geometryPreview.session && selectionGeometryReady(current)) schedulePreview();
      refresh('territory-selection-open');
      return true;
    }
    if (current.stage !== 'selection' || !previewReady(current)
      || current.activePhase === 'candidate' && !!current.currentGeometry) return false;
    current.stage = 'review';
    (0, dependencies.taskUi.setModeBanner)('');
    refresh('territory-selection-review');
    return true;
  }

  function back() {
    const current = session();
    if (!current) return false;
    if (current.methodChangeConfirmation) {
      current.methodChangeConfirmation = null;
      current.requestedMethod = current.activeMethod || current.requestedMethod;
      refresh('territory-selection-method-change-kept');
      return true;
    }
    if (current.stage === 'review') {
      current.stage = 'selection';
      (0, dependencies.taskUi.setModeBanner)('');
      refresh('territory-selection-back-selection');
      return true;
    }
    if (current.stage === 'selection') {
      cancelSelectionComputation(current);
      cancelPreview({ discard: false, preserveReady: true });
      current.stage = 'setup';
      dependencies.domains.editingDomain.cancelActiveGesture('territory-selection-back-setup');
      dependencies.domains.editingDomain.clearDraftHover('territory-selection-back-setup');
      (0, dependencies.taskUi.setModeBanner)('');
      refresh('territory-selection-back-setup');
      return true;
    }
    return false;
  }

  async function selectMethod(method) {
    const current = session();
    if (!current || current.stage !== 'selection' || !['line', 'polygon', 'components'].includes(method)) return false;
    if (current.activeMethod === method && current.activePhase !== 'source') {
      if (current.computationError) { refreshCombinedGeometry(current); refresh('territory-selection-components-retry'); }
      return true;
    }
    current.requestedMethod = method;
    if (current.activePhase === 'components'
      && current.selectedComponentKeys.length && current.activeMethod !== method) {
      if (canAddPart(current)) return archiveComponentsAndActivate(current, method);
      if (current.computationPending || current.previewPending || current.currentGeometry) {
        if (current.currentGeometry && !current.previewPending) schedulePreview();
        refresh('territory-selection-method-awaiting-preview');
        return false;
      }
      current.requestedMethod = current.activeMethod;
      return false;
    }
    if (current.activeMethod && current.activeMethod !== method && activeCurrentWork(current)) {
      current.methodChangeConfirmation = { type: 'method', method };
      refresh('territory-selection-method-change-confirm');
      return false;
    }
    if (current.methodsRequiringSources.includes(method) && !current.sourceCountryIds.length) {
      current.activePhase = 'source';
      refresh('territory-selection-source-required');
      return false;
    }
    return activateMethod(current, method);
  }

  async function archiveComponentsAndActivate(current, method) {
    if (current !== session() || current.stage !== 'selection' || !canAddPart(current)) return false;
    if (!addPart()) return false;
    return activateMethod(current, method);
  }

  async function confirmMethodChange() {
    const current = session();
    const confirmation = current?.methodChangeConfirmation;
    if (!current || !confirmation) return false;
    if (confirmation.type === 'settings') {
      current.methodChangeConfirmation = null;
      return applySourceCountryToggle(current, confirmation.countryId, { reset: true });
    }
    const requested = confirmation.method;
    if (current.stage !== 'selection' || !requested) return false;
    current.methodChangeConfirmation = null;
    const started = await activateMethod(current, requested);
    if (!started && current === session()) {
      current.requestedMethod = current.activeMethod || requested;
    }
    return started;
  }

  function cancelMethodChange() {
    const current = session();
    if (!current?.methodChangeConfirmation) return false;
    const wasMethodChange = current.methodChangeConfirmation.type === 'method';
    current.methodChangeConfirmation = null;
    if (wasMethodChange) {
      current.requestedMethod = current.activeMethod || current.requestedMethod;
    }
    refresh('territory-selection-method-change-kept');
    return true;
  }

  async function startReferenceMethod() {
    const current = session();
    if (!current || current.stage !== 'selection' || current.activePhase !== 'source' || !current.requestedMethod) return false;
    if (!current.sourceCountryIds.length) return false;
    return activateMethod(current, current.requestedMethod);
  }

  function updateName(value) {
    const current = session();
    if (!current || !adapterFor(current)?.supportsName) return false;
    const next = String(value ?? '');
    if (current.name === next) return true;
    current.name = next;
    current.settingsRevision += 1;
    current.previewReadyKey = null;
    if (current.computationPending) refreshCombinedGeometry(current);
    if (current.stage === 'selection' && selectionGeometryReady(current)) schedulePreview();
    else refresh('territory-selection-name');
    return true;
  }

  function countryPickingActive(current = session()) {
    return !!current && ((current.stage === 'setup' && current.setupCountryPicking)
      || (current.stage === 'selection' && current.activePhase === 'source'
        && current.methodsRequiringSources.includes(current.requestedMethod)));
  }

  function toggleSourceCountry(countryId) {
    const current = session();
    const id = text(countryId);
    if (!countryPickingActive(current) || !id || !dependencies.territorialModel.entityRepository.get(id)) return false;
    const adapter = adapterFor(current);
    if (adapter?.canUseSource?.(current, id) !== true) {
      if (adapter?.sourceRejectedMessage) (0, dependencies.feedback.setActionStatus)(adapter.sourceRejectedMessage, 'error', 3000);
      return false;
    }
    const firstRegionReference = current.kind === 'entity' && current.entityKind === 'regional' && current.stage === 'selection'
      && current.activePhase === 'source' && current.sourceCountryIds.length === 0;
    if ((current.parts.length || activeCurrentWork(current)) && current.stage !== 'setup' && !firstRegionReference) {
      current.methodChangeConfirmation = { type: 'settings', countryId: id };
      refresh('territory-selection-settings-change-confirm');
      return true;
    }
    return applySourceCountryToggle(current, id, { reset: !!current.activeMethod });
  }

  function applySourceCountryToggle(current, id, { reset = false } = {}) {
    const adapter = adapterFor(current);
    if (!current || !id || adapter?.canUseSource?.(current, id) !== true) return false;
    const selected = new Set(current.sourceCountryIds.map(text));
    if (selected.has(id)) selected.delete(id); else selected.add(id);
    current.sourceCountryIds = [...selected];
    current.settingsRevision += 1;
    if (reset) {
      const requestedMethod = current.requestedMethod;
      resetSelection(current, { keepRequestedMethod: true, refreshUi: false });
      current.requestedMethod = requestedMethod;
      current.stage = 'selection';
    }
    refresh('territory-selection-source-country');
    return true;
  }

  function sourceCountryInstruction(current = session()) {
    return current ? adapterFor(current)?.sourceInstruction || '' : '';
  }

  function finishDraft() {
    const current = session();
    if (!current || current.stage !== 'selection' || current.activePhase !== 'drawing'
      || !['line', 'polygon'].includes(current.activeMethod)) return false;
    if (!dependencies.countryEditingA.editingDraftCoordinates().length && current.parts.length) {
      current.currentGeometry = null;
      current.candidates = [];
      current.selectedCandidateIds = [];
      current.activePhase = 'result';
      dependencies.domains.editingDomain?.clearDraft?.({ reason: 'territory-selection-parts-review', render: false });
      refreshCombinedGeometry(current);
      if (!previewReady(current)) schedulePreview();
      refresh('territory-selection-parts-review');
      return true;
    }
    return adapterFor(current)?.finishDraft?.(current) || false;
  }

  function refreshCombinedGeometry(current = session()) {
    if (!current) return null;
    const epoch = ++current.computationEpoch;
    clearTimeout(current.computationTimer);
    current.computationTimer = null;
    current.combinedGeometry = null;
    current.computationError = false;
    if (!current.parts.length && !current.currentGeometry && !current.selectedComponentKeys.length && !current.selectedCandidateIds.length) {
      current.computationPending = false;
      current.archivedGeometry = null;
      return null;
    }
    const key = previewKey(current);
    const phase = current.activePhase;
    current.computationPending = true;
    current.computationError = false;
    dependencies.taskUi.setModeBanner('선택 영역을 계산하는 중입니다.');
    const isCurrent = () => previewIsCurrent(current, key) && epoch === current.computationEpoch && phase === current.activePhase;
    current.computationTimer = setTimeout(async () => {
      current.computationTimer = null;
      try {
        if (!isCurrent()) return;
        const prepared = await prepareComponentSource(current);
        if (!isCurrent()) return;
        if (!prepared) throw new Error('기준 영역이 변경되었습니다. 다시 시도하세요.');
        const selected = phase === 'components'
          ? dependencies.territoryComponents.territoryComponentItems().filter(item => item.selected).map(item => item.geometry)
          : phase === 'candidate' ? current.candidates.filter(item => current.selectedCandidateIds.includes(item.id)).map(item => item.geometry) : [];
        const result = await executeSelectionOperation(current, 'territory-selection', {
          components: phase === 'components', candidates: phase === 'candidate', selected, currentGeometry: current.currentGeometry,
          archivedGeometry: current.archivedGeometry, workingSourceGeometry: current.workingSourceGeometry,
        });
        if (!isCurrent()) return;
        for (const value of Object.values(result)) freezeEditingGeometry(value);
        Object.assign(current, result);
        current.computationPending = false;
        dependencies.territoryComponentUi.updateTerritoryComponentSelectionFeedback();
        schedulePreview();
      } catch (error) {
        if (!isCurrent()) return;
        current.computationPending = false;
        current.computationError = true;
        current.combinedGeometry = null;
        dependencies.feedback.reportOperationError(error, '선택 영역을 계산하지 못했습니다. 다시 계산을 눌러 재시도하세요.', 'PL-TERRITORY-SELECTION-001', 3800);
      } finally {
        if (isCurrent()) refresh('territory-selection-calculated');
      }
    }, 0);
    return null;
  }

  function setCurrentCandidates(candidates) {
    const current = session();
    if (!current || current.stage !== 'selection') return false;
    touchSelection(current);
    const calculationId = dependencies.surfaces.uid('territory-candidates');
    current.candidates = candidates.map((item, index) => ({ ...item, id: `${calculationId}:${index}` }));
    const smallest = current.candidates.reduce((best, item) => !best || item.area < best.area ? item : best, null);
    current.selectedCandidateIds = smallest ? [smallest.id] : [];
    current.currentGeometry = smallest?.geometry || null;
    current.activePhase = 'candidate';
    refreshCombinedGeometry(current);
    dependencies.domains.editingDomain?.clearDraft?.({ reason: 'territory-selection-candidate-ready', render: false });
    refresh('territory-selection-candidates');
    return !!smallest;
  }

  function selectCandidate(candidateId) {
    const current = session();
    if (!current || current.stage !== 'selection' || current.activePhase !== 'candidate'
      || !current.candidates.some(item => item.id === candidateId)) return false;
    touchSelection(current);
    const selected = new Set(current.selectedCandidateIds);
    if (selected.has(candidateId)) selected.delete(candidateId); else selected.add(candidateId);
    current.selectedCandidateIds = [...selected];
    current.currentGeometry = null;
    refreshCombinedGeometry(current);
    dependencies.taskUi.setModeBanner(selected.size ? '가져올 조각을 클릭하여 선택하거나 해제하세요.' : '가져올 조각을 하나 이상 선택하세요.');
    refresh('territory-selection-candidate');
    return true;
  }

  function toggleComponent(componentKey) {
    const current = session();
    if (!current || current.stage !== 'selection' || current.activePhase !== 'components') return false;
    if (current.useRiverBoundaries) dependencies.territoryComponents.territoryComponentItems();
    const available = current.useRiverBoundaries ? current.componentIndex?.river?.byKey : current.componentIndex?.byKey;
    if (!available?.has(componentKey)) return false;
    touchSelection(current);
    const selected = new Set(current.selectedComponentKeys);
    if (selected.has(componentKey)) selected.delete(componentKey); else selected.add(componentKey);
    current.selectedComponentKeys = [...selected];
    current.currentGeometry = null;
    refreshCombinedGeometry(current);
    (0, dependencies.territoryComponentUi.updateTerritoryComponentSelectionFeedback)();
    if (current.currentGeometry) schedulePreview();
    refresh('territory-selection-component');
    return true;
  }

  function toggleRiverBoundaries(enabled) {
    const current = session();
    if (!current || current.stage !== 'selection' || current.activePhase !== 'components') return false;
    const next = enabled === true;
    if (current.useRiverBoundaries === next) return true;
    touchSelection(current);
    current.useRiverBoundaries = next;
    current.selectedComponentKeys = [];
    current.currentGeometry = null;
    current.hoveredComponentKey = null;
    refreshCombinedGeometry(current);
    (0, dependencies.riverCandidates.resetRiverPartitionState)();
    if (next) void (0, dependencies.riverCandidates.prepareRiverPartitionCandidates)();
    else (0, dependencies.territoryComponentUi.updateTerritoryComponentSelectionFeedback)();
    dependencies.domains.editingDomain?.refreshTerritorySelection?.({
      tool: current.tool,
      reason: 'territory-selection-river-toggle',
    });
    refresh('territory-selection-river');
    return true;
  }

  function startNextDraft(current) {
    current.activePhase = 'drawing';
    dependencies.domains.editingDomain?.startDraft?.({ coords: [] });
    (0, dependencies.taskUi.setModeBanner)((0, dependencies.interactionPresentation.defaultDraftInstruction)());
    refresh('territory-selection-next-part');
  }

  function addPart() {
    const current = session();
    if (!canAddPart(current)) return false;
    touchSelection(current);
    if (current.activePhase === 'components') {
      const selected = new Set(current.selectedComponentKeys);
      const allItems = (0, dependencies.territoryComponents.territoryComponentItems)();
      const snapshotId = (0, dependencies.surfaces.uid)('territory-component-snapshot');
      current.componentSnapshots.push({ id: snapshotId, items: allItems });
      const selectedItems = allItems.filter(item => selected.has(item.key));
      current.parts.push(...selectedItems.map(item => ({
        id: (0, dependencies.surfaces.uid)('territory-part'),
        method: 'components',
        geometry: item.geometry,
        component: { ...item, snapshotId },
      })));
    } else {
      current.parts.push({
        id: (0, dependencies.surfaces.uid)('territory-part'),
        method: current.activeMethod,
        geometry: current.currentGeometry,
      });
    }
    current.currentGeometry = null;
    current.candidates = [];
    current.selectedCandidateIds = [];
    current.selectedComponentKeys = [];
    current.hoveredComponentKey = null;
    current.activeMethod = null;
    current.requestedMethod = null;
    current.activePhase = null;
    current.useRiverBoundaries = false;
    (0, dependencies.riverCandidates.resetRiverPartitionState)();
    refreshCombinedGeometry(current);
    schedulePreview();
    dependencies.domains.editingDomain?.clearDraft?.({ reason: 'territory-selection-part-archived', render: false });
    refresh('territory-selection-part-archived');
    return true;
  }

  function undoPart() {
    const current = session();
    if (!current || current.stage !== 'selection'
      || dependencies.domains.editingDomain?.draftInputActive?.() && (0, dependencies.countryEditingA.editingDraftCoordinates)().length) return false;
    if (!current.currentGeometry && !current.parts.length && !current.selectedComponentKeys.length && !current.selectedCandidateIds.length) return false;
    touchSelection(current);
    if (current.activePhase === 'candidate' && current.selectedCandidateIds.length) {
      current.selectedCandidateIds.pop();
      current.currentGeometry = null;
      refreshCombinedGeometry(current);
      refresh('territory-selection-candidate-undone');
      return true;
    }
    if (current.activePhase === 'components' && current.selectedComponentKeys.length) {
      current.selectedComponentKeys.pop();
      current.currentGeometry = null;
      (0, dependencies.territoryComponentUi.updateTerritoryComponentSelectionFeedback)();
    } else if (current.currentGeometry) {
      current.currentGeometry = null;
    } else {
      current.parts.pop();
      const referencedSnapshots = new Set(current.parts.map(part => part.component?.snapshotId).filter(Boolean));
      current.componentSnapshots = current.componentSnapshots.filter(snapshot => referencedSnapshots.has(snapshot.id));
    }
    current.candidates = [];
    current.selectedCandidateIds = [];
    refreshCombinedGeometry(current);
    if (selectionGeometryReady(current)) schedulePreview();
    refresh('territory-selection-part-undone');
    return true;
  }

  function removePart(partId) {
    const current = session();
    if (!current || current.stage !== 'selection') return false;
    const index = current.parts.findIndex(part => part.id === partId);
    if (index < 0) return false;
    touchSelection(current);
    current.parts.splice(index, 1);
    const referenced = new Set(current.parts.map(part => part.component?.snapshotId).filter(Boolean));
    current.componentSnapshots = current.componentSnapshots.filter(snapshot => referenced.has(snapshot.id));
    refreshCombinedGeometry(current);
    if (selectionGeometryReady(current)) schedulePreview();
    refresh('territory-selection-part-removed');
    return true;
  }

  function redraw() {
    const current = session();
    if (!current || current.stage !== 'selection' || current.activePhase === 'components') return false;
    touchSelection(current);
    current.currentGeometry = null;
    current.candidates = [];
    current.selectedCandidateIds = [];
    refreshCombinedGeometry(current);
    startNextDraft(current);
    return true;
  }

  function partCount(current = session()) {
    if (!current) return 0;
    return current.parts.length + (current.activePhase === 'components'
      ? current.selectedComponentKeys.length
      : current.currentGeometry ? 1 : 0);
  }

  function canAddPart(current = session()) {
    if (!current || current.stage !== 'selection' || !current.currentGeometry || !previewReady(current)) return false;
    if (!['candidate', 'components', 'result'].includes(current.activePhase)) return false;
    return current.kind === 'annex' || current.unboundedMethods.includes(current.activeMethod) || !!current.remainingGeometry;
  }

  function selectionGeometryReady(current = session()) {
    if (!current || current.computationPending || !['selection', 'review'].includes(current.stage)) return false;
    if (current.stage === 'review') return !!current.combinedGeometry && !dependencies.domains.editingDomain?.draftInputActive?.();
    if (current.activePhase === 'components') {
      return (!current.useRiverBoundaries || current.riverPartitionStatus === 'ready') && current.selectedComponentKeys.length > 0 && !!current.currentGeometry;
    }
    if (current.activePhase === 'candidate' && !current.selectedCandidateIds.length) return false;
    if (current.activePhase === 'drawing' || current.activePhase === 'source' || current.activePhase === 'preparing') return false;
    return !!current.combinedGeometry && !dependencies.domains.editingDomain?.draftInputActive?.();
  }

  function schedulePreview({ delay = 300 } = {}) {
    const current = session();
    if (!selectionGeometryReady(current)) return false;
    cancelPreview({ discard: true });
    const generation = previewGeneration;
    const key = previewKey(current);
    current.previewPending = true;
    refresh('territory-selection-preview-pending');
    previewTimer = setTimeout(async () => {
      previewTimer = null;
      if (generation !== previewGeneration || !previewIsCurrent(current, key)) return;
      if (!selectionGeometryReady(current)) {
        current.previewPending = false;
        refresh('territory-selection-preview-deferred');
        return;
      }
      let prepared = false;
      try {
        prepared = await adapterFor(current)?.preview?.(current, key) === true;
      } catch (error) {
        if (previewIsCurrent(current, key)) {
          (0, dependencies.feedback.reportOperationError)(
            error,
            '선택한 영역의 결과를 준비하지 못했습니다. 영역을 다시 선택하세요.',
            'PL-TERRITORY-PREVIEW-001',
            3800,
          );
        }
      } finally {
        if (previewIsCurrent(current, key)) {
          current.previewPending = false;
          current.previewReadyKey = prepared && dependencies.projectState.state.geometryPreview.session ? key : null;
          refresh('territory-selection-preview-ready');
          if (current.activePhase === 'components'
            && current.selectedComponentKeys.length && current.requestedMethod
            && current.requestedMethod !== current.activeMethod) {
            const requested = current.requestedMethod;
            if (canAddPart(current)) {
              try { await archiveComponentsAndActivate(current, requested); }
              catch (error) {
                dependencies.feedback.reportOperationError(error, '선택한 영역을 보관하지 못했습니다.', 'PL-TERRITORY-SELECTION-002', 3800);
              }
            } else {
              current.requestedMethod = current.activeMethod;
              refresh('territory-selection-method-await-failed');
            }
          }
        }
      }
    }, Math.max(0, Number(delay) || 0));
    return true;
  }

  function previewReady(current = session()) {
    return !!current && !current.computationPending && current.previewReadyKey === previewKey(current)
      && !current.previewPending
      && selectionGeometryReady(current)
      && !!dependencies.projectState.state.geometryPreview.session
      && dependencies.projectState.state.geometryPreview.session.validation?.blocking !== true;
  }

  async function apply() {
    const current = session();
    if (!current || current.stage !== 'review' || current.applying || !previewReady(current)) return false;
    current.applying = true;
    refresh('territory-selection-applying');
    try {
      const applied = await (0, dependencies.geometryOperations.applyActiveGeometryPreview)();
      if (!applied) return false;
      if (session() === current) {
        (0, dependencies.riverCandidates.resetRiverPartitionState)();
        dependencies.projectState.state.territorySelectionSession = null;
      }
      return true;
    } finally {
      if (session() === current) current.applying = false;
      refresh('territory-selection-apply-finished');
    }
  }

  function presentation(current = session()) {
    if (!current) return null;
    const adapter = adapterFor(current);
    if (!adapter) return null;
    const step = current.stage === 'setup' ? 1 : current.stage === 'selection' ? 2 : 3;
    const scoped = current.kind === 'annex' || current.kind === 'entity' && current.entityKind === 'general' && !current.parentId;
    const stageLabel = current.stage === 'setup' ? current.setupStageLabel
      : current.stage === 'selection' ? scoped ? '영토 선택' : '영역 선택'
        : current.kind === 'annex' ? '편입 확인' : '생성 확인';
    const count = partCount(current);
    const primaryLabel = current.stage === 'selection' && current.computationError ? '다시 계산' : current.stage === 'review'
      ? current.editOperation === 'annex' ? '편입' : adapter.finalLabel(count)
      : '다음';
    return {
      current,
      step,
      taskName: current.taskLabel,
      stageLabel,
      setup: current.stage === 'setup',
      selection: current.stage === 'selection',
      review: current.stage === 'review',
      line: current.stage === 'selection' && current.activePhase === 'drawing' && current.activeMethod === 'line',
      polygon: current.stage === 'selection' && current.activePhase === 'drawing' && current.activeMethod === 'polygon',
      candidate: current.stage === 'selection' && current.activePhase === 'candidate',
      result: current.stage === 'selection' && current.activePhase === 'result',
      components: current.stage === 'selection' && current.activePhase === 'components',
      activeMethod: current.activeMethod || current.requestedMethod,
      showSetup: current.stage === 'setup' && adapter.showSetup,
      showName: current.stage === 'setup' && adapter.supportsName,
      referenceLabel: adapter.referenceLabel,
      showEntityFields: current.stage === 'setup' && current.kind === 'entity',
      showReference: adapter.showReference(current)
        || current.stage === 'selection' && current.activePhase === 'source',
      showMethods: current.stage === 'selection',
      showReferenceStart: current.stage === 'selection' && current.activePhase === 'source',
      showMethodChangeConfirmation: !!current.methodChangeConfirmation,
      methodChangeConfirmationMessage: current.methodChangeConfirmation?.type === 'settings'
        ? '선택한 영역을 모두 지우고 설정을 바꿀까요?'
        : '현재 영역을 버리고 방식을 바꿀까요?',
      showRiver: current.stage === 'selection' && current.activePhase === 'components',
      count,
      canAddPart: canAddPart(current),
      primaryLabel,
      primaryIcon: current.stage === 'review' ? '#icon-check' : '#icon-chevron-right',
      primaryDisabled: current.applying || current.previewPending
        || current.stage === 'setup' && !setupValid(current)
        || current.stage === 'selection' && current.activePhase === 'candidate' && !!current.currentGeometry
        || current.stage === 'selection' && !current.computationError && !previewReady(current)
        || current.stage === 'review' && !previewReady(current),
      cancelLabel: current.stage === 'setup' ? '취소' : '뒤로',
      cancelIcon: current.stage === 'setup' ? '#icon-close' : '#icon-chevron-left',
      referenceCount: current.sourceCountryIds.length,
    };
  }

  return Object.freeze({
    connect,
    initializeTerritorySelectionWorkflow,
    get activeSession() { return session; },
    get addPart() { return addPart; },
    get advance() { return advance; },
    get apply() { return apply; },
    get back() { return back; },
    get canAddPart() { return canAddPart; },
    get cancelMethodChange() { return cancelMethodChange; },
    get cancelPreview() { return cancelPreview; },
    get clear() { return clear; },
    get confirmMethodChange() { return confirmMethodChange; },
    get countryPickingActive() { return countryPickingActive; },
    get finishDraft() { return finishDraft; },
    get partCount() { return partCount; },
    get presentation() { return presentation; },
    get previewIsCurrent() { return previewIsCurrent; },
    get previewKey() { return previewKey; },
    get previewReady() { return previewReady; },
    get redraw() { return redraw; },
    get removePart() { return removePart; },
    get refreshCombinedGeometry() { return refreshCombinedGeometry; },
    get resetSelection() { return resetSelection; },
    get schedulePreview() { return schedulePreview; },
    get selectCandidate() { return selectCandidate; },
    get selectMethod() { return selectMethod; },
    get setCurrentCandidates() { return setCurrentCandidates; },
    get setupValid() { return setupValid; },
    get sourceCountryInstruction() { return sourceCountryInstruction; },
    get startReferenceMethod() { return startReferenceMethod; },
    get start() { return start; },
    get toggleComponent() { return toggleComponent; },
    get toggleRiverBoundaries() { return toggleRiverBoundaries; },
    get toggleSourceCountry() { return toggleSourceCountry; },
    get touchSelection() { return touchSelection; },
    get undoPart() { return undoPart; },
    get updateName() { return updateName; },
  });
}
