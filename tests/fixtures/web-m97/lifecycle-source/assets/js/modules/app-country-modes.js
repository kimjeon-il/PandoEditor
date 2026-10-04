import { subunitSelectionPolicy } from './territorial-interaction-policy.js';
import './territorial-edit-plan.js';
/** CountryModes: extracted application responsibility.
 * Dependencies are explicitly wired once by the composition modules.
 * Mutable bindings stay local; exported accessors retain live identity.
 */
export function createCountryModes() {
  let dependencies;
  let hydroToolConfig;
  let draftToolConfig;
  let emptyDraftSession;
  let editingDraftSnapshot;
  let editingDraftCoordinates;
  let dispatchEditingInteraction;
  function connect(ports) {
    if (dependencies) throw new Error('country-modes already connected');
    dependencies = ports;
  }

  function resetMergeState() {
    dependencies.projectState.state.mergeSourceCountryId = null;
    dependencies.projectState.state.mergeTargetCountryIds = [];
  }

  function resetGenericFeatureMergeState() {
    dependencies.projectState.state.genericFeatureMergeSourceId = null;
    dependencies.projectState.state.genericFeatureMergeTargetIds = [];
  }

  function resetTerritorialUnitEditState() {
    if (dependencies.projectState.state.territorySelectionSession?.tool === 'draw-territorial-unit') {
      (0, dependencies.territorySelectionA.clearTerritorySelection)({ discardPreview: true, refreshUi: false });
    }
    dependencies.projectState.state.territorialUnitMergeSourceId = null;
    dependencies.projectState.state.territorialUnitMergeTargetIds = [];
    dependencies.projectState.state.territorialUnitSplitSourceId = null;
    dependencies.projectState.state.territorialUnitSplitVirtualSource = null;
    dependencies.projectState.state.territorialUnitRedrawSourceId = null;
    dependencies.projectState.state.multiDraft = null;
  }

  function draftMinimumPoints() {
    return (0, dependencies.surfaces.isPolygonDraftTool)(dependencies.projectState.state.tool) ? 3 : 2;
  }

  function resetTerritoryEditingState(invalidateInteraction = true) {
    dependencies.domains.editingDomain?.clearDraft?.(invalidateInteraction);
    if (dependencies.projectState.state.territorySelectionSession) {
      (0, dependencies.territorySelectionA.clearTerritorySelection)({ discardPreview: true, refreshUi: false });
      return;
    }
  }

  function resetBoundaryEditState() {
    dependencies.projectState.state.boundaryEditEntityIds = [];
    dependencies.projectState.state.boundaryEditAutoSeedId = null;
    dependencies.projectState.state.boundaryEditPhase = null;
    dependencies.projectState.state.boundaryEditInitialSelection = null;
    dependencies.projectState.state.boundaryEditSeedEntityId = null;
  }

  function enterTerrainGenericFeatureMode(tool) {
    const config = hydroToolConfig(tool);
    if (!config) return false;
    (0, dependencies.readinessUi.clearNotification)();
    dependencies.domains.selectionUiController.clear({ reason: 'tool-mode-selection-clear' });
    resetTerritoryEditingState(true);
    dependencies.projectState.state.coastEditCountryId = null;
    resetMergeState();
    dependencies.projectState.state.multiDraft = { kind: 'hydro', category: config.category, shape: config.category === 'lake' ? 'polygon' : 'line', parts: [], current: null };
    dependencies.domains.editingDomain?.setTool(tool, { announce: false });
    (0, dependencies.taskUi.setModeBanner)((0, dependencies.interactionPresentation.defaultDraftInstruction)());
    (0, dependencies.taskUi.updateModeButtons)();
    return true;
  }

  function enterAnnexTerritoryMode(id) {
    if (!dependencies.territorialServicesA.planDrawnTerritoryAnnex || !dependencies.territorialModel.composeRiverBoundaryTerritoryComponents) {
      (0, dependencies.feedback.setActionStatus)('영토 편입 도구를 준비하는 중입니다.', 'working', 0);
      void (0, dependencies.gisServicesA.ensureGisRuntime)()
        .then(() => enterAnnexTerritoryMode(id))
        .catch(error => (0, dependencies.feedback.reportOperationError)(error, '영토 편입 도구를 불러오지 못했습니다.', 'PL-GIS-LAZY-001', 4200));
      return true;
    }
    (0, dependencies.readinessUi.clearNotification)();
    const feature = dependencies.territorialModel.entityRepository.get(id);
    if (!(feature?.properties?.entityKind === 'general' && !feature?.properties?.parentId)) return false;
    if (!(0, dependencies.objectOperationsB.requireObjectsUnlocked)([id].map(id => ({ domain: 'territorial', type: 'entity', id: String(id) })), '영토 편입을 시작')) return false;
    const current = (0, dependencies.territorySelectionA.startTerritorySelection)('annex', {
      tool: 'annex-territory', targetCountryId: String(id), sourceCountryIds: [],
    });
    if (!current) return false;
    (0, dependencies.taskPresentation.syncCountryActionButtons)();
    dependencies.domains.renderingDomain?.invalidateEditingOverlays?.('annex-target-selected');
    (0, dependencies.taskUi.setModeBanner)('가져올 국가를 선택하세요.');
    (0, dependencies.taskUi.updateModeButtons)();
    return true;
  }

  function validateAnnexSelectionSetup(session) {
    const targetId = String(session?.targetCountryId || '');
    return !!targetId && ((dependencies.territorialModel.entityRepository.get(targetId)?.properties?.entityKind === 'general' && !dependencies.territorialModel.entityRepository.get(targetId)?.properties?.parentId))
      && Array.isArray(session.sourceCountryIds) && session.sourceCountryIds.length > 0
      && session.sourceCountryIds.every(sourceId => String(sourceId) !== targetId && ((dependencies.territorialModel.entityRepository.get(sourceId)?.properties?.entityKind === 'general' && !dependencies.territorialModel.entityRepository.get(sourceId)?.properties?.parentId)));
  }

  function validateNewCountrySelectionSetup(session) {
    return !!session?.name?.trim() && Array.isArray(session.sourceCountryIds) && session.sourceCountryIds.length > 0
      && session.sourceCountryIds.every(sourceId => ((dependencies.territorialModel.entityRepository.get(sourceId)?.properties?.entityKind === 'general' && !dependencies.territorialModel.entityRepository.get(sourceId)?.properties?.parentId)));
  }

  function prepareCountrySelectionSource(session, operationLabel) {
    const ids = session.sourceCountryIds.map(String);
    const lockIds = session.kind === 'annex' ? [session.targetCountryId, ...ids] : ids;
    if (!(0, dependencies.objectOperationsB.requireObjectsUnlocked)(lockIds.map(id => ({ domain: 'territorial', type: 'entity', id: String(id) })), operationLabel)) return false;
    try {
      const fingerprint = [...ids].sort().join('|');
      if (session.sourceCountryFingerprint === fingerprint && session.baseSourceGeometry) return true;
      const features = ids.map(id => dependencies.territorialModel.entityRepository.get(id)).filter(Boolean);
      if (!features.length) return false;
      // The selection worker unions multiple sources; keep immutable source references here.
      session.baseSourceGeometry = features.length === 1 ? features[0].geometry : null;
      session.workingSourceGeometry = session.baseSourceGeometry;
      session.remainingGeometry = session.baseSourceGeometry;
      session.sourceRevision += 1;
      session.sourceCountryFingerprint = fingerprint;
      session.componentFeatures = features;
      return true;
    } catch (error) {
      (0, dependencies.feedback.reportOperationError)(error, '선택한 국가의 영토를 준비할 수 없습니다. 대상을 다시 선택하세요.', 'PL-TERRITORY-SELECTION-002', 3800);
      return false;
    }
  }

  function prepareAnnexSelection(session) {
    return validateAnnexSelectionSetup(session) && prepareCountrySelectionSource(session, '영토 편입을 시작');
  }

  function prepareNewCountrySelection(session) {
    return validateNewCountrySelectionSetup(session) && prepareCountrySelectionSource(session, '새 국가 분리 작업을 시작');
  }

  function selectionSessionSnapshot() {
    const snapshot = dependencies.domains.selectionDomain.snapshot().selection;
    return {
      primaryKey: snapshot.primaryKey,
      items: snapshot.items.map(ref => ({ domain: ref.domain, type: ref.type, id: ref.id })),
    };
  }

  function enterCountryBorderSelection(id) {
    (0, dependencies.readinessUi.clearNotification)();
    const feature = dependencies.territorialModel.entityRepository.get(id);
    if (!(feature?.properties?.entityKind === 'general' && !feature?.properties?.parentId)) return false;
    if (!(0, dependencies.objectOperationsB.requireObjectsUnlocked)([id].map(id => ({ domain: 'territorial', type: 'entity', id: String(id) })), '국경 조정 대상을 선택')) return false;
    const initialSelection = selectionSessionSnapshot();
    if (!dependencies.domains.editingDomain?.setTool('territorial-border', { announce: false })) return false;
    dependencies.projectState.state.boundaryEditAutoSeedId = null;
    dependencies.projectState.state.boundaryEditEntityIds = [String(id)];
    dependencies.projectState.state.boundaryEditPhase = 'selecting';
    dependencies.projectState.state.boundaryEditInitialSelection = initialSelection;
    dependencies.projectState.state.boundaryEditSeedEntityId = String(id);
    dependencies.domains.selectionUiController.replaceMany(dependencies.projectState.state.boundaryEditEntityIds.map(id => dependencies.selectionServices.normalizeObjectRef({ domain: 'territorial', type: 'entity', id })), {
      primary: dependencies.selectionServices.normalizeObjectRef({ domain: 'territorial', type: 'entity', id: String(id) }), scope: 'map', reason: 'boundary-edit-selection', present: false,
    });
    (0, dependencies.geometryPreview.rebuildBoundaryTopology)(dependencies.projectState.state.boundaryEditEntityIds);
    (0, dependencies.taskUi.setModeBanner)(`${(0, dependencies.objectPresentation.territorialEntityName)(feature)}와 접한 국가를 선택하세요.`);
    dependencies.domains.renderingDomain?.invalidateGpuInteraction?.('territorial-border-selection');
    (0, dependencies.taskUi.updateModeButtons)();
    return true;
  }

  function enterTerritorialBorderEditFromSelection(operation = 'boundary') {
    (0, dependencies.readinessUi.clearNotification)();
    const snapshot = selectionSessionSnapshot();
    const refs = dependencies.domains.selectionDomain.snapshot().selection.items;
    if (refs.length >= 2 && refs.every(ref => ref.domain === 'territorial' && (dependencies.territorialModel.entityRepository.get(ref?.id)?.properties.entityKind === 'general' && !!dependencies.territorialModel.entityRepository.get(ref?.id)?.properties.parentId))) {
      const units = refs.map(ref => dependencies.territorialModel.entityRepository.get(ref.id));
      const policy = subunitSelectionPolicy(units, operation === 'merge' ? { adjacent: (a, b) => globalThis.PandoLabTerritorialEdit.createKernel(window.polygonClipping).adjacent(a.geometry, b.geometry) } : { deferConnectivity: true });
      if (!policy.valid) { (0, dependencies.feedback.setActionStatus)(policy.message, 'error', 3600); return false; }
      if (operation === 'merge') return (0, dependencies.territorialEditingB.previewTerritorialEdit)({ operation: 'merge', targetId: units[0].id,
        parentId: units[0].properties.parentId, sourceIds: units.slice(1).map(unit => unit.id),
      }, { selectedId: units[0].id });
      return (0, dependencies.territorialEditingA.enterTerritorialUnitRedrawMode)(units[0].id, units.map(unit => String(unit.id)));
    }
    const ids = refs.filter(ref => ref.domain === 'territorial' && (dependencies.territorialModel.entityRepository.get(ref?.id)?.properties.entityKind === 'general' && !dependencies.territorialModel.entityRepository.get(ref?.id)?.properties.parentId)).map(ref => ref.id);
    if (ids.length !== refs.length || ids.length < 2) {
      (0, dependencies.feedback.setActionStatus)('국가를 2개 이상 선택하세요', 'error', 3200);
      return false;
    }
    if (!(0, dependencies.objectOperationsB.requireObjectsUnlocked)(ids.map(id => ({ domain: 'territorial', type: 'entity', id: String(id) })), '국경 조정을 시작')) return false;
    const analysis = { selectedIds: ids };
    if (!dependencies.domains.editingDomain?.setTool('territorial-border', { announce: false })) return false;
    dependencies.projectState.state.boundaryEditAutoSeedId = null;
    dependencies.projectState.state.boundaryEditEntityIds = analysis.selectedIds;
    dependencies.projectState.state.boundaryEditPhase = 'editing';
    dependencies.projectState.state.boundaryEditInitialSelection = snapshot;
    dependencies.projectState.state.boundaryEditSeedEntityId = analysis.selectedIds[0];
    dependencies.domains.selectionUiController.replaceMany(analysis.selectedIds.map(id => dependencies.selectionServices.normalizeObjectRef({ domain: 'territorial', type: 'entity', id })), {
      primary: dependencies.selectionServices.normalizeObjectRef({ domain: 'territorial', type: 'entity', id: String(analysis.selectedIds.at(-1)) }), scope: 'map', reason: 'boundary-edit-selection', present: false,
    });
    (0, dependencies.geometryPreview.rebuildBoundaryTopology)(analysis.selectedIds);
    (0, dependencies.taskUi.setModeBanner)('공유국경 꼭짓점을 드래그하세요. 외부 접점은 고정됩니다.');
    dependencies.domains.renderingDomain?.invalidateGpuInteraction?.('territorial-border-edit-selection');
    (0, dependencies.taskUi.updateModeButtons)();
    return true;
  }

  function boundaryNeighborIds(selectedCountryIds = dependencies.projectState.state.boundaryEditEntityIds) {
    return new Set(dependencies.projectState.state.boundaryPreparation?.status === 'ready'
      ? dependencies.projectState.state.boundaryPreparation.result.neighbors : []);
  }

  function toggleBoundaryEditCountry(id) {
    if (dependencies.projectState.state.tool !== 'territorial-border' || dependencies.projectState.state.boundaryEditPhase !== 'selecting') return false;
    const countryId = String(id || '');
    if (!(dependencies.territorialModel.entityRepository.get(countryId)?.properties?.entityKind === 'general' && !dependencies.territorialModel.entityRepository.get(countryId)?.properties?.parentId)) return false;
    const selected = new Set(dependencies.projectState.state.boundaryEditEntityIds.map(String));
    if (selected.has(countryId)) {
      if (countryId === dependencies.projectState.state.boundaryEditSeedEntityId) {
        (0, dependencies.feedback.setActionStatus)('시작 국가는 대상 선택 단계에서 해제할 수 없습니다.', 'error', 2800);
        return false;
      }
      selected.delete(countryId);
    } else {
      if (dependencies.projectState.state.boundaryPreparation?.status !== 'ready') return false;
      if (!boundaryNeighborIds([...selected]).has(countryId)) {
        (0, dependencies.feedback.setActionStatus)('현재 선택 집합과 실제 국경을 맞댄 국가만 추가할 수 있습니다.', 'error', 3200);
        return false;
      }
      selected.add(countryId);
    }
    dependencies.projectState.state.boundaryEditEntityIds = [...selected];
    dependencies.domains.selectionUiController.replaceMany(dependencies.projectState.state.boundaryEditEntityIds.map(id => dependencies.selectionServices.normalizeObjectRef({ domain: 'territorial', type: 'entity', id })), {
      primary: dependencies.selectionServices.normalizeObjectRef({ domain: 'territorial', type: 'entity', id: String(countryId) }), scope: 'map', reason: 'boundary-edit-selection', present: false,
    });
    (0, dependencies.geometryPreview.rebuildBoundaryTopology)(dependencies.projectState.state.boundaryEditEntityIds);
    const analysis = (0, dependencies.geometryOperations.boundaryEditSelectionAnalysis)(dependencies.projectState.state.boundaryEditEntityIds);
    (0, dependencies.taskUi.setModeBanner)(analysis.valid
      ? `${analysis.selectedIds.length}개 국가 선택됨 · 완료하면 공유국경을 편집합니다.`
      : analysis.message);
    dependencies.domains.renderingDomain?.invalidateGpuInteraction?.('territorial-border-country-toggle');
    (0, dependencies.taskUi.updateModeButtons)();
    return true;
  }

  function beginTerritorialBorderEditing() {
    if (dependencies.projectState.state.tool !== 'territorial-border' || dependencies.projectState.state.boundaryEditPhase !== 'selecting') return false;
    if (!(0, dependencies.objectOperationsB.requireObjectsUnlocked)(dependencies.projectState.state.boundaryEditEntityIds.map(id => ({ domain: 'territorial', type: 'entity', id: String(id) })), '국경 조정을 시작')) return false;
    const analysis = (0, dependencies.geometryOperations.boundaryEditSelectionAnalysis)(dependencies.projectState.state.boundaryEditEntityIds);
    if (!analysis.valid) {
      (0, dependencies.feedback.setActionStatus)(analysis.message, 'error', 3400);
      return false;
    }
    dependencies.projectState.state.boundaryEditAutoSeedId = null;
    dependencies.projectState.state.boundaryEditEntityIds = analysis.selectedIds;
    dependencies.projectState.state.boundaryEditPhase = 'editing';
    (0, dependencies.geometryPreview.rebuildBoundaryTopology)(analysis.selectedIds);
    (0, dependencies.taskUi.setModeBanner)('공유국경 꼭짓점을 드래그하세요. 외부 접점은 고정됩니다.');
    dependencies.domains.renderingDomain?.invalidateGpuInteraction?.('territorial-border-edit-start');
    (0, dependencies.taskUi.updateModeButtons)();
    return true;
  }

  function finishTerritorialBorderEdit() {
    if (dependencies.projectState.state.tool !== 'territorial-border') return false;
    const seedEntity = dependencies.territorialModel.entityRepository.get(dependencies.projectState.state.boundaryEditSeedEntityId);
    if ((seedEntity?.properties?.entityKind === 'general' && !!seedEntity?.properties?.parentId)) {
      dependencies.domains.editingDomain?.setTool('select', { announce: false });
      dependencies.projectState.state.boundaryPreparation?.cancel();
      dependencies.projectState.state.boundaryPreparation = null;

      return true;
    }
    const ids = dependencies.projectState.state.boundaryEditEntityIds.slice();
    const primaryId = (dependencies.projectState.state.selected?.domain === 'territorial' && (dependencies.territorialModel.entityRepository.get(dependencies.projectState.state.selected?.id)?.properties.entityKind === 'general' && !dependencies.territorialModel.entityRepository.get(dependencies.projectState.state.selected?.id)?.properties.parentId)) && ids.includes(String(dependencies.projectState.state.selected.id)) ? String(dependencies.projectState.state.selected.id) : ids.at(-1);
    dependencies.domains.editingDomain?.setTool('select', { announce: false });
    dependencies.projectState.state.boundaryPreparation?.cancel();
    dependencies.projectState.state.boundaryPreparation = null;

    dependencies.domains.selectionUiController.replaceMany(ids.map(id => dependencies.selectionServices.normalizeObjectRef({ domain: 'territorial', type: 'entity', id })), {
      primary: dependencies.selectionServices.normalizeObjectRef({ domain: 'territorial', type: 'entity', id: String(primaryId) }), scope: 'map', reason: 'boundary-edit-commit', present: true,
    });
    dependencies.domains.projectDomain.queueAutosave();
    (0, dependencies.feedback.setActionStatus)(`${ids.length}개 국가 사이의 공유국경 조정을 완료했습니다.`, 'success');
    return true;
  }

  function enterCountryCoastEdit(id, { scopeGenericFeatureId = null, returnSelection = null } = {}) {
    (0, dependencies.readinessUi.clearNotification)();
    const feature = dependencies.territorialModel.entityRepository.get(id);
    if (!(feature?.properties?.entityKind === 'general' && !feature?.properties?.parentId)) return false;
    if (!(0, dependencies.objectOperationsB.requireObjectsUnlocked)([id].map(id => ({ domain: 'territorial', type: 'entity', id: String(id) })), '해안선 조정을 시작')) return false;
    if (!dependencies.domains.editingDomain?.setTool('country-coast', { announce: false })) return false;
    dependencies.projectState.state.coastEditCountryId = String(id);
    dependencies.projectState.state.coastEditScopeGenericFeatureId = scopeGenericFeatureId ? String(scopeGenericFeatureId) : null;
    dependencies.projectState.state.coastEditReturnSelection = returnSelection ? (0, dependencies.platform.deepClone)(returnSelection) : null;
    (0, dependencies.geometryPreview.rebuildBoundaryTopology)(id);
    (0, dependencies.taskPresentation.syncCountryActionButtons)();
    (0, dependencies.taskUi.setModeBanner)(scopeGenericFeatureId
      ? '해안선 꼭짓점을 드래그하세요. 연결 영역도 함께 변경됩니다.'
      : '해안선 꼭짓점을 드래그하세요. 국경 접점은 고정됩니다.');
    return true;
  }

  function finishCountryCoastEdit() {
    const id = dependencies.projectState.state.coastEditCountryId;
    if (!id) return;
    const feature = dependencies.territorialModel.entityRepository.get(id);
    const returnSelection = dependencies.projectState.state.coastEditReturnSelection ? (0, dependencies.platform.deepClone)(dependencies.projectState.state.coastEditReturnSelection) : null;
    dependencies.domains.editingDomain?.setTool('select', { announce: false });
    dependencies.projectState.state.boundaryPreparation?.cancel();
    dependencies.projectState.state.boundaryPreparation = null;

    dependencies.projectState.state.coastEditScopeGenericFeatureId = null;
    dependencies.projectState.state.coastEditReturnSelection = null;
    if (returnSelection?.type === 'generic' && dependencies.projectState.state.genericFeatures.some(item => String(item.id) === String(returnSelection.id))) (0, dependencies.propertyEditingA.applyGenericSelectionIntent)(String(returnSelection.id), true);
    else if (feature) dependencies.domains.selectionUiController.applyIntent({ domain: 'territorial', type: 'entity', id: String(id) }, { refreshOnly: true, openEditor: false });
    dependencies.domains.projectDomain.queueAutosave();
    (0, dependencies.feedback.setActionStatus)(`${feature ? (0, dependencies.objectPresentation.territorialEntityName)(feature) : '국가'}의 해안선을 조정했습니다.`, 'success');
  }

  function enterMergeCountryMode(id) {
    (0, dependencies.readinessUi.clearNotification)();
    const feature = dependencies.territorialModel.entityRepository.get(id);
    if (!(feature?.properties?.entityKind === 'general' && !feature?.properties?.parentId)) return false;
    if (!(0, dependencies.objectOperationsB.requireObjectsUnlocked)([id].map(id => ({ domain: 'territorial', type: 'entity', id: String(id) })), '국가 합병을 시작')) return false;
    dependencies.projectState.state.mergeSourceCountryId = String(id);
    dependencies.projectState.state.mergeTargetCountryIds = [];
    dependencies.domains.editingDomain?.setTool('merge-country', { announce: false });
    dependencies.projectState.state.mergeSourceCountryId = String(id);
    dependencies.projectState.state.mergeTargetCountryIds = [];
    (0, dependencies.taskUi.setModeBanner)('합병할 국가를 선택하세요.');
    (0, dependencies.taskPresentation.syncCountryActionButtons)();
    (0, dependencies.taskUi.updateModeButtons)();
    return true;
  }

  function toggleMergeTarget(id) {
    const sourceId = String(dependencies.projectState.state.mergeSourceCountryId || '');
    const targetId = String(id || '');
    if (dependencies.projectState.state.tool !== 'merge-country' || !sourceId) return;
    if (!targetId || targetId === sourceId) {
      (0, dependencies.feedback.setActionStatus)('기준 국가 외 합병 대상을 선택하세요', 'error', 3200);
      return;
    }
    if (!(dependencies.territorialModel.entityRepository.get(targetId)?.properties?.entityKind === 'general' && !dependencies.territorialModel.entityRepository.get(targetId)?.properties?.parentId)) {
      (0, dependencies.feedback.setActionStatus)('합병 대상을 찾을 수 없습니다. 지도에 표시된 다른 국가를 선택하세요.', 'error', 3200);
      return;
    }
    const selected = new Set(dependencies.projectState.state.mergeTargetCountryIds.map(String));
    if (selected.has(targetId)) selected.delete(targetId);
    else selected.add(targetId);
    dependencies.projectState.state.mergeTargetCountryIds = [...selected];
    dependencies.domains.renderingDomain?.invalidateEditingOverlays?.('merge-country-target-selection-changed');
    (0, dependencies.taskUi.setModeBanner)('합병할 국가를 선택하세요.');
    (0, dependencies.taskUi.updateModeButtons)();
  }

  function cancelActiveMode(announce = true) {
    const cancelledTool = dependencies.projectState.state.tool;
    const boundarySelectionSnapshot = dependencies.projectState.state.boundaryEditInitialSelection;
    dependencies.spatialQuery.mapEditClient.cancel();
    const selectedTerritorialUnitId = dependencies.projectState.state.territorialUnitSplitSourceId || dependencies.projectState.state.territorialUnitMergeSourceId
      || ((dependencies.projectState.state.selected?.domain === 'territorial' && !(dependencies.territorialModel.entityRepository.get(dependencies.projectState.state.selected?.id)?.properties.entityKind === 'general' && !dependencies.territorialModel.entityRepository.get(dependencies.projectState.state.selected?.id)?.properties.parentId)) ? dependencies.projectState.state.selected.id : null);
    const selectedGenericFeatureId = dependencies.projectState.state.genericFeatureSplitSourceId || dependencies.projectState.state.genericFeatureMergeSourceId
      || (dependencies.projectState.state.coastEditReturnSelection?.type === 'generic' ? dependencies.projectState.state.coastEditReturnSelection.id : null);
    const selectedId = dependencies.projectState.state.territorySelectionSession?.targetCountryId
      || dependencies.projectState.state.coastEditCountryId
      || dependencies.projectState.state.mergeSourceCountryId
      || ((dependencies.projectState.state.selected?.domain === 'territorial' && (dependencies.territorialModel.entityRepository.get(dependencies.projectState.state.selected?.id)?.properties.entityKind === 'general' && !dependencies.territorialModel.entityRepository.get(dependencies.projectState.state.selected?.id)?.properties.parentId)) ? dependencies.projectState.state.selected.id : null);
    resetTerritoryEditingState(true);
    dependencies.projectState.state.coastEditCountryId = null;
    dependencies.projectState.state.coastEditScopeGenericFeatureId = null;
    dependencies.projectState.state.coastEditReturnSelection = null;
    resetBoundaryEditState();
    resetMergeState();
    resetGenericFeatureMergeState();
    resetTerritorialUnitEditState();
    dependencies.projectState.state.genericFeatureSplitSourceId = null;
    dependencies.domains.editingDomain?.setTool('select', { announce: false });
    dependencies.projectState.state.boundaryPreparation?.cancel();
    dependencies.projectState.state.boundaryPreparation = null;

    if (cancelledTool === 'territorial-border' && boundarySelectionSnapshot) dependencies.domains.selectionUiController.restore(boundarySelectionSnapshot);
    else if (selectedGenericFeatureId && dependencies.projectState.state.genericFeatures.some(item => String(item.id) === String(selectedGenericFeatureId))) (0, dependencies.propertyEditingA.applyGenericSelectionIntent)(String(selectedGenericFeatureId), true);
    else if (selectedTerritorialUnitId && dependencies.territorialModel.entityRepository.get(selectedTerritorialUnitId)) dependencies.domains.selectionUiController.applyIntent({ domain: 'territorial', type: 'entity', id: String(String(selectedTerritorialUnitId)) }, { refreshOnly: true, openEditor: false });
    else if (selectedId && dependencies.territorialModel.entityRepository.get(selectedId)) dependencies.domains.selectionUiController.applyIntent({ domain: 'territorial', type: 'entity', id: String(selectedId) }, { refreshOnly: true, openEditor: false });
    // Country tool highlighting lives in the GPU scene rather than the draft overlay.
    // Rebuild that scene on cancellation so its translucent fills do not remain until
    // the next camera movement triggers a full country presentation pass.
    if (['annex-territory', 'new-country', 'draw-territorial-unit', 'merge-country'].includes(cancelledTool)) {
      dependencies.domains.renderingDomain?.invalidateCountryPatch?.('active-mode-cancelled');
    } else {
      dependencies.domains.renderingDomain?.invalidateEditingOverlays?.('active-mode-cancelled');
    }
    const labels = { 'new-country': '국가 추가', 'annex-territory': '영토 편입', 'merge-country': '국가 합병', 'merge-generic-feature': '영역 합치기', 'split-generic-feature': '영역 나누기', 'merge-territorial-unit': '영역 합치기', 'split-territorial-unit': '영역 나누기', 'territorial-border': '국경 조정', 'country-coast': '해안선 조정' };
    if (announce) (0, dependencies.feedback.setActionStatus)(`${labels[cancelledTool] || '지도 작업'}을 취소했습니다.`, 'success');
  }

  function enterLabelMode() {
    (0, dependencies.readinessUi.clearNotification)();
    resetTerritoryEditingState(true);
    dependencies.projectState.state.coastEditCountryId = null;
    resetMergeState();
    dependencies.projectState.state.tool = 'label';
    dependencies.projectState.state.labelPlacementMode = true;
    (0, dependencies.readinessUi.setCurrentTool)('지명 추가');
    (0, dependencies.platform.$)('map').classList.add('generic-feature-mode');
    (0, dependencies.platform.$)('map').classList.remove('select-mode');
    (0, dependencies.taskUi.setModeBanner)('지명을 배치할 위치를 선택하세요.');
    (0, dependencies.taskUi.updateModeButtons)();
    return true;
  }

  function exitLabelMode(announce = true) {
    dependencies.projectState.state.labelPlacementMode = false;
    dependencies.projectState.state.tool = 'select';
    (0, dependencies.platform.$)('map').classList.remove('generic-feature-mode');
    (0, dependencies.platform.$)('map').classList.add('select-mode');
    (0, dependencies.taskUi.setModeBanner)();
    (0, dependencies.readinessUi.setCurrentTool)('국가 선택');
    (0, dependencies.taskUi.updateModeButtons)();
    if (announce) (0, dependencies.feedback.setActionStatus)('지명 추가를 취소했습니다.', 'success');
  }

  function initializeHydroToolConfig() {
    (hydroToolConfig = tool => dependencies.hydroPresentation.HYDRO_TOOL_CONFIG[tool] || null);

    (draftToolConfig = tool => (0, dependencies.toolServices.toolDraftDefinition)(tool, dependencies.projectState.state));
  }

  function initializeEmptyDraftSession() {
    (emptyDraftSession = Object.freeze({
      coords: Object.freeze([]), hover: null, inputPhase: 'draw', vertexInsertMode: false, selectedVertexIndex: null,
      insertTarget: null, dragging: false, issues: Object.freeze([]), historyCount: 0,
      futureCount: 0, strokeActive: false, cutAssessment: null, activeSnap: null,
    }));

    (editingDraftSnapshot = () => dependencies.domains.editingDomain?.snapshot?.().draft || emptyDraftSession);

    (editingDraftCoordinates = () => editingDraftSnapshot().coords);

    (dispatchEditingInteraction = (type, detail = {}) => {
      const currentPacket = dependencies.domains.editingDomain?.createRenderPacket?.();
      if (!currentPacket) return false;
      return dependencies.domains.editingDomain.handleInteraction({
        type,
        projectGeneration: currentPacket.projectGeneration,
        packetRevision: currentPacket.revision,
        ...detail,
      });
    });
  }

  return Object.freeze({
    connect,
    initializeHydroToolConfig,
    initializeEmptyDraftSession,
    get beginTerritorialBorderEditing() { return beginTerritorialBorderEditing; },
    get cancelActiveMode() { return cancelActiveMode; },
    get dispatchEditingInteraction() { return dispatchEditingInteraction; },
    get draftMinimumPoints() { return draftMinimumPoints; },
    get draftToolConfig() { return draftToolConfig; },
    get editingDraftCoordinates() { return editingDraftCoordinates; },
    get editingDraftSnapshot() { return editingDraftSnapshot; },
    get enterAnnexTerritoryMode() { return enterAnnexTerritoryMode; },
    get enterTerritorialBorderEditFromSelection() { return enterTerritorialBorderEditFromSelection; },
    get enterCountryBorderSelection() { return enterCountryBorderSelection; },
    get enterCountryCoastEdit() { return enterCountryCoastEdit; },
    get enterLabelMode() { return enterLabelMode; },
    get enterMergeCountryMode() { return enterMergeCountryMode; },
    get enterTerrainGenericFeatureMode() { return enterTerrainGenericFeatureMode; },
    get exitLabelMode() { return exitLabelMode; },
    get finishTerritorialBorderEdit() { return finishTerritorialBorderEdit; },
    get finishCountryCoastEdit() { return finishCountryCoastEdit; },
    get hydroToolConfig() { return hydroToolConfig; },
    get resetBoundaryEditState() { return resetBoundaryEditState; },
    get resetGenericFeatureMergeState() { return resetGenericFeatureMergeState; },
    get resetMergeState() { return resetMergeState; },
    get resetTerritorialUnitEditState() { return resetTerritorialUnitEditState; },
    get resetTerritoryEditingState() { return resetTerritoryEditingState; },
    get toggleBoundaryEditCountry() { return toggleBoundaryEditCountry; },
    get toggleMergeTarget() { return toggleMergeTarget; },
    get prepareAnnexSelection() { return prepareAnnexSelection; },
    get prepareNewCountrySelection() { return prepareNewCountrySelection; },
    get validateAnnexSelectionSetup() { return validateAnnexSelectionSetup; },
    get validateNewCountrySelectionSetup() { return validateNewCountrySelectionSetup; },
  });
}
