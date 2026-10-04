import { adoptBoundaryRenderPacketAsync } from './editing-render-packet.js';
/** GeometryPreview: extracted application responsibility.
 * Dependencies are explicitly wired once by the composition modules.
 * Mutable bindings stay local; exported accessors retain live identity.
 */
export function createGeometryPreview() {
  let dependencies;
  let editPreviewController;
  let editPipelineMetrics;
  let boundarySelectionAnalysisCache;
  let boundarySelectionAnalysisMetrics;
  let activeGeometryPreviewApply;
  let activeGeometryPreviewDiscard;
  let localPreparationEpoch = 0;
  let localPreparationPending = false;
  function connect(ports) {
    if (dependencies) throw new Error('geometry-preview already connected');
    dependencies = ports;
  }

  function assertCurrentProjectReferences() {
    return (0, dependencies.territorialModel.assertProjectReferenceIntegrity)({
      territorialEntities: dependencies.territorialModel.entityRepository.list(),
      distributionLayers: dependencies.projectState.state.distributionLayers || [],
      distributionEntries: dependencies.projectState.state.distributionEntries || [],
      labels: dependencies.projectState.state.labels || [],
      genericFeatures: dependencies.projectState.state.genericFeatures || [],
      itemVisibility: dependencies.projectState.state.itemVisibility || {},
      labelSettings: dependencies.projectState.state.labelSettings || {},
    });
  }

  function transactMapEdit({ operation, payload, snapshot, applyResult, onSuccess, onError }) {
    return (0, dependencies.geometryEditingCore.runMapEditTransaction)({
      client: dependencies.spatialQuery.mapEditClient,
      operation,
      payload,
      snapshot,
      applyResult,
      validateCanonical: assertCurrentProjectReferences,
      commitHistory: (...args) => dependencies.domains.projectDomain.commitHistorySnapshot(...args),
      restore: (editableSnapshot, { rebaseWorker }) => {
        (0, dependencies.validation.restoreEditTransactionSnapshot)(editableSnapshot);
        if (rebaseWorker) dependencies.spatialQuery.mapEditClient.rebase();
      },
      queueAutosave: (...args) => dependencies.domains.projectDomain.queueAutosave(...args),
      diagnostic: dependencies.readiness.reliabilityDiagnostic,
      onSuccess,
      onError,
    });
  }

  function geometryPolygonSets(geometry) {
    if (!geometry) return [];
    if (geometry.type === 'Polygon') return [geometry.coordinates || []];
    if (geometry.type === 'MultiPolygon') return geometry.coordinates || [];
    return [];
  }

  function coordKey(coord, precision = 7) {
    return `${Number(coord?.[0] || 0).toFixed(precision)},${Number(coord?.[1] || 0).toFixed(precision)}`;
  }

  function coordNear(a, b, tolerance = 0.00008) {
    if (!a || !b) return false;
    let dx = Math.abs(a[0] - b[0]);
    dx = Math.min(dx, Math.abs(dx - 360));
    return dx <= tolerance && Math.abs(a[1] - b[1]) <= tolerance;
  }

  let boundaryEpoch = 0;
  let identitySequence = 0;
  const geometryIdentities = new WeakMap();
  function boundaryRevision() {
    const identity = geometry => {
      if (!geometry) return 0;
      if (!geometryIdentities.has(geometry)) geometryIdentities.set(geometry, ++identitySequence);
      return geometryIdentities.get(geometry);
    };
    return JSON.stringify([dependencies.domains.projectDomain?.getGeneration?.(),
      [...(dependencies.territorialModel.entityRepository.list({ kind: 'general', parentId: '' })), ...dependencies.territorialModel.entityRepository.list({  }).filter(entity => entity.properties.entityKind === 'regional' || !!entity.properties.parentId)].map(feature => [
        String(feature.id), identity(feature.geometry), feature.properties?.parentId, (0, dependencies.territorialModel.territorialRootId)(feature, id => dependencies.territorialModel.entityRepository.get(id)),
        !!feature.properties?.locked,
      ])]);
  }

  function rebuildBoundaryTopology(targetCountryIds = dependencies.projectState.state.coastEditCountryId) {
    const state = dependencies.projectState.state;
    const targetIds = [...new Set((Array.isArray(targetCountryIds) ? targetCountryIds : [targetCountryIds]).filter(id => id != null).map(String))].sort();
    if (!['territorial-border', 'country-coast'].includes(state.tool) || !targetIds.length) return Promise.resolve(false);
    const tool = state.tool;
    const mode = tool === 'country-coast' ? 'coast' : 'border';
    const neighborsOnly = mode === 'border' && state.boundaryEditPhase === 'selecting';
    const scopeId = state.coastEditScopeGenericFeatureId;
    const selectionKey = JSON.stringify(targetIds);
    const revision = boundaryRevision();
    const key = JSON.stringify([mode, targetIds, neighborsOnly, state.boundaryEditAutoSeedId, state.coastEditScopeGenericFeatureId, revision]);
    const previous = state.boundaryPreparation;
    if (previous?.key === key && ['pending', 'ready'].includes(previous.status)) return previous.promise;
    previous?.cancel();
    const epoch = ++boundaryEpoch;
    const controller = new AbortController();
    const current = () => state.boundaryPreparation === preparation && epoch === boundaryEpoch
      && tool === state.tool && revision === boundaryRevision() && !controller.signal.aborted
      && scopeId === state.coastEditScopeGenericFeatureId
      && neighborsOnly === (mode === 'border' && state.boundaryEditPhase === 'selecting')
      && selectionKey === JSON.stringify((mode === 'coast' ? [String(state.coastEditCountryId)] : [...state.boundaryEditEntityIds].map(String)).sort());
    const stale = () => {
      if (state.boundaryPreparation === preparation && tool === state.tool && !controller.signal.aborted) {
        preparation.status = 'error';
        preparation.message = '준비 중 형상이나 소속·잠금이 바뀌었습니다. 다시 시도하세요.';
        refresh();
      }
      return false;
    };
    const refresh = () => {
      dependencies.domains.editingDomain?.refreshTerritorySelection?.({ tool, reason: 'boundary-preparation' });
      dependencies.domains.renderingDomain?.invalidateGpuInteraction?.('boundary-preparation');
      dependencies.taskUi.updateModeButtons();
    };
    const preparation = {
      key, status: 'pending', workerPending: true, result: null, nodes: new Map(), revision,
      cancel() {
        if (controller.signal.aborted) return;
        controller.abort();
        if (preparation.workerPending || preparation.status === 'moving') dependencies.spatialQuery.mapEditClient.stop();
      },
      retry() { void rebuildBoundaryTopology(targetIds); },
      current,
    };
    state.boundaryPreparation = preparation;
    preparation.promise = dependencies.spatialQuery.mapEditClient.execute('boundary-prepare', { payload: {
      targetIds, mode, neighborsOnly, autoSeedId: state.boundaryEditAutoSeedId, projectGeneration: dependencies.domains.projectDomain?.getGeneration?.(), revision,
    } }, { signal: controller.signal, jobKey: 'boundary-prepare' }).then(async ({ result }) => {
      preparation.workerPending = false;
      if (!current()) return stale();
      const scope = state.coastEditScopeGenericFeatureId
        ? state.genericFeatures.find(feature => String(feature.id) === String(state.coastEditScopeGenericFeatureId)) : null;
      if (scope) for (const handle of result.handles) if (!dependencies.territoryGeometry.pointInGenericFeature(handle.coordinate, dependencies.presentation.genericFeatureDisplayFeature(scope))) handle.fixed = true;
      let lastYield = performance.now();
      const checkpoint = async () => {
        if (performance.now() - lastYield >= 8) { await new Promise(resolve => setTimeout(resolve, 0)); lastYield = performance.now(); }
        if (!current()) throw Object.assign(new Error('준비가 취소되었습니다.'), { cancelled: true });
      };
      const nodes = { get: key => result.handles[result.displayIndex?.nodeKeys?.[key]] };
      const packet = await adoptBoundaryRenderPacketAsync(result, checkpoint);
      if (!current()) return stale();
      preparation.result = result;
      preparation.nodes = nodes;
      preparation.packet = packet;
      preparation.status = 'ready';
      if (neighborsOnly && dependencies.taskUi.setModeBanner) dependencies.taskUi.setModeBanner(result.valid
        ? `${result.selectedIds.length}개 국가 선택됨 · 완료하면 공유국경을 편집합니다.`
        : result.selectedIds.length < 2 ? '접경국을 하나 이상 더 선택하세요.' : '선택 국가 사이에 연결된 공유국경이 없습니다.');
      if (!neighborsOnly && !result.valid) {
        preparation.status = 'error';
        preparation.message = '편집 가능한 경계가 없습니다. 대상을 다시 선택하세요.';
      }
      refresh();
      return result;
    }).catch(error => {
      preparation.workerPending = false;
      if (!current()) return stale();
      preparation.status = 'error';
      preparation.message = error.message || '경계 준비에 실패했습니다. 다시 시도하세요.';
      refresh();
      return false;
    });
    refresh();
    return preparation.promise;
  }

  function boundaryEditSelectionAnalysis(countryIds = dependencies.projectState.state.boundaryEditEntityIds) {
    const ids = [...new Set(countryIds.map(String))].sort();
    const preparation = dependencies.projectState.state.boundaryPreparation;
    const result = preparation?.status === 'ready' ? preparation.result : null;
    const matches = result && JSON.stringify(result.selectedIds) === JSON.stringify(ids);
    return {
      selectedIds: ids, valid: !!matches && result.valid, analyzed: !!matches,
      isolatedIds: matches ? result.isolatedIds : [],
      message: preparation?.status === 'error' ? preparation.message : !matches ? '경계를 준비하고 있습니다.'
        : ids.length < 2 ? '접경국을 하나 이상 더 선택하세요.' : result.valid ? '' : '선택 국가 사이에 연결된 공유국경이 없습니다.',
    };
  }

  async function beginWorkerGeometryPreview({
    operation,
    payload,
    snapshot,
    transferredGeometry = null,
    applyResult,
    resolveFeature = id => dependencies.territorialModel.entityRepository.get(id),
    invalidateAfterApply = () => dependencies.domains.renderingDomain?.invalidateCountryPatch?.('country-geometry-preview-applied'),
    onSuccess = () => {},
    onError = () => {},
    shouldKeepResult = () => true,
  }) {
    discardActiveGeometryPreview({ announce: false });
    const baseDataRevision = dependencies.projectState.state.stateRevision;
    (0, dependencies.feedback.setActionStatus)('변경 미리보기 계산 중…', 'working', 0);
    let requestId = 0;
    try {
      const response = await dependencies.spatialQuery.mapEditClient.execute(operation, { ...payload, previewTransferredGeometry: transferredGeometry });
      requestId = response.requestId;
      if (dependencies.projectState.state.stateRevision !== baseDataRevision || !shouldKeepResult()) {
        dependencies.spatialQuery.mapEditClient.discard(requestId);
        throw Object.assign(new Error('계산 중 지도 상태가 바뀌어 미리보기를 폐기했습니다.'), { cancelled: true });
      }
      const result = response.result;
      transferredGeometry = result.transferredGeometry || transferredGeometry;
      const affectedIds = new Set((result.affectedIds || []).map(String));
      const removedIds = new Set((result.removedIds || []).map(String));
      const beforeFeatures = [...affectedIds]
        .map(id => resolveFeature(id))
        .filter(Boolean);
      const patchById = new Map((result.features || []).map(feature => [String(feature?.id || ''), feature]));
      const afterFeatures = [...affectedIds].filter(id => !removedIds.has(id))
        .map(id => patchById.get(id) || resolveFeature(id))
        .filter(Boolean);
      const preview = result.preview;
      if (!preview?.validation) throw new Error('Worker 미리보기 검증 결과를 받지 못했습니다.');
      const validationIssues = preview.validation.issues;
      const session = (0, dependencies.geometryEditingCore.beginGeometryPreview)(dependencies.projectState.state.geometryPreview, {
        operation,
        baseDataRevision,
        workerRequestId: requestId,
        affectedIds: [...affectedIds],
        beforeFeatures,
        afterFeatures,
        removedIds: [...removedIds],
        ...preview,
        validation: {
          issues: validationIssues,
          blocking: validationIssues.some(issue => issue.severity !== 'warning'),
        },
      });
      activeGeometryPreviewDiscard = () => dependencies.spatialQuery.mapEditClient.discard(requestId);
      activeGeometryPreviewApply = async () => {
        if (!shouldKeepResult() || !(0, dependencies.geometryEditingCore.previewIsCurrent)(dependencies.projectState.state.geometryPreview, session.sessionId, baseDataRevision) || dependencies.projectState.state.stateRevision !== baseDataRevision) {
          dependencies.spatialQuery.mapEditClient.discard(requestId);
          (0, dependencies.geometryEditingCore.clearGeometryPreview)(dependencies.projectState.state.geometryPreview);
          activeGeometryPreviewApply = null;
          activeGeometryPreviewDiscard = null;
          dependencies.domains.renderingDomain?.invalidateGpuInteraction?.('geometry-preview-cancelled');
          (0, dependencies.taskUi.updateModeButtons)();
          (0, dependencies.feedback.setActionStatus)('지도가 바뀌어 미리보기를 취소했습니다.', 'error', 3800);
          return false;
        }
        if (session.validation?.blocking) {
          (0, dependencies.feedback.setActionStatus)('미리보기 형상을 수정하세요.', 'error', 3400);
          return false;
        }
        (0, dependencies.geometryEditingCore.clearGeometryPreview)(dependencies.projectState.state.geometryPreview);
        activeGeometryPreviewApply = null;
        activeGeometryPreviewDiscard = null;
        try {
          await applyResult(result);
          assertCurrentProjectReferences();
          dependencies.spatialQuery.mapEditClient.commit(requestId);
          dependencies.domains.projectDomain.commitHistorySnapshot(snapshot);
          dependencies.domains.projectDomain.queueAutosave();
          onSuccess(result);
          invalidateAfterApply(result);
          (0, dependencies.taskUi.updateModeButtons)();
          return true;
        } catch (error) {
          dependencies.spatialQuery.mapEditClient.discard(requestId);
          (0, dependencies.validation.restoreEditTransactionSnapshot)(snapshot);
          onError(error);
          return false;
        }
      };
      dependencies.domains.renderingDomain?.invalidateGpuInteraction?.('geometry-preview-ready');
      (0, dependencies.taskUi.updateModeButtons)();
      const blockingIssue = validationIssues.find(issue => issue.severity !== 'warning');
      const included = result.autoIncludedSlivers;
      const inclusionNote = included?.count
        ? ` 미세 잔여 영역 ${included.count}개 포함 · ${included.areaM2 < 0.01 ? '0.01m² 미만' : `${included.areaM2.toFixed(2)}m²`}.` : '';
      (0, dependencies.taskUi.setModeBanner)(blockingIssue?.message || `변경 결과를 확인한 뒤 적용하세요.${inclusionNote}`);
      if (blockingIssue) (0, dependencies.platform.$)('modeTaskInstruction')?.classList.add('cut-invalid');
      (0, dependencies.feedback.setActionStatus)(validationIssues.length
        ? `미리보기에서 geometry 문제 ${validationIssues.length}건을 찾았습니다.`
        : '변경 결과 미리보기를 준비했습니다.', validationIssues.length ? 'error' : 'success', 3600);
      return true;
    } catch (error) {
      if (requestId) dependencies.spatialQuery.mapEditClient.discard(requestId);
      if (!error?.cancelled) onError(error);
      else (0, dependencies.feedback.setActionStatus)('지도 작업을 취소했습니다.', 'success', 2200);
      return false;
    }
  }

  async function beginLocalGeometryPreview({
    operation,
    beforeFeatures = [],
    afterFeatures = [],
    removedIds = [],
    transferredGeometry = null,
    snapshot = (0, dependencies.snapshots.snapshotEditable)(),
    applyResult,
    shouldKeepResult = () => true,
    commitHistorySnapshot = false,
    beforeApply = async () => true,
    preparedPreview = null,
    validatePrepared = async () => true,
    invalidateAfterApply = () => dependencies.domains.renderingDomain?.invalidateGenericPatch?.('local-geometry-preview-applied'),
    successMessage = '변경을 적용했습니다.',
    errorMessage = '변경을 적용하지 못했습니다.',
  }) {
    discardActiveGeometryPreview({ announce: false });
    if (!shouldKeepResult()) return false;
    const baseDataRevision = dependencies.projectState.state.stateRevision;
    const epoch = ++localPreparationEpoch;
    if (!preparedPreview) {
      localPreparationPending = true;
      try {
        const response = await dependencies.spatialQuery.mapEditClient.execute('territorial-preview', { payload: {
          operation, beforeIds: beforeFeatures.map(feature => String(feature.id)), afterFeatures, removedIds, transferredGeometry,
        } });
        if (epoch !== localPreparationEpoch || !shouldKeepResult() || dependencies.projectState.state.stateRevision !== baseDataRevision) return false;
        preparedPreview = response.result;
        validatePrepared = async () => {
          await dependencies.spatialQuery.mapEditClient.execute('territorial-validation', { payload: { preparationId: preparedPreview.preparationId } });
          return dependencies.spatialQuery.mapEditClient.sourcesCurrent(response.sourceRevision);
        };
      } catch (error) {
        if (!error?.cancelled) (0, dependencies.feedback.reportOperationError)(error, errorMessage, 'PL-PREVIEW-PREPARE', 3600);
        return false;
      } finally {
        if (epoch === localPreparationEpoch) localPreparationPending = false;
      }
    }
    const issues = preparedPreview.validation?.issues || [];
    const preview = preparedPreview;
    const session = (0, dependencies.geometryEditingCore.beginGeometryPreview)(dependencies.projectState.state.geometryPreview, {
      operation,
      baseDataRevision,
      affectedIds: [...new Set([...beforeFeatures, ...afterFeatures].map(feature => String(feature?.id || '')).filter(Boolean))],
      beforeFeatures,
      afterFeatures,
      removedIds,
      ...preview,
      validation: { issues, blocking: issues.some(issue => issue.severity !== 'warning') },
    });
    activeGeometryPreviewDiscard = null;
    activeGeometryPreviewApply = async () => {
      if (!shouldKeepResult() || !(0, dependencies.geometryEditingCore.previewIsCurrent)(dependencies.projectState.state.geometryPreview, session.sessionId, baseDataRevision) || dependencies.projectState.state.stateRevision !== baseDataRevision) {
        (0, dependencies.geometryEditingCore.clearGeometryPreview)(dependencies.projectState.state.geometryPreview);
        activeGeometryPreviewApply = null;
        dependencies.domains.renderingDomain?.invalidateGpuInteraction?.('local-geometry-preview-cancelled');
        (0, dependencies.taskUi.updateModeButtons)();
        (0, dependencies.feedback.setActionStatus)('지도가 바뀌어 미리보기를 취소했습니다.', 'error', 3600);
        return false;
      }
      if (session.validation?.blocking) {
        (0, dependencies.feedback.setActionStatus)('미리보기 형상을 수정하세요.', 'error', 3400);
        return false;
      }
      if (!await beforeApply()) return false;
      try { if (!await validatePrepared()) return false; }
      catch (error) {
        (0, dependencies.feedback.reportOperationError)(error, '미리보기를 다시 계산하세요.', 'PL-PREVIEW-STALE', 3600);
        return false;
      }
      if (!shouldKeepResult() || dependencies.projectState.state.stateRevision !== baseDataRevision
        || !(0, dependencies.geometryEditingCore.previewIsCurrent)(dependencies.projectState.state.geometryPreview, session.sessionId, baseDataRevision)) return false;
      (0, dependencies.geometryEditingCore.clearGeometryPreview)(dependencies.projectState.state.geometryPreview);
      activeGeometryPreviewApply = null;
      activeGeometryPreviewDiscard = null;
      try {
        applyResult();
        assertCurrentProjectReferences();
        if (commitHistorySnapshot) dependencies.domains.projectDomain.commitHistorySnapshot(snapshot);
        dependencies.projectState.state.stateRevision += 1;
        dependencies.domains.projectDomain.queueAutosave();
        invalidateAfterApply();
        (0, dependencies.taskUi.updateModeButtons)();
        (0, dependencies.feedback.setActionStatus)(successMessage, 'success', 3600);
        return true;
      } catch (error) {
        (0, dependencies.validation.restoreEditTransactionSnapshot)(snapshot);
        (0, dependencies.feedback.reportOperationError)(error, errorMessage, 'PL-PREVIEW-001', 4400);
        return false;
      }
    };
    dependencies.domains.editingDomain?.refreshDraftPresentation?.('draft-preview-ready');
    dependencies.domains.renderingDomain?.invalidateGpuInteraction?.('local-geometry-preview-ready');
    (0, dependencies.taskUi.updateModeButtons)();
    const blockingIssue = issues.find(issue => issue.severity !== 'warning');
    (0, dependencies.taskUi.setModeBanner)(blockingIssue?.message || '변경 결과를 확인한 뒤 적용하세요.');
    if (blockingIssue) (0, dependencies.platform.$)('modeTaskInstruction')?.classList.add('cut-invalid');
    return true;
  }

  async function applyActiveGeometryPreview() {
    if (!activeGeometryPreviewApply) return false;
    return activeGeometryPreviewApply();
  }

  function discardActiveGeometryPreview({ announce = true } = {}) {
    localPreparationEpoch += 1;
    if (localPreparationPending) {
      localPreparationPending = false;
      dependencies.spatialQuery.mapEditClient.stop();
    }
    if (!dependencies.projectState.state.geometryPreview.session) return false;
    activeGeometryPreviewDiscard?.();
    activeGeometryPreviewApply = null;
    activeGeometryPreviewDiscard = null;
    (0, dependencies.geometryEditingCore.clearGeometryPreview)(dependencies.projectState.state.geometryPreview);
    dependencies.domains.editingDomain?.refreshDraftPresentation?.('draft-preview-discard');
    dependencies.domains.renderingDomain?.invalidateGpuInteraction?.('geometry-preview-discard');
    (0, dependencies.taskUi.updateModeButtons)();
    if (announce) (0, dependencies.feedback.setActionStatus)('미리보기를 닫았습니다.', 'success', 2600);
    return true;
  }

  function initializeEditPreviewController() {
    (editPreviewController = (0, dependencies.spatialFactories.createEditPreviewController)());

    (editPipelineMetrics = {
      commitCount: 0,
      rollbackCount: 0,
      lastCommitMs: 0,
      lastCommitDomain: '',
      commitP95Ms: 0,
      commitP99Ms: 0,
    });
  }

  function initializeBoundarySelectionAnalysisCache() {
    (boundarySelectionAnalysisCache = new Map());

    (boundarySelectionAnalysisMetrics = { builds: 0, cacheHits: 0, cacheMisses: 0, buildMs: 0 });
  }

  function initializeActiveGeometryPreviewApply() {
    (activeGeometryPreviewApply = null);

    (activeGeometryPreviewDiscard = null);
  }

  return Object.freeze({
    connect,
    initializeEditPreviewController,
    initializeBoundarySelectionAnalysisCache,
    initializeActiveGeometryPreviewApply,
    get applyActiveGeometryPreview() { return applyActiveGeometryPreview; },
    get assertCurrentProjectReferences() { return assertCurrentProjectReferences; },
    get beginLocalGeometryPreview() { return beginLocalGeometryPreview; },
    get beginWorkerGeometryPreview() { return beginWorkerGeometryPreview; },
    get boundaryEditSelectionAnalysis() { return boundaryEditSelectionAnalysis; },
    get boundarySelectionAnalysisCache() { return boundarySelectionAnalysisCache; },
    get boundarySelectionAnalysisMetrics() { return boundarySelectionAnalysisMetrics; },
    get coordKey() { return coordKey; },
    get coordNear() { return coordNear; },

    get discardActiveGeometryPreview() { return discardActiveGeometryPreview; },
    get editPipelineMetrics() { return editPipelineMetrics; },
    get editPreviewController() { return editPreviewController; },
    get geometryPolygonSets() { return geometryPolygonSets; },

    get rebuildBoundaryTopology() { return rebuildBoundaryTopology; },

    get transactMapEdit() { return transactMapEdit; },
  });
}
