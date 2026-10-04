/** CountryCommits: extracted application responsibility.
 * Dependencies are explicitly wired once by the composition modules.
 * Mutable bindings stay local; exported accessors retain live identity.
 */
export function createCountryCommits() {
  let dependencies;
  let multiDraftPreviewTimer = null;
  let multiDraftPreviewGeneration = 0;

  function connect(ports) {
    if (dependencies) throw new Error('country-commits already connected');
    dependencies = ports;
  }

  function applyTerritorialGeometryPlan(plan, {
    patchOptions = undefined,
    afterPatch = () => {},
    updateDependents = () => {},
    centroidIds = plan?.affectedIds || [],
    requireCountryId = '',
    clearDraftReason = '',
    clearMultiDraft = false,
    selectedId = '',
  } = {}) {
    dependencies.territorialModel.entityStore.transaction(() => {
      (0, dependencies.cutOperations.applyWorkerCountryPatches)(plan, patchOptions);
      afterPatch(plan);
      updateDependents(plan);
    });
    (0, dependencies.countryValidation.refreshCountryCentroids)(new Set([...(centroidIds || [])].map(String)));
    dependencies.projectState.state.boundaryPreparation?.cancel();
    dependencies.projectState.state.boundaryPreparation = null;
    if (requireCountryId && !(dependencies.territorialModel.entityRepository.get(requireCountryId)?.properties?.entityKind === 'general' && !dependencies.territorialModel.entityRepository.get(requireCountryId)?.properties?.parentId)) {
      throw new Error('국가 geometry 적용 결과에서 대상 국가가 사라졌습니다.');
    }
    if (clearMultiDraft) dependencies.projectState.state.multiDraft = null;
    if (clearDraftReason) dependencies.domains.editingDomain?.clearDraft?.({ reason: clearDraftReason, render: false });
    dependencies.domains.editingDomain?.setTool('select', { announce: false });
    if (selectedId) dependencies.domains.selectionUiController.applyIntent({ domain: 'territorial', type: 'entity', id: String(selectedId) }, { refreshOnly: false, openEditor: false });
  }

  function cancelScheduledMultiDraftPreview() {
    multiDraftPreviewGeneration += 1;
    if (multiDraftPreviewTimer !== null) {
      clearTimeout(multiDraftPreviewTimer);
      multiDraftPreviewTimer = null;
    }
    const draft = multiDraft();
    if (draft && draft.kind !== 'new-country') draft.previewPending = false;
  }

  function scheduleMultiDraftPreview({ delay = 300 } = {}) {
    const draft = multiDraft();
    if (!draft || draft.kind === 'new-country') return false;
    cancelScheduledMultiDraftPreview();
    const generation = multiDraftPreviewGeneration;
    draft.previewPending = true;
    draft.previewIssues = [];
    (0, dependencies.taskUi.updateModeButtons)();
    multiDraftPreviewTimer = setTimeout(() => {
      multiDraftPreviewTimer = null;
      if (generation !== multiDraftPreviewGeneration || dependencies.projectState.state.multiDraft !== draft) return;
      const geometry = composeMultiDraftGeometry({ polygon: draft.shape === 'polygon' });
      const feature = geometry && draft.shape === 'polygon'
        ? { type: 'Feature', id: 'multi-draft-preview', properties: {}, geometry }
        : null;
      draft.previewGeometry = geometry;
      const issues = feature ? (0, dependencies.geometryModel.validateStructuredGeometry)(feature) : [{ message: '그린 조각을 하나의 형상으로 만들 수 없습니다.', severity: 'error' }];
      draft.previewIssues = issues.filter(issue => issue.severity !== 'warning');
      draft.previewPending = false;
      dependencies.domains.renderingDomain?.invalidateEditingOverlays?.('multi-draft-preview-ready');
      (0, dependencies.taskUi.updateModeButtons)();
    }, Math.max(0, Number(delay) || 0));
    return true;
  }

  function redrawCurrentDraft() {
    const state = dependencies.projectState.state;
    const draft = dependencies.draftPresentation.editingDraftSnapshot();
    if (state.modeProcessing || draft.strokeActive || draft.dragging) return false;
    if (state.territorySelectionSession) return (0, dependencies.territorySelectionA.redrawTerritorySelection)();
    const multi = state.multiDraft;
    const multiDraft = !!multi && ['hydro', 'territorial-direct', 'territorial-split-create'].includes(multi.kind);
    if (!multiDraft && !dependencies.surfaces.isGenericFeatureDraftTool(state.tool)) return false;
    dependencies.geometryOperations.discardActiveGeometryPreview?.({ announce: false });
    cancelScheduledMultiDraftPreview();
    if (multi?.current) multi.current = null;
    if (multi) {
      multi.previewGeometry = null;
      multi.previewIssues = [];
    }
    dependencies.domains.editingDomain?.startDraft?.({ coords: [] });
    dependencies.taskUi.setModeBanner(dependencies.interactionPresentation.defaultDraftInstruction());
    dependencies.domains.renderingDomain?.invalidateEditingOverlays?.('current-draft-redrawn');
    dependencies.taskUi.updateModeButtons();
    return true;
  }

  function multiDraft() {
    const draft = dependencies.projectState.state.multiDraft;
    return draft && Array.isArray(draft.parts) ? draft : null;
  }

  function multiDraftPartCount({ includeCurrent = true } = {}) {
    const draft = multiDraft();
    return draft ? draft.parts.length + (includeCurrent && draft.current ? 1 : 0) : 0;
  }

  function composeMultiDraftGeometry({ polygon = false } = {}) {
    const draft = multiDraft();
    const pieces = [...(draft?.parts || []), ...(draft?.current ? [draft.current] : [])].filter(item => item?.geometry);
    if (!pieces.length) return null;
    if (!polygon) {
      const lines = pieces.flatMap(item => item.geometry.type === 'MultiLineString'
        ? item.geometry.coordinates : [item.geometry.coordinates]);
      return lines.length === 1
        ? { type: 'LineString', coordinates: lines[0].map(coord => coord.slice()) }
        : { type: 'MultiLineString', coordinates: lines.map(line => line.map(coord => coord.slice())) };
    }
    try {
      const coordinates = pieces.flatMap(item => (0, dependencies.territoryGeometry.geometryMultiCoordinates)(item.geometry));
      const union = window.polygonClipping?.union ? window.polygonClipping.union(...coordinates) : coordinates;
      return (0, dependencies.cutGeometry.normalizeClippedLandGeometry)(union);
    } catch (error) {
      (0, dependencies.feedback.reportOperationError)(error, '여러 영역을 하나로 준비하지 못했습니다. 겹치는 경계를 확인하세요.', 'PL-MULTI-DRAFT-001', 3800);
      return null;
    }
  }

  function startNextMultiDraftPart() {
    dependencies.domains.editingDomain?.startDraft?.({ coords: [] });
    (0, dependencies.taskUi.setModeBanner)((0, dependencies.interactionPresentation.defaultDraftInstruction)());
    dependencies.domains.renderingDomain?.invalidateEditingOverlays?.('multi-draft-next-part');
    (0, dependencies.taskUi.updateModeButtons)();
  }

  function addMultiDraftPart() {
    const draft = multiDraft();
    const snapshot = dependencies.draftPresentation.editingDraftSnapshot();
    if (!draft || dependencies.projectState.state.modeProcessing || snapshot.strokeActive || !draft.current) return false;
    draft.parts.push(draft.current);
    draft.current = null;
    scheduleMultiDraftPreview();
    startNextMultiDraftPart();
    return true;
  }

  function undoMultiDraftPart() {
    const draft = multiDraft();
    const snapshot = dependencies.draftPresentation.editingDraftSnapshot();
    if (!draft || dependencies.projectState.state.modeProcessing || snapshot.strokeActive) return false;
    if (!draft.current && !draft.parts.length) return false;
    cancelScheduledMultiDraftPreview();
    draft.current = null;
    if (!draft.parts.length) {
      draft.previewGeometry = null;
      draft.previewIssues = [];
      startNextMultiDraftPart();
      return true;
    }
    draft.parts.pop();
    scheduleMultiDraftPreview();
    startNextMultiDraftPart();
    return true;
  }

  function finishHydroDraft(geometry, hydro) {
    const draft = multiDraft();
    if (!draft || draft.kind !== 'hydro') return false;
    draft.current = { geometry };
    scheduleMultiDraftPreview();
    dependencies.domains.editingDomain?.clearDraft?.({ reason: 'hydro-draft-part-finished', render: false });
    (0, dependencies.taskUi.setModeBanner)('그린 경로를 확인하세요.');
    dependencies.domains.renderingDomain?.invalidateEditingOverlays?.('hydro-draft-part-finished');
    (0, dependencies.taskUi.updateModeButtons)();
    return true;
  }

  function completeHydroMultiDraft() {
    const draft = multiDraft();
    const hydro = (0, dependencies.draftPresentation.hydroToolConfig)(dependencies.projectState.state.tool);
    if (!draft || draft.kind !== 'hydro' || !hydro || dependencies.domains.editingDomain?.draftInputActive?.() || draft.previewPending || draft.previewIssues?.length) return false;
    const geometry = draft.previewGeometry || composeMultiDraftGeometry({ polygon: hydro.category === 'lake' });
    if (!geometry) return false;
    const feature = {
      type: 'Feature', id: (0, dependencies.surfaces.uid)(hydro.prefix), geometry,
      properties: { name: '', editorColor: hydro.color, category: hydro.category, notes: '' },
    };
    try {
      dependencies.domains.projectDomain.recordHistory({ type: 'hydro-create', affectedIds: [String(feature.id)] });
      (0, dependencies.hydroModel.normalizeHydroEdit)(feature);
      dependencies.projectState.state.hydroEdits.push(feature);
      dependencies.projectState.state.multiDraft = null;
      dependencies.domains.editingDomain?.clearDraft?.({ reason: 'hydro-draft-committed', render: false });
      dependencies.domains.editingDomain?.setTool('select', { announce: false });
      (0, dependencies.layers.markLayerTreeDirty)();
      (0, dependencies.propertyEditingA.applyHydroSelectionIntent)(String(feature.id));
      dependencies.domains.renderingDomain?.invalidateGpuInteraction?.('finish-hydro-multi-draft');
      dependencies.domains.projectDomain.queueAutosave();
      (0, dependencies.feedback.setActionStatus)(`${hydro.category === 'river' ? '강을' : '호수를'} 추가했습니다.`, 'success');
      return true;
    } catch (error) {
      (0, dependencies.feedback.reportOperationError)(error, '그린 객체를 생성하지 못했습니다.', 'PL-MULTI-DRAFT-002', 3800);
      return false;
    }
  }

  function completeMultiDraftCreation() {
    const draft = multiDraft();
    if (!draft) return false;
    if (draft.kind === 'hydro') return completeHydroMultiDraft();
    return false;
  }

  function prepareAnnexDraftCandidates(session = dependencies.projectState.state.territorySelectionSession) {
    const targetId = String(session?.targetCountryId || '');
    const target = dependencies.territorialModel.entityRepository.get(targetId);
    if (session?.kind !== 'annex' || session.activePhase !== 'drawing' || session.activeMethod !== 'line' || !target || !session.sourceCountryIds.length) {
      (0, dependencies.feedback.setActionStatus)('편입을 진행할 수 없습니다. 편입받을 국가와 영토를 가져올 국가를 먼저 선택하세요.', 'error', 3800);
      return;
    }
    try {
      const sourceGeometry = session.workingSourceGeometry;
      const split = (0, dependencies.cutOperations.buildCutSplitCandidates)(sourceGeometry, (0, dependencies.countryEditingA.editingDraftCoordinates)());
      dependencies.domains.editingDomain?.replaceDraftCoordinates?.(split.line, { record: false, inputPhase: 'refine' });
      (0, dependencies.territorySelectionA.setTerritorySelectionCandidates)(split.candidates);
      dependencies.domains.editingDomain?.refreshTerritorySelection?.({ tool: session.tool, reason: 'annex-candidates-ready' });
      (0, dependencies.taskUi.setModeBanner)('가져올 영역을 선택하세요.', 'annex-mode');
      (0, dependencies.taskUi.updateModeButtons)();
      dependencies.domains.renderingDomain?.invalidateGpuInteraction?.('annex-candidates-ready');
      return true;
    } catch (error) {
      (0, dependencies.feedback.reportOperationError)(error, '새 경계를 사용할 수 없습니다. 표시된 문제 지점을 확인하고 선을 수정하세요.', 'PL-ANNEX-003');
    }
  }

  function prepareNewCountryDraftCandidates(session = dependencies.projectState.state.territorySelectionSession) {
    if (session?.kind !== 'entity' || session.entityKind !== 'general' || !!session.parentId || session.activePhase !== 'drawing' || session.activeMethod !== 'line') {
      (0, dependencies.feedback.setActionStatus)('새 국가를 만들 수 없습니다. 영토를 가져올 국가 선택을 먼저 완료하세요.', 'error', 3600);
      return;
    }
    try {
      const sourceGeometry = session.workingSourceGeometry;
      const split = (0, dependencies.cutOperations.buildCutSplitCandidates)(sourceGeometry, (0, dependencies.countryEditingA.editingDraftCoordinates)());
      dependencies.domains.editingDomain?.replaceDraftCoordinates?.(split.line, { record: false, inputPhase: 'refine' });
      (0, dependencies.territorySelectionA.setTerritorySelectionCandidates)(split.candidates);
      dependencies.domains.editingDomain?.refreshTerritorySelection?.({ tool: session.tool, reason: 'new-country-candidates-ready' });
      (0, dependencies.taskUi.setModeBanner)('신생국으로 만들 영역을 선택하세요.', 'add-country-mode');
      (0, dependencies.taskUi.updateModeButtons)();
      dependencies.domains.renderingDomain?.invalidateGpuInteraction?.('new-country-candidates-ready');
      return true;
    } catch (error) {
      (0, dependencies.feedback.reportOperationError)(error, '신생국 국경선을 사용할 수 없습니다. 표시된 문제 지점을 확인하고 선을 수정하세요.', 'PL-COUNTRY-003');
    }
  }

  function prepareAnnexPolygon(session = dependencies.projectState.state.territorySelectionSession) {
    const target = dependencies.territorialModel.entityRepository.get(String(session?.targetCountryId || ''));
    const donors = (session?.sourceCountryIds || []).map(dependencies.territorialModel.entityRepository.get).filter(Boolean);
    if (session?.kind !== 'annex' || session.activePhase !== 'drawing' || session.activeMethod !== 'polygon' || !target || !donors.length) return;
    const plan = (0, dependencies.territorialServicesA.planDrawnTerritoryAnnex)({
      drawnGeometry: { type: 'Polygon', coordinates: [(0, dependencies.geometryModel.ensureClosedRing)((0, dependencies.countryEditingA.editingDraftCoordinates)())] },
      donorFeatures: [{ geometry: session.workingSourceGeometry }],
      targetFeature: target,
      clipper: window.polygonClipping,
    });
    if (!plan?.transferGeometry) {
      (0, dependencies.feedback.setActionStatus)('그린 영역 안에 편입할 영토가 없습니다.', 'error', 3200);
      return;
    }
    (0, dependencies.territorySelectionA.setTerritorySelectionCandidates)([{ geometry: plan.transferGeometry }]);
    dependencies.domains.editingDomain?.refreshTerritorySelection?.({ tool: session.tool, reason: 'annex-polygon-ready' });
    (0, dependencies.taskUi.setModeBanner)('가져올 영역을 선택하세요.', 'annex-mode');
    (0, dependencies.taskUi.updateModeButtons)();
    dependencies.domains.renderingDomain?.invalidateGpuInteraction?.('annex-polygon-ready');
    return true;
  }

  function prepareNewCountryPolygon(session = dependencies.projectState.state.territorySelectionSession) {
    if (session?.kind !== 'entity' || session.entityKind !== 'general' || !!session.parentId || session.activePhase !== 'drawing' || session.activeMethod !== 'polygon') return false;
    try {
      const drawn = { type: 'Polygon', coordinates: [(0, dependencies.geometryModel.ensureClosedRing)((0, dependencies.countryEditingA.editingDraftCoordinates)())] };
      const geometry = (0, dependencies.cutGeometry.normalizeClippedLandGeometry)(window.polygonClipping.intersection(
        (0, dependencies.territoryGeometry.geometryMultiCoordinates)(session.workingSourceGeometry),
        (0, dependencies.territoryGeometry.geometryMultiCoordinates)(drawn),
      ));
      if (!geometry) throw new Error('그린 영역 안에 새 국가로 만들 영토가 없습니다.');
      return (0, dependencies.territorySelectionA.setTerritorySelectionCandidates)([{ geometry }]);
    } catch (error) {
      (0, dependencies.feedback.reportOperationError)(error, '그린 영역을 새 국가 후보로 만들 수 없습니다.', 'PL-COUNTRY-003', 3800);
      return false;
    }
  }

  function finishAnnexSelectionDraft(session) {
    return session?.activeMethod === 'polygon' ? prepareAnnexPolygon(session) : prepareAnnexDraftCandidates(session);
  }

  function finishNewCountrySelectionDraft(session) {
    return session?.activeMethod === 'polygon' ? prepareNewCountryPolygon(session) : prepareNewCountryDraftCandidates(session);
  }

  function finishGenericFeatureDraft(polygonMode) {
    const draftCoords = (0, dependencies.countryEditingA.editingDraftCoordinates)();
    const hydro = (0, dependencies.draftPresentation.hydroToolConfig)(dependencies.projectState.state.tool);
    const id = (0, dependencies.surfaces.uid)(hydro?.prefix || (polygonMode ? 'poly' : 'line'));
    const geometry = polygonMode
      ? { type: 'Polygon', coordinates: [(0, dependencies.applicationServicesB.orientRing)(draftCoords, true)] }
      : { type: 'LineString', coordinates: draftCoords.map(coord => coord.slice()) };
    if (polygonMode) {
      const issues = (0, dependencies.geometryModel.validateStructuredGeometry)({ type: 'Feature', id, properties: {}, geometry });
      if (issues.length) {
        (0, dependencies.feedback.setActionStatus)(issues[0].message || '그린 영역을 저장할 수 없습니다. 표시된 경계를 수정하세요.', 'error', 4200);
        (0, dependencies.taskUi.updateModeButtons)();
        return;
      }
    }
    if (polygonMode && dependencies.projectState.state.distributionDraft) {
      const draft = dependencies.projectState.state.distributionDraft;
      const layer = (0, dependencies.propertyEditingA.distributionLayerById)(draft.layerId);
      if (!layer) {
        (0, dependencies.feedback.setActionStatus)('분포 항목을 찾을 수 없습니다. 그린 영역은 유지했으니 분포 항목을 확인하세요.', 'error', 3600);
        return;
      }
      const result = dependencies.objectModelA.distributionService.addEntry({
        id: (0, dependencies.surfaces.uid)('distribution_entry'),
        layerId: layer.id,
        mode: dependencies.territorialModel.DISTRIBUTION_MODES.GEOMETRY,
        geometry,
        value: draft.value,
      });
      if (!result.ok) {
        (0, dependencies.feedback.setActionStatus)(result.error?.message || '자유 분포 영역을 저장하지 못했습니다.', 'error', 3600);
        return;
      }
      dependencies.projectState.state.distributionDraft = null;
      dependencies.domains.editingDomain?.clearDraft?.({ reason: 'distribution-draft-committed', render: false });
      dependencies.domains.editingDomain?.setTool('select', { announce: false });
      (0, dependencies.propertyEditingA.applyDistributionSelectionIntent)(layer.id);
      (0, dependencies.feedback.setActionStatus)(`${layer.name} 자유 분포 영역을 추가했습니다.`, 'success');
      return;
    }
    if (!hydro) {
      (0, dependencies.feedback.setActionStatus)('기타 객체는 새로 만들 수 없습니다. 필요한 종류를 선택해 생성하세요.', 'error', 3600);
      return false;
    }
    return finishHydroDraft(geometry, hydro);
  }

  function finishDraft() {
    const territorySession = dependencies.projectState.state.territorySelectionSession;
    if (!((0, dependencies.surfaces.isGenericFeatureDraftTool)(dependencies.projectState.state.tool) || territorySession)) {
      (0, dependencies.feedback.setActionStatus)('완료할 형상이 없습니다. 지도에서 점을 먼저 입력하세요.', 'error');
      return;
    }
    const polygonMode = (0, dependencies.surfaces.isPolygonDraftTool)(dependencies.projectState.state.tool);
    const minimumPoints = polygonMode ? 3 : 2;
    const draft = (0, dependencies.draftPresentation.editingDraftSnapshot)();
    const accumulatedMultiDraft = dependencies.projectState.state.multiDraft?.kind === 'hydro'
      && dependencies.projectState.state.multiDraft.parts.length > 0 && !draft.coords.length && !draft.strokeActive;
    if (accumulatedMultiDraft) {
      dependencies.domains.editingDomain?.clearDraft?.({ reason: 'multi-draft-review', render: false });
      (0, dependencies.taskUi.setModeBanner)('그린 영역을 확인하세요.');
      dependencies.domains.renderingDomain?.invalidateEditingOverlays?.('multi-draft-review');
      scheduleMultiDraftPreview();
      (0, dependencies.taskUi.updateModeButtons)();
      return true;
    }
    if (draft.coords.length < minimumPoints) {
      (0, dependencies.feedback.setActionStatus)(`완료하려면 점이 최소 ${minimumPoints}개 필요합니다. 지도에서 점을 더 입력하세요.`, 'error');
      return;
    }
    if (draft.strokeActive) {
      (0, dependencies.feedback.setActionStatus)('선을 그리는 중입니다. 포인터를 놓은 뒤 완료하세요.', 'error', 2400);
      return;
    }
    const cutSourceGeometry = (0, dependencies.draftPresentation.activeCutDraftSourceGeometry)();
    if (!cutSourceGeometry && draft.issues.length) {
      (0, dependencies.feedback.setActionStatus)(draft.issues[0].message || '그린 형상에서 수정이 필요한 위치를 확인하세요.', 'error', 4200);
      (0, dependencies.taskUi.updateModeButtons)();
      return;
    }
    if (cutSourceGeometry) {
      const assessment = (0, dependencies.cutOperations.assessCutDraft)(draft.coords, cutSourceGeometry);
      if (!assessment.valid) {
        (0, dependencies.feedback.setActionStatus)(assessment.message || '경계선을 선택 영역의 반대쪽 경계까지 연결하세요.', 'error', 4200);
        (0, dependencies.taskUi.updateModeButtons)();
        return;
      }
    }
    dependencies.domains.editingDomain?.setDraftVertexInsertMode?.(false);
    if (territorySession) return (0, dependencies.territorySelectionA.finishTerritorySelectionDraft)();
    return (0, dependencies.toolServices.dispatchTool)(dependencies.projectState.state.tool, {
      'split-generic-feature': dependencies.genericEditingA.finishSplitGenericFeatureDraft,
      'split-territorial-unit': dependencies.territorialEditingA.finishTerritorialUnitSplitDraft,
      'redraw-territorial-unit': dependencies.territorialEditingA.finishTerritorialUnitRedrawDraft,
    }, () => finishGenericFeatureDraft(polygonMode));
  }

  async function prepareAnnexSelectionPreview(session, expectedKey) {
    if (session?.kind !== 'annex' || session.stage !== 'selection'
      || !(['candidate', 'components', 'result'].includes(session.activePhase) || session.parts?.length)) return false;
    const targetId = String(session.targetCountryId || '');
    const activeComponentItems = session.activePhase === 'components'
      ? (0, dependencies.territoryComponents.territoryComponentItems)().filter(item => item.selected)
      : [];
    const archivedComponents = (session.parts || []).filter(part => part.method === 'components' && part.component).map(part => part.component);
    const selectedComponents = [...archivedComponents, ...activeComponentItems];
    const snapshots = new Map((session.componentSnapshots || []).map(snapshot => [snapshot.id, snapshot.items || []]));
    const groups = new Map();
    const liveItems = session.activePhase === 'components' ? dependencies.territoryComponents.territoryComponentItems() : [];
    for (const item of selectedComponents) {
      const polygonIndex = item.sourcePolygonIndex ?? item.polygonIndex;
      if (!item.usesRiverBoundary) continue;
      const groupKey = `${item.countryId}:${polygonIndex}`;
      if (!groups.has(groupKey)) groups.set(groupKey, { donorId: item.countryId, polygonIndex, geometries: new Set() });
      const snapshotItems = item.snapshotId ? snapshots.get(item.snapshotId) || [] : liveItems;
      for (const other of snapshotItems) {
        if (other.countryId === item.countryId && (other.sourcePolygonIndex ?? other.polygonIndex) === polygonIndex
          && other.key !== item.key) groups.get(groupKey).geometries.add(other.geometry);
      }
    }
    let riverSliverContext = [];
    if (groups.size) {
      session.workerRequests = (session.workerRequests || 0) + 1;
      session.operationAbort ||= new AbortController();
      let response;
      try { response = await dependencies.spatialQuery.mapEditClient.execute('territory-slivers', { payload: {
        groups: [...groups.values()].map(group => ({ ...group, geometries: [...group.geometries] })),
        combinedGeometry: session.combinedGeometry,
      } }, { jobKey: `${session.id}:territory-slivers`, signal: session.operationAbort.signal });
      } finally { session.workerRequests -= 1; }
      if (dependencies.projectState.state.territorySelectionSession !== session
        || expectedKey !== [session.id, session.projectGeneration, session.settingsRevision, session.sourceRevision, session.selectionRevision].join(':')) return false;
      riverSliverContext = response.result;
    }
    const donorIds = [...new Set([
      ...session.sourceCountryIds.map(String),
      ...selectedComponents.map(item => String(item.countryId)).filter(Boolean),
    ])];
    if (!(0, dependencies.objectOperationsB.requireObjectsUnlocked)([targetId, ...donorIds].map(id => ({ domain: 'territorial', type: 'entity', id: String(id) })), '영토를 편입')) return;
    const candidate = { geometry: session.combinedGeometry };
    const targetBefore = dependencies.territorialModel.entityRepository.get(targetId);
    const donorsBefore = donorIds.map(dependencies.territorialModel.entityRepository.get).filter(Boolean);
    if (!candidate?.geometry || !targetBefore || !donorsBefore.length) {
      (0, dependencies.feedback.setActionStatus)('편입 후보나 국가 데이터를 찾을 수 없습니다.', 'error', 3800);
      return;
    }
    const targetName = (0, dependencies.objectPresentation.territorialEntityName)(targetBefore);
    const snapshot = (0, dependencies.snapshots.snapshotEditable)();
    await (0, dependencies.geometryOperations.beginWorkerGeometryPreview)({
      operation: 'annex',
      payload: { targetId, donorIds, transferredGeometry: candidate.geometry, riverSliverContext },
      snapshot,
      transferredGeometry: candidate.geometry,
      applyResult: plan => applyTerritorialGeometryPlan(plan, {
        updateDependents: currentPlan => (0, dependencies.landRelations.transferLandDependents)(
          currentPlan.transferredGeometry || candidate.geometry,
          donorIds,
          targetId,
        ),
        requireCountryId: targetId,
        clearDraftReason: 'annex-committed',
        selectedId: targetId,
      }),
      invalidateAfterApply: () => {
        dependencies.domains.renderingDomain?.invalidateCountryPatch?.('territory-annex-committed');
        dependencies.domains.renderingDomain?.invalidateTerritorialPatch?.('territory-annex-committed');
      },
      onSuccess: plan => {
        const removedText = plan.removedIds.length ? ` · ${plan.removedIds.length}개국 완전 흡수` : '';
        const selectedText = selectedComponents.length ? `선택한 ${selectedComponents.length}개 영토 조각을 포함해 ` : '선택한 ';
        (0, dependencies.feedback.setActionStatus)(`${selectedText}${plan.affectedDonorIds.length}개국의 영토를 ${targetName}에 편입했습니다${removedText}.`, 'success', 4000);
      },
      onError: error => (0, dependencies.feedback.reportOperationError)(error, '영토를 편입하지 못해 변경을 되돌렸습니다. 편입 범위를 조정한 뒤 다시 시도하세요.', 'PL-ANNEX-002'),
      shouldKeepResult: () => dependencies.projectState.state.territorySelectionSession === session
        && expectedKey === [session.id, session.projectGeneration, session.settingsRevision, session.sourceRevision, session.selectionRevision].join(':'),
    });
    return true;
  }

  function newCountryPreviewContext(session) {
    if (session?.kind !== 'entity' || session.entityKind !== 'general' || !!session.parentId || session.stage !== 'selection') return null;
    const transferredGeometry = session.combinedGeometry;
    if (!transferredGeometry) {
      (0, dependencies.feedback.setActionStatus)('신생국 영토 후보를 찾을 수 없습니다.', 'error', 3800);
      return null;
    }
    if (!(0, dependencies.objectOperationsB.requireObjectsUnlocked)(session.sourceCountryIds.map(id => ({ domain: 'territorial', type: 'entity', id: String(id) })), '새 국가를 분리')) return null;
    const sourceIds = session.sourceCountryIds.map(String);
    const snapshot = (0, dependencies.snapshots.snapshotEditable)();
    const feature = (0, dependencies.objectPicking.createCountryFeature)(
      session.name.trim(),
      (0, dependencies.countryEditingA.editingDraftCoordinates)(),
      (0, dependencies.countryValidation.snapGeometryToGrid)(transferredGeometry, 7),
    );
    feature.id = session.generatedId;
    return { sourceIds, snapshot, feature, transferredGeometry };
  }

  async function prepareNewCountrySelectionPreview(session, expectedKey) {
    const context = newCountryPreviewContext(session);
    if (!context) return false;
    const { sourceIds, snapshot, feature, transferredGeometry } = context;
    await (0, dependencies.geometryOperations.beginWorkerGeometryPreview)({
      operation: 'new-country',
      payload: { sourceIds, transferredGeometry, newFeature: feature },
      snapshot,
      applyResult: transferPlan => applyTerritorialGeometryPlan(transferPlan, {
        patchOptions: { presentation: 'preserve-existing-scene' },
        updateDependents: () => (0, dependencies.landRelations.transferLandDependents)(transferredGeometry, sourceIds, feature.id),
        clearMultiDraft: true,
        clearDraftReason: 'country-created',
        selectedId: feature.id,
      }),
      invalidateAfterApply: () => dependencies.domains.renderingDomain?.invalidateCountryPatch?.('new-country-committed'),
      onSuccess: transferPlan => {
        const removedText = transferPlan.removedIds.length ? ` · 원본 ${transferPlan.removedIds.length}개국 완전 흡수` : '';
        (0, dependencies.feedback.setActionStatus)(`${(0, dependencies.objectPresentation.territorialEntityName)(feature)} 국가를 추가했습니다${removedText}.`, 'success', 4200);
      },
      onError: error => (0, dependencies.feedback.reportOperationError)(error, '국가를 추가하지 못해 변경을 되돌렸습니다. 선택 범위를 조정한 뒤 다시 시도하세요.', 'PL-COUNTRY-002'),
      shouldKeepResult: () => dependencies.projectState.state.territorySelectionSession === session
        && expectedKey === [session.id, session.projectGeneration, session.settingsRevision, session.sourceRevision, session.selectionRevision].join(':'),
    });
    return true;
  }

  async function completeCountryMerge() {
    const sourceId = String(dependencies.projectState.state.mergeSourceCountryId || '');
    const targetIds = [...new Set(dependencies.projectState.state.mergeTargetCountryIds.map(String))].filter(id => id && id !== sourceId);
    if (!sourceId || !targetIds.length) {
      (0, dependencies.feedback.setActionStatus)('합병할 국가를 하나 이상 선택하세요.', 'error', 3200);
      return;
    }
    if (!(0, dependencies.objectOperationsB.requireObjectsUnlocked)([sourceId, ...targetIds].map(id => ({ domain: 'territorial', type: 'entity', id: String(id) })), '국가를 합병')) return;
    const source = dependencies.territorialModel.entityRepository.get(sourceId);
    const targets = targetIds.map(dependencies.territorialModel.entityRepository.get).filter(Boolean);
    if (!source || targets.length !== targetIds.length) {
      (0, dependencies.feedback.setActionStatus)('합병할 국가를 찾을 수 없습니다. 대상을 다시 선택하세요.', 'error');
      return;
    }
    const sourceName = (0, dependencies.objectPresentation.territorialEntityName)(source);
    const snapshot = (0, dependencies.snapshots.snapshotEditable)();
    await (0, dependencies.geometryOperations.beginWorkerGeometryPreview)({
      operation: 'merge',
      payload: { sourceId, targetIds },
      snapshot,
      applyResult: result => applyTerritorialGeometryPlan(result, {
        afterPatch: () => {
          dependencies.territorialModel.entityStore.setField(
            sourceId,
            'name',
            sourceName,
          );
        },
        updateDependents: () => (0, dependencies.landRelations.reassignLandDependents)(targetIds, sourceId),
        centroidIds: [sourceId],
        selectedId: sourceId,
      }),
      invalidateAfterApply: () => dependencies.domains.renderingDomain?.invalidateCountryPatch?.('country-merge-committed'),
      onSuccess: () => (0, dependencies.feedback.setActionStatus)(`${targetIds.length}개국을 ${sourceName}에 합병했습니다.`, 'success', 3200),
      onError: error => (0, dependencies.feedback.reportOperationError)(error, '국가를 합병하지 못해 변경을 되돌렸습니다. 대상을 다시 확인하세요.', 'PL-MERGE-001'),
    });
  }

  return Object.freeze({
    connect,

    get completeCountryMerge() { return completeCountryMerge; },
    get addMultiDraftPart() { return addMultiDraftPart; },
    get completeMultiDraftCreation() { return completeMultiDraftCreation; },
    get composeMultiDraftGeometry() { return composeMultiDraftGeometry; },
    get multiDraftPartCount() { return multiDraftPartCount; },
    get undoMultiDraftPart() { return undoMultiDraftPart; },
    get cancelScheduledMultiDraftPreview() { return cancelScheduledMultiDraftPreview; },
    get finishDraft() { return finishDraft; },
    get finishAnnexSelectionDraft() { return finishAnnexSelectionDraft; },
    get finishNewCountrySelectionDraft() { return finishNewCountrySelectionDraft; },
    get prepareAnnexSelectionPreview() { return prepareAnnexSelectionPreview; },
    get prepareNewCountrySelectionPreview() { return prepareNewCountrySelectionPreview; },
    get redrawCurrentDraft() { return redrawCurrentDraft; },
    get scheduleMultiDraftPreview() { return scheduleMultiDraftPreview; },
  });
}
