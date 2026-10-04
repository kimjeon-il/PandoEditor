/** RiverCandidates: extracted application responsibility.
 * Dependencies are explicitly wired once by the composition modules.
 * Mutable bindings stay local; exported accessors retain live identity.
 */
export function createRiverCandidates() {
  let dependencies;
  let riverPartitionGeneration;
  let riverPartitionCache;
  const geometrySignatures = new WeakMap();
  function connect(ports) {
    if (dependencies) throw new Error('river-candidates already connected');
    dependencies = ports;
  }

  function resetRiverPartitionState({ preserveCache = true } = {}) {
    riverPartitionGeneration += 1;
    dependencies.domainControllers.gisDomain?.cancelRiverPartition?.();
    const session = (0, dependencies.territorySelectionA.activeTerritorySelectionSession)();
    if (session) {
      session.riverPartitionStatus = 'idle';
      session.riverPartitionCandidates = [];
      session.riverPartitionDonorResults = [];
      session.hoveredComponentKey = null;
    }
    if (!preserveCache) riverPartitionCache.clear();
  }

  function riverPartitionGeometrySignature(feature) {
    if (!feature?.geometry) return '';
    let signature = geometrySignatures.get(feature.geometry);
    if (signature === undefined) {
      signature = JSON.stringify(feature.geometry.coordinates || []);
      geometrySignatures.set(feature.geometry, signature);
    }
    return `${String(feature.id || '')}:${signature}`;
  }

  function riverPartitionHydroEditSignature() {
    const edits = dependencies.projectState.state.hydroEdits
      .filter(feature => feature?.properties?.category === 'river' && feature.geometry)
      .map(feature => `${String(feature.id)}:${JSON.stringify(feature.geometry.coordinates || [])}`)
      .sort();
    return edits.join('|');
  }

  function riverPartitionHydroSignature() {
    return `${dependencies.projectState.state.hydroManifest?.version || ''}:${dependencies.projectState.state.hydroManifest?.index?.sha256 || ''}:${riverPartitionHydroEditSignature()}`;
  }

  function riverPartitionCandidateSignature(donors) {
    return [
      ...donors.map(riverPartitionGeometrySignature).sort(),
      riverPartitionHydroSignature(),
      dependencies.applicationConstantsA.RIVER_TERRITORY_PARTITION_ALGORITHM_REVISION,
      (0, dependencies.territorialServicesA.riverTerritoryPartitionConfigFingerprint)(dependencies.applicationConstantsA.RIVER_TERRITORY_PARTITION_CONFIG),
    ].join('::');
  }

  function riverPartitionBoundsOverlap(left, right) {
    return !!left && !!right && left[0] <= right[2] && left[2] >= right[0] && left[1] <= right[3] && left[3] >= right[1];
  }

  function riverPartitionQueryBounds(geometry) {
    const output = [];
    for (const polygon of (0, dependencies.geometryPreview.geometryPolygonSets)(geometry)) {
      const longitudes = [];
      let minLatitude = Infinity;
      let maxLatitude = -Infinity;
      let minLongitude = Infinity;
      let maxLongitude = -Infinity;
      for (const ring of polygon || []) for (const point of ring || []) {
        const longitude = Number(point?.[0]);
        const latitude = Number(point?.[1]);
        if (!Number.isFinite(longitude) || !Number.isFinite(latitude)) continue;
        longitudes.push(longitude);
        minLatitude = Math.min(minLatitude, latitude);
        maxLatitude = Math.max(maxLatitude, latitude);
        minLongitude = Math.min(minLongitude, longitude);
        maxLongitude = Math.max(maxLongitude, longitude);
      }
      if (!longitudes.length) continue;
      if (maxLongitude - minLongitude <= 180) {
        output.push([minLongitude, minLatitude, maxLongitude, maxLatitude]);
        continue;
      }
      let eastMinimum = Infinity;
      let westMaximum = -Infinity;
      for (const longitude of longitudes) {
        if (longitude >= 0) eastMinimum = Math.min(eastMinimum, longitude);
        else westMaximum = Math.max(westMaximum, longitude);
      }
      if (Number.isFinite(eastMinimum)) output.push([eastMinimum, minLatitude, 180, maxLatitude]);
      if (Number.isFinite(westMaximum)) output.push([-180, minLatitude, westMaximum, maxLatitude]);
    }
    return output;
  }

  function riverPartitionFeatureKey(feature) {
    return String(feature?.properties?.pandolab_id ?? feature?.properties?.__logicalFid ?? feature?.id ?? '');
  }

  function riverPartitionRequestActive() {
    const session = (0, dependencies.territorySelectionA.activeTerritorySelectionSession)();
    return session?.stage === 'selection' && session.activePhase === 'components' && session.useRiverBoundaries;
  }

  function refreshRiverPartitionPresentation(reason) {
    const session = (0, dependencies.territorySelectionA.activeTerritorySelectionSession)();
    dependencies.domains.editingDomain?.refreshTerritorySelection?.({ tool: session?.tool, reason });
  }

  function applyRiverPartitionResult(candidates, donorResults) {
    const session = (0, dependencies.territorySelectionA.activeTerritorySelectionSession)();
    if (!session) return { items: [] };
    session.riverPartitionCandidates = candidates;
    session.riverPartitionDonorResults = donorResults;
    const composition = (0, dependencies.territoryComponents.riverBoundaryComposition)((0, dependencies.territoryComponents.territoryBaseComponentItems)(), { candidates, donorResults });
    dependencies.territoryComponents.installRiverComponentIndex(session, composition, candidates, donorResults);
    session.riverPartitionStatus = composition.items.length ? 'ready' : 'error';
    return composition;
  }

  function riverPartitionResultMessage(composition, donorResults, donors) {
    const invalidIds = new Set((donorResults || []).filter(result => result.status === 'invalid').map(result => String(result.donorCountryId)));
    const invalidNames = donors.filter(feature => invalidIds.has(String(feature.id))).map(feature => (0, dependencies.objectPresentation.territorialEntityName)(feature) || feature.properties?.name || '기준 영역');
    const suffix = invalidNames.length ? ` ${invalidNames.join(', ')}은(는) 분할 오류로 제외했습니다.` : '';
    if (composition.riverCandidateCount > 0) return `하천으로 나뉜 영토 조각을 선택하세요.${suffix}`;
    if (composition.items.length) return `분할 가능한 하천이 없어 기존 영토 조각을 표시합니다.${suffix}`;
    return invalidNames.length
      ? `하천 분할 오류로 ${invalidNames.join(', ')}의 영토 조각을 표시할 수 없습니다.`
      : '표시할 수 있는 영토 조각이 없습니다.';
  }

  async function prepareRiverPartitionCandidates() {
    const session = (0, dependencies.territorySelectionA.activeTerritorySelectionSession)();
    const donors = (session?.componentFeatures || []).filter(feature => feature?.geometry);
    if (!riverPartitionRequestActive() || !donors.length) return false;
    resetRiverPartitionState();
    const generation = riverPartitionGeneration;
    const projectGeneration = dependencies.domains.projectDomain?.getGeneration?.();
    const donorSignature = donors.map(riverPartitionGeometrySignature).sort().join('::');
    const editSignature = riverPartitionHydroEditSignature();
    let signature = null;
    const current = () => riverPartitionRequestActive()
      && generation === riverPartitionGeneration
      && projectGeneration === dependencies.domains.projectDomain?.getGeneration?.()
      && session === (0, dependencies.territorySelectionA.activeTerritorySelectionSession)()
      && donorSignature === (session.componentFeatures || []).map(riverPartitionGeometrySignature).sort().join('::')
      && editSignature === riverPartitionHydroEditSignature()
      && (signature === null || signature === riverPartitionCandidateSignature(donors));
    session.riverPartitionStatus = 'loading';
    (0, dependencies.taskUi.setModeBanner)('하천 기준 영토 조각을 준비하는 중입니다.');
    (0, dependencies.taskUi.updateModeButtons)();
    try {
      await (0, dependencies.gisServicesA.ensureGisRuntime)();
      if (!current()) return;
      const ready = dependencies.projectState.state.physicalLoadState.hydro === 'ready'
        || await (0, dependencies.physicalData.loadHydroData)(dependencies.projectState.state.physicalLoadState.hydro === 'error');
      if (!current()) return;
      if (!ready) throw new Error('강·호수 데이터가 아직 준비되지 않았습니다.');
      // Initial manifest loading is part of this request, not an invalidation.
      // Freeze the data/cache identity only after the manifest is available.
      signature = riverPartitionCandidateSignature(donors);
      const cached = riverPartitionCache.get(signature);
      if (cached) {
        const candidates = structuredClone(cached.candidates);
        const donorResults = structuredClone(cached.donorResults || []);
        const composition = applyRiverPartitionResult(candidates, donorResults);
        (0, dependencies.taskUi.setModeBanner)(riverPartitionResultMessage(composition, donorResults, donors));
        (0, dependencies.taskUi.updateModeButtons)();
        refreshRiverPartitionPresentation('river-partition-cache-ready');
        return;
      }
      const sources = await dependencies.domainControllers.gisDomain.loadRiverPartitionFeatures(donors);
      if (!current()) return;
      const result = await dependencies.domainControllers.gisDomain.computeRiverPartition({
        donors: donors.map(feature => ({
          countryId: String(feature.id || ''),
          geometry: feature.geometry,
          geometryRevision: riverPartitionGeometrySignature(feature),
        })),
        riverFeatures: sources.features,
        hydroRevision: riverPartitionHydroSignature(),
        config: dependencies.applicationConstantsA.RIVER_TERRITORY_PARTITION_CONFIG,
        algorithmRevision: dependencies.applicationConstantsA.RIVER_TERRITORY_PARTITION_ALGORITHM_REVISION,
      });
      if (!current()) return;
      if (!result) return;
      const candidates = (result.candidates || []).map(candidate => ({
        ...candidate,
        geometry: (0, dependencies.cutGeometry.normalizeClippedLandGeometry)(candidate?.geometry),
      })).filter(candidate => candidate.geometry && candidate.donorCountryId);
      const donorResults = result.donorResults || [];
      const diagnostics = { ...sources.diagnostics, ...(result.diagnostics || {}) };
      riverPartitionCache.set(signature, { candidates: structuredClone(candidates), donorResults: structuredClone(donorResults), diagnostics });
      if (riverPartitionCache.size > 8) riverPartitionCache.delete(riverPartitionCache.keys().next().value);
      const composition = applyRiverPartitionResult(candidates, donorResults);
      (0, dependencies.taskUi.setModeBanner)(riverPartitionResultMessage(composition, donorResults, donors));
      (0, dependencies.taskUi.updateModeButtons)();
      refreshRiverPartitionPresentation('river-partition-ready');
    } catch (error) {
      if (!current()) return;
      session.riverPartitionStatus = error?.code === 'RIVER_PARTITION_SOURCE_ERROR' ? 'source-error' : 'error';
      session.riverPartitionCandidates = [];
      session.riverPartitionDonorResults = [];
      (0, dependencies.taskUi.setModeBanner)(error?.code === 'RIVER_PARTITION_SOURCE_ERROR'
        ? '가져올 국가의 하천 데이터를 불러오지 못했습니다.'
        : '강으로 분리되는 영토 조각을 계산하지 못했습니다.');
      (0, dependencies.taskUi.updateModeButtons)();
      (0, dependencies.feedback.reportOperationError)(error, '강으로 분리되는 영토 조각을 계산하지 못했습니다. 잠시 후 다시 시도하세요.', 'PL-TERRITORY-RIVER-001', 4200);
      refreshRiverPartitionPresentation('river-partition-error');
    }
  }

  function initializeRiverPartitionGeneration() {
    (riverPartitionGeneration = 0);

    (riverPartitionCache = new Map());
  }

  return Object.freeze({
    connect,
    initializeRiverPartitionGeneration,
    get prepareRiverPartitionCandidates() { return prepareRiverPartitionCandidates; },
    get resetRiverPartitionState() { return resetRiverPartitionState; },
    get riverPartitionBoundsOverlap() { return riverPartitionBoundsOverlap; },
    get riverPartitionFeatureKey() { return riverPartitionFeatureKey; },
    get riverPartitionQueryBounds() { return riverPartitionQueryBounds; },
  });
}
