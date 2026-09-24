export function createGisImportTransactionCommitter(runtime = {}) {
  const {
    state,
    TERRITORIAL_UNIT_TYPES,
    TERRITORIAL_COVERAGE_MODES,
    DISTRIBUTION_TYPES,
    DISTRIBUTION_MODES,
    GENERIC_FEATURE_SCHEMA_VERSION,
    DEFAULT_GENERIC_FEATURE_COLOR,
    uid,
    deepClone,
    countryFeatureById,
    countryName,
    territorialUnitName,
    territorialRepository,
    distributionService,
    genericFeatureService,
    resolveImportedCountryId,
    normalizeCountryGeometry,
    normalizeClippedLandGeometry,
    createTerritorialFeature,
    createPartitionTerritorialFeature,
    normalizeTerritorialUnits,
    reconcileTerritorialUnitCompleteness,
    partitionGroupMatches,
    multiPolygonPlanarArea,
    createGisImportError,
    RELIABILITY_ERROR_CATEGORIES,
    recordHistory,
    snapshotEditable,
    markCountryGeometriesChanged,
    refreshCountryCentroids,
    normalizeProjectObjects,
    markLayerTreeDirty,
    applyTerritorialUnitSelectionIntent,
    renderingDomain,
    queueAutosave,
    setActionStatus,
    createDistributionLayer,
    createDistributionEntry,
    activeLayerFolderKeys,
    normalizeGenericFeatureSemantics,
    validateStructuredGeometry,
    territorialTypeLabel,
    sphericalGeometryAreaKm2,
    buildTerritorialImportTransactionPlan,
    mapEditClient,
    validateGisCountryCollection,
    reindexCountries,
    transferLandDependents,
    assertCurrentProjectReferences,
    commitHistorySnapshot,
    restoreCountryEditSnapshot,
    createCancellationError,
    buildSharedBoundaryTopology,
    analyzeAdminCountryCoast,
    ensureGisRuntime,
    getCoastReconciliationController,
    normalizeCoastDecision,
    planCoastReconciliations,
    validateCoastReplacement,
    DISTRIBUTION_TYPE_LABELS,
    polygonClipping,
    importedCountryOverrides,
    applyImportedPackageAssets,
    projectDomain,
    appendImportedSourceInfo,
    assertProjectReferenceIntegrity,
    pruneLayerItemVisibility,
    scheduleCountryLabelAnchors,
    selectionUiController,
  } = runtime;

  function territorialUnitMatchesFromImportedValue(value, countryId = '', units = state.territorialUnits) {
    const key = String(value ?? '').trim();
    if (!key) return [];
    const exact = units.filter(feature => String(feature.id) === key);
    if (exact.length) return exact;
    return units.filter(feature => territorialUnitName(feature).toLocaleLowerCase('ko') === key.toLocaleLowerCase('ko')
      && (!countryId || String(feature.properties?.sovereignId || '') === String(countryId)));
  }
  
  function importedTerritorialUnitFeature(raw, index, kind, mapping, sourceFolderId, knownUnits) {
    if (!['Polygon', 'MultiPolygon'].includes(raw.geometry?.type)) return null;
    const properties = raw.properties || {};
    const rawCountryValue = mapping.useFeatureCountryField && mapping.countryField
      ? String(properties[mapping.countryField] ?? '').trim()
      : '';
    const fieldCountry = rawCountryValue
      ? resolveImportedCountryId(rawCountryValue, state.countriesData?.features || [])
      : '';
    if (rawCountryValue && !fieldCountry) throw createGisImportError(`객체별 소속 국가 값 "${rawCountryValue}"을(를) 현재 지도에서 찾을 수 없습니다.`, {
      category: RELIABILITY_ERROR_CATEGORIES.RELATION,
      objectIds: [String(raw.id ?? index + 1), rawCountryValue],
    });
    const countryId = String(fieldCountry || resolveImportedCountryId(mapping.targetCountryId, state.countriesData?.features || []) || '');
    const commonParent = kind === TERRITORIAL_UNIT_TYPES.SUBUNIT && mapping.parentId
      ? knownUnits.find(candidate => String(candidate.id) === String(mapping.parentId)
        && String(candidate.properties?.sovereignId || '') === countryId)
      : null;
    const rawParentValue = kind === TERRITORIAL_UNIT_TYPES.SUBUNIT && mapping.useFeatureCountryField && mapping.parentField
      ? String(properties[mapping.parentField] ?? '').trim()
      : '';
    const parentMatches = rawParentValue
      ? territorialUnitMatchesFromImportedValue(rawParentValue, countryId, knownUnits)
      : [];
    const mappedParent = parentMatches.length === 1 ? parentMatches[0] : null;
    if (rawParentValue && parentMatches.length > 1) throw createGisImportError(`객체별 상위 단위 값 "${rawParentValue}"이(가) 여러 영역과 일치합니다. 고유 ID로 직접 연결하세요.`, {
      category: RELIABILITY_ERROR_CATEGORIES.RELATION,
      objectIds: [String(raw.id ?? index + 1), ...parentMatches.map(feature => String(feature.id))],
    });
    if (rawParentValue && !mappedParent) throw createGisImportError(`객체별 상위 단위 값 "${rawParentValue}"을(를) 현재 지도에서 찾을 수 없습니다.`, {
      category: RELIABILITY_ERROR_CATEGORIES.RELATION,
      objectIds: [String(raw.id ?? index + 1), rawParentValue],
    });
    const parent = commonParent || mappedParent;
    const mappedId = mapping.idField === '__fid__' ? raw.id : properties[mapping.idField];
    const sourceId = String(mappedId ?? raw.id ?? '').trim();
    const baseOptions = {
      id: uid(),
      unitType: kind,
      sovereignId: countryId,
      parentId: parent?.id || '',
      name: String(mapping.nameField ? properties[mapping.nameField] || '' : properties.name || '').trim() || `가져온 ${territorialTypeLabel(kind)} ${index + 1}`,
      color: properties.color || properties.editorColor || '',
      notes: properties.notes || '',
      sourceFolderId,
      metadata: { sourceId },
      geometry: normalizeCountryGeometry(raw.geometry) || raw.geometry,
    };
    if (kind === TERRITORIAL_UNIT_TYPES.REGION) {
      return createTerritorialFeature({
        ...baseOptions,
        coverageMode: TERRITORIAL_COVERAGE_MODES.EXPLICIT,
      });
    }
    return createPartitionTerritorialFeature(baseOptions);
  }
  
  function prepareImportedTerritorialUnitFeatures(features, kind, mapping, sourceFolderId) {
    const knownUnits = deepClone(state.territorialUnits);
    const imported = [];
    const ids = new Set(knownUnits.map(feature => String(feature.id)));
    for (let index = 0; index < features.length; index += 1) {
      const feature = importedTerritorialUnitFeature(features[index], index, kind, mapping, sourceFolderId, [...knownUnits, ...imported]);
      if (!feature) continue;
      if (!feature.properties?.sovereignId && kind !== TERRITORIAL_UNIT_TYPES.REGION) throw createGisImportError(`${territorialUnitName(feature)}의 소속 국가를 정하지 못했습니다.`, {
        category: RELIABILITY_ERROR_CATEGORIES.RELATION,
        objectIds: [feature.id],
      });
      if (ids.has(String(feature.id))) throw new Error(`영역 ID 충돌: ${feature.id}`);
      ids.add(String(feature.id));
      imported.push(feature);
    }
    if (!imported.length) throw new Error('가져올 Polygon 또는 MultiPolygon 객체가 없습니다.');
    return imported;
  }
  
  function importedTerritorialParent(parentId, sovereignId, country, units = state.territorialUnits) {
    const normalizedParentId = String(parentId || '');
    if (!normalizedParentId) return null;
    const territorialParent = (units || []).find(candidate => String(candidate.id) === normalizedParentId);
    if (territorialParent) return territorialParent;
    return normalizedParentId === String(sovereignId || '') ? country : null;
  }
  
  function appendPreparedTerritorialUnits(imported, kind, { preserveIds = [] } = {}) {
    const clipper = polygonClipping;
    const nextUnits = deepClone(state.territorialUnits);
    const preserved = new Set(preserveIds.map(String));
    const affectedCountries = new Set();
    for (const feature of imported) {
      const countryId = String(feature.properties?.sovereignId || '');
      const country = countryFeatureById(countryId);
      const parent = importedTerritorialParent(
        feature.properties.parentId,
        countryId,
        country,
        nextUnits,
      );
      const container = parent || country;
      if (!country?.geometry) throw createGisImportError(`${territorialUnitName(feature)}의 소속 국가를 찾을 수 없습니다.`, {
        category: RELIABILITY_ERROR_CATEGORIES.RELATION,
        objectIds: [feature.id, countryId],
      });
      if (feature.properties.parentId && !parent?.geometry) throw createGisImportError(`${territorialUnitName(feature)}의 지정된 부모 영역을 찾을 수 없습니다.`, {
        category: RELIABILITY_ERROR_CATEGORIES.RELATION,
        objectIds: [feature.id, feature.properties.parentId],
      });
      if (!container?.geometry) throw createGisImportError(`${territorialUnitName(feature)}의 소속 국가 또는 상위 단위를 찾을 수 없습니다.`, {
        category: RELIABILITY_ERROR_CATEGORIES.RELATION,
        objectIds: [feature.id, countryId, feature.properties.parentId],
      });
      const outside = preserved.has(String(feature.id))
        ? null
        : normalizeClippedLandGeometry(clipper.difference(feature.geometry.coordinates, container.geometry.coordinates));
      if (outside && sphericalGeometryAreaKm2(outside) > Math.max(0.0001, sphericalGeometryAreaKm2(feature.geometry) * 1e-9)) {
        throw new Error(`${territorialUnitName(feature)}의 전체 geometry가 선택한 국가 또는 상위 단위 안에 포함되지 않습니다.`);
      }
      const context = {
        unitType: kind,
        sovereignId: countryId,
        parentId: feature.properties.parentId,
      };
      const siblings = nextUnits.filter(candidate => partitionGroupMatches(candidate, context));
      for (const sibling of siblings) {
        const overlap = clipper.intersection(feature.geometry.coordinates, sibling.geometry.coordinates);
        if (multiPolygonPlanarArea(overlap) > Math.max(1e-9, multiPolygonPlanarArea(feature.geometry.coordinates) * 1e-9)) {
          throw new Error(`${territorialUnitName(feature)}이(가) 기존 ${territorialUnitName(sibling)}과(와) 겹칩니다.`);
        }
      }
      nextUnits.push(feature);
      affectedCountries.add(countryId);
    }
    state.territorialUnits = normalizeTerritorialUnits(nextUnits, { countryExists: id => !!countryFeatureById(id) });
    reconcileTerritorialUnitCompleteness(affectedCountries, { preserveIds: [...preserved] });
    return affectedCountries;
  }
  
  async function commitTerritorialImportWithTransfer(result, fileName) {
    const kind = result.targetType === 'subunit'
      ? TERRITORIAL_UNIT_TYPES.SUBUNIT
      : result.targetType === 'region'
        ? TERRITORIAL_UNIT_TYPES.REGION
        : TERRITORIAL_UNIT_TYPES.SUBUNIT;
    const mapping = result.mapping || {};
    const sourceFolderId = `gis:${uid('source')}`;
    const imported = prepareImportedTerritorialUnitFeatures(result.collection?.features || [], kind, mapping, sourceFolderId);
    const snapshot = snapshotEditable();
    const affectedCountryIds = new Set();
    const preservedIds = new Set();
    const countryGeometryOverrides = new Map();
    let activeRequestId = null;
    try {
      // Coast decisions are made against a detached draft before any annex or
      // canonical project mutation. Every later impact calculation uses the
      // resulting geometry, not the raw import geometry.
      for (const feature of imported) {
        const country = countryFeatureById(feature.properties?.sovereignId);
        const resolution = requireImportCoastResolution(await resolveTerritorialCoast(feature, country, countryGeometryOverrides));
        if (resolution.direction === 'admin-to-country' || resolution.direction === 'independent') preservedIds.add(String(feature.id));
      }
  
      const draftCountries = deepClone(state.countriesData);
      const draftById = new Map((draftCountries.features || []).map(feature => [String(feature.id || ''), feature]));
      for (const [countryId, geometry] of countryGeometryOverrides) {
        const country = draftById.get(String(countryId));
        if (country) country.geometry = deepClone(geometry);
      }
  
      let impact = { groups: [], absorbedCountryIds: [] };
      let annexResponse = null;
      if (kind !== TERRITORIAL_UNIT_TYPES.REGION) {
        impact = buildTerritorialImportTransactionPlan({
          features: imported,
          countries: draftCountries.features || [],
          targetCountryId: '',
          useFeatureCountryField: true,
          countryField: 'sovereignId',
          clipper: polygonClipping,
          areaKm2: sphericalGeometryAreaKm2,
        });
        const targetIds = new Set((impact.groups || []).map(group => String(group.targetCountryId)));
        const absorbedTarget = (impact.absorbedCountryIds || []).find(id => targetIds.has(String(id)));
        if (absorbedTarget) throw new Error('한 가져오기 작업에서 소속 국가가 다른 대상 국가에 완전히 흡수됩니다. 객체별 소속 국가를 다시 확인하세요.');
        const operations = (impact.groups || []).map(group => ({
          targetId: String(group.targetCountryId),
          donorIds: (group.donorIds || []).map(String).filter(id => id && id !== String(group.targetCountryId) && draftById.has(id)),
          transferredGeometry: group.importedGeometry,
          allowUnclaimed: true,
        }));
        mapEditClient.rebase(draftCountries.features || []);
        annexResponse = await mapEditClient.execute('annex-batch', { operations });
        activeRequestId = annexResponse.requestId;
        const patchById = new Map((annexResponse.result.features || []).map(feature => [String(feature.id || ''), feature]));
        const removed = new Set((annexResponse.result.removedIds || []).map(String));
        draftCountries.features = (draftCountries.features || [])
          .filter(feature => !removed.has(String(feature.id || '')))
          .map(feature => patchById.get(String(feature.id || '')) || feature);
        for (const [id, feature] of patchById) {
          if (!draftById.has(id)) draftCountries.features.push(feature);
        }
        for (const id of annexResponse.result.affectedIds || []) affectedCountryIds.add(String(id));
      }
      for (const id of countryGeometryOverrides.keys()) affectedCountryIds.add(String(id));
  
      const draftCountryById = new Map((draftCountries.features || [])
        .map(feature => [String(feature.id || ''), feature]));
      for (const feature of imported) {
        const sovereignId = String(feature.properties?.sovereignId || '');
        if (!sovereignId && kind === TERRITORIAL_UNIT_TYPES.REGION) continue;
        const country = draftCountryById.get(sovereignId);
        const parentId = String(feature.properties?.parentId || '');
        const parent = importedTerritorialParent(parentId, sovereignId, country);
        if (!country?.geometry) throw createGisImportError('소속 국가를 찾을 수 없습니다.', {
          category: RELIABILITY_ERROR_CATEGORIES.RELATION,
          objectIds: [feature.id, sovereignId],
        });
        if (parentId && !parent?.geometry) throw createGisImportError('지정된 부모 영역을 찾을 수 없습니다.', {
          category: RELIABILITY_ERROR_CATEGORIES.RELATION,
          objectIds: [feature.id, parentId],
        });
        const container = parent || country;
        const overlap = normalizeClippedLandGeometry(polygonClipping.intersection(
          feature.geometry.coordinates,
          container.geometry.coordinates,
        ));
      if (!overlap || sphericalGeometryAreaKm2(overlap) <= Math.max(0.000001, sphericalGeometryAreaKm2(feature.geometry) * 1e-10)) {
          throw createGisImportError('가져온 영역이 선택한 국가 또는 부모 영역과 겹치지 않습니다.', {
            category: RELIABILITY_ERROR_CATEGORIES.GEOMETRY,
            objectIds: [feature.id, sovereignId, parentId],
          });
        }
        if (parent) {
          const outsideParent = normalizeClippedLandGeometry(polygonClipping.difference(
            feature.geometry.coordinates,
            parent.geometry.coordinates,
          ));
      if (outsideParent && sphericalGeometryAreaKm2(outsideParent) > Math.max(0.0001, sphericalGeometryAreaKm2(feature.geometry) * 1e-9)) {
            throw createGisImportError('가져온 하위단위가 지정된 부모 영역 밖에 존재합니다.', {
              category: RELIABILITY_ERROR_CATEGORIES.GEOMETRY,
              objectIds: [feature.id, parentId],
            });
          }
        }
      }
  
      const draftValidation = await validateGisCountryCollection(draftCountries, affectedCountryIds);
      if (Number(draftValidation?.overlapAreaKm2 || 0) > 0.001) throw createGisImportError('해안선 정합 및 영토 이전 결과에 국가 간 중첩이 남았습니다.', {
        category: RELIABILITY_ERROR_CATEGORIES.GEOMETRY,
        objectIds: draftValidation?.firstOverlap || [...affectedCountryIds],
      });
  
      // Canonical mutation begins only after coast, impact, worker and geometry
      // validation have all succeeded. The surrounding snapshot restores this
      // short synchronous commit if project reference validation fails.
      state.countriesData = reindexCountries(draftCountries, true);
      for (const id of affectedCountryIds) state.historyDirtyCountryIds.add(id);
      for (const group of impact.groups || []) {
        const targetId = String(group.targetCountryId);
        const donorIds = (group.donorIds || []).map(String).filter(id => id && id !== targetId);
        transferLandDependents(group.importedGeometry, donorIds, targetId);
      }
      let importedCountries;
      if (kind === TERRITORIAL_UNIT_TYPES.REGION) {
        state.territorialUnits.push(...deepClone(imported));
        importedCountries = new Set(imported.map(feature => String(feature.properties?.sovereignId || '')).filter(Boolean));
      } else {
        importedCountries = appendPreparedTerritorialUnits(imported, kind, { preserveIds: [...preservedIds] });
      }
      for (const id of importedCountries) affectedCountryIds.add(String(id));
      normalizeProjectObjects();
      assertCurrentProjectReferences();
      refreshCountryCentroids(affectedCountryIds);
      markLayerTreeDirty();
      commitHistorySnapshot(snapshot, {
        type: 'gis-import',
        description: `${fileName} 영토 이전 및 ${territorialTypeLabel(kind)} 가져오기`,
        affectedIds: [...affectedCountryIds, ...imported.map(feature => String(feature.id))],
      });
      renderingDomain?.invalidateTerritorialPatch?.('territorial-import-committed');
      queueAutosave();
      setActionStatus(`${territorialTypeLabel(kind)} ${imported.length}개를 전체 형상으로 가져왔습니다.`, 'success', 4400);
      if (activeRequestId != null) mapEditClient.discard(activeRequestId);
      activeRequestId = null;
      mapEditClient.rebase(state.countriesData?.features || []);
    } catch (error) {
      if (activeRequestId != null) mapEditClient.discard(activeRequestId);
      restoreCountryEditSnapshot(snapshot);
      mapEditClient.rebase(state.countriesData?.features || []);
      throw error;
    }
  }
  
  function requireImportCoastResolution(resolution) {
    if (resolution?.direction === 'cancel') throw createCancellationError('해안선 정합을 취소했습니다.');
    return resolution;
  }
  
  async function resolveTerritorialCoast(feature, country, countryGeometryOverrides) {
    const unitType = feature?.properties?.unitType;
    if (![TERRITORIAL_UNIT_TYPES.SUBUNIT, TERRITORIAL_UNIT_TYPES.REGION].includes(unitType)) return { direction: 'none' };
    if (!country?.geometry) {
      if (!feature?.properties?.sovereignId && unitType === TERRITORIAL_UNIT_TYPES.REGION) return { direction: 'none' };
      throw createGisImportError('소속 국가를 찾을 수 없습니다.', {
        category: RELIABILITY_ERROR_CATEGORIES.RELATION,
        objectIds: [feature?.id, feature?.properties?.sovereignId],
      });
    }
    const draftCountryFeatures = (state.countriesData?.features || []).map(candidate => {
      const id = String(candidate.id || '');
      const geometry = countryGeometryOverrides.get(id);
      return geometry ? { ...candidate, geometry } : candidate;
    });
    const topology = buildSharedBoundaryTopology(draftCountryFeatures);
    const analysis = analyzeAdminCountryCoast({
      adminFeature: feature,
      countryFeature: { ...country, geometry: countryGeometryOverrides.get(String(country.id || '')) || country.geometry },
      countryTopology: topology,
    });
    if (analysis.status !== 'unavailable' && !analysis.conflicts.length) return { direction: 'none' };
    await ensureGisRuntime();
    const choice = await (await getCoastReconciliationController()).open({
      subjectName: territorialUnitName(feature),
      subjectActionLabel: '가져온 영역',
      countryName: countryName(country),
      conflicts: analysis.conflicts,
      automaticAvailable: analysis.status !== 'unavailable',
      unavailableReason: analysis.unavailableReason,
    });
    const direction = normalizeCoastDecision(choice);
    if (direction === 'cancel') return { direction };
    if (direction === 'independent') return { direction };
    const baseCountryGeometry = deepClone(countryGeometryOverrides.get(String(country.id || '')) || country.geometry);
    const baseAdminGeometry = deepClone(feature.geometry);
    const planned = planCoastReconciliations({
      conflicts: analysis.conflicts.map(conflict => ({ ...conflict, countryGeometry: baseCountryGeometry, adminGeometry: baseAdminGeometry })),
      direction,
    });
    const nextAdmin = planned.adminGeometry;
    const nextCountry = planned.countryGeometry;
    const adminValidation = validateCoastReplacement(nextAdmin, { clipper: polygonClipping });
    const countryValidation = validateCoastReplacement(nextCountry, { clipper: polygonClipping });
    if (!adminValidation.ok || !countryValidation.ok) throw createGisImportError('해안선 정합 결과가 유효한 닫힌 영역이 아닙니다.', {
      category: RELIABILITY_ERROR_CATEGORIES.GEOMETRY,
      objectIds: [feature.id, country.id],
      technicalMessage: [...(adminValidation.issues || []), ...(countryValidation.issues || [])].join(' / '),
    });
    if (direction === 'country-to-admin') feature.geometry = nextAdmin;
    else countryGeometryOverrides.set(String(country.id || ''), nextCountry);
    return { direction };
  }
  
  async function importGeoJsonTerritorialUnits(features, kind, mapping) {
    const clipper = polygonClipping;
    const sourceFolderId = `geojson:${uid('source')}`;
    const nextUnits = deepClone(state.territorialUnits);
    const countryGeometryOverrides = new Map();
    const preservedIds = new Set();
    const importedIds = [];
    let importedCount = 0;
    for (let index = 0; index < features.length; index += 1) {
      const feature = importedTerritorialUnitFeature(features[index], index, kind, mapping, sourceFolderId, nextUnits);
      if (!feature) continue;
      const country = countryFeatureById(feature.properties.sovereignId);
      const coastResolution = requireImportCoastResolution(await resolveTerritorialCoast(feature, country, countryGeometryOverrides));
      if (coastResolution.direction === 'admin-to-country' || coastResolution.direction === 'independent') preservedIds.add(String(feature.id));
      const parent = feature.properties.parentId
        ? nextUnits.find(candidate => String(candidate.id) === String(feature.properties.parentId))
        : null;
      const container = parent || (country ? { ...country, geometry: countryGeometryOverrides.get(String(country.id || '')) || country.geometry } : country);
      if (!container?.geometry) {
        feature.properties.sovereignId = '';
        feature.properties.parentId = '';
        nextUnits.push(feature);
        importedCount += 1;
        continue;
      }
      const clipped = preservedIds.has(String(feature.id))
        ? normalizeClippedLandGeometry(feature.geometry)
        : normalizeClippedLandGeometry(clipper.intersection(feature.geometry.coordinates, container.geometry.coordinates));
      if (!clipped) throw new Error(`${territorialUnitName(feature)}이(가) 지정된 국가 또는 부모와 겹치지 않습니다.`);
      feature.geometry = clipped;
      const context = {
        unitType: kind,
        sovereignId: feature.properties.sovereignId,
        parentId: feature.properties.parentId,
      };
      const siblings = nextUnits.filter(candidate => partitionGroupMatches(candidate, context));
      for (const sibling of siblings) {
        const overlap = clipper.intersection(feature.geometry.coordinates, sibling.geometry.coordinates);
        if (multiPolygonPlanarArea(overlap) > Math.max(1e-9, multiPolygonPlanarArea(feature.geometry.coordinates) * 1e-9)) {
          throw new Error(`${territorialUnitName(feature)}이(가) 기존 ${territorialUnitName(sibling)}과(와) 겹칩니다.`);
        }
      }
      nextUnits.push(feature);
      importedCount += 1;
      importedIds.push(String(feature.id));
    }
    if (!importedCount) throw new Error('가져올 Polygon 또는 MultiPolygon 객체가 없습니다.');
    recordHistory();
    for (const [countryId, geometry] of countryGeometryOverrides) {
      const country = countryFeatureById(countryId);
      if (country) {
        country.geometry = geometry;
        state.historyDirtyCountryIds.add(countryId);
      }
    }
    state.territorialUnits = normalizeTerritorialUnits(nextUnits, { countryExists: id => !!countryFeatureById(id) });
    const affectedCountryIds = new Set(state.territorialUnits.map(feature => String(feature.properties?.sovereignId || '')).filter(Boolean));
    reconcileTerritorialUnitCompleteness(affectedCountryIds, { preserveIds: [...preservedIds] });
    markLayerTreeDirty();
    renderingDomain?.invalidateTerritorialPatch?.('territorial-import');
    queueAutosave();
    setActionStatus(`${territorialTypeLabel(kind)} ${importedCount}개를 가져왔습니다.`, 'success', 3800);
    return importedIds;
  }
  
  async function importGeoJsonRegions(features, mapping) {
    const imported = [];
    const defaultContext = { sovereignId: '', parentId: '' };
    const existingIds = new Set(state.territorialUnits.map(feature => String(feature.id)));
    const countryGeometryOverrides = new Map();
    for (let index = 0; index < features.length; index += 1) {
      const raw = features[index];
      if (!['Polygon', 'MultiPolygon'].includes(raw.geometry?.type)) continue;
      const properties = raw.properties || {};
      const sourceId = String(raw.id ?? properties.id ?? '').trim();
      const id = uid();
      if (existingIds.has(id)) throw new Error(`영역 ID 충돌: ${id}`);
      existingIds.add(id);
      const rawCountry = String(mapping.countryField ? properties[mapping.countryField] ?? '' : properties.sovereign_id || properties.sovereignId || properties.country_id || '').trim();
      const resolvedCountryId = resolveImportedCountryId(rawCountry, state.countriesData?.features || []);
      if (rawCountry && !resolvedCountryId) throw createGisImportError('소속 국가를 찾을 수 없습니다.', {
        category: RELIABILITY_ERROR_CATEGORIES.RELATION,
        objectIds: [id, rawCountry],
        technicalMessage: `Unresolved imported region country: ${rawCountry}`,
      });
      const parentValue = String(mapping.parentField ? properties[mapping.parentField] ?? '' : properties.parent_id || properties.parentId || '').trim();
      const parentMatches = parentValue
        ? territorialUnitMatchesFromImportedValue(parentValue, resolvedCountryId || defaultContext.sovereignId)
        : [];
      const parent = parentMatches.length === 1 ? parentMatches[0] : null;
      if (parentValue && parentMatches.length > 1) throw createGisImportError('지정된 부모 이름이 여러 영역과 일치합니다. 고유 ID로 직접 연결하세요.', {
        category: RELIABILITY_ERROR_CATEGORIES.RELATION,
        objectIds: [id, ...parentMatches.map(feature => String(feature.id))],
      });
      if (parentValue && !parent) throw createGisImportError('지정된 부모 영역을 찾을 수 없습니다.', {
        category: RELIABILITY_ERROR_CATEGORIES.RELATION,
        objectIds: [id, parentValue],
        technicalMessage: `Unresolved imported region parent: ${parentValue}`,
      });
      const sovereignId = String(resolvedCountryId || parent?.properties?.sovereignId || defaultContext.sovereignId || '');
      const feature = createTerritorialFeature({
        id,
        unitType: TERRITORIAL_UNIT_TYPES.REGION,
        name: String(mapping.nameField ? properties[mapping.nameField] || '' : properties.name || '').trim() || `가져온 지방 ${index + 1}`,
        parentId: String(parent?.id || (!rawCountry ? defaultContext.parentId : '') || ''),
        sovereignId,
        coverageMode: TERRITORIAL_COVERAGE_MODES.EXPLICIT,
        validFrom: properties.valid_from || properties.validFrom || null,
        validTo: properties.valid_to || properties.validTo || null,
        color: properties.color || properties.editorColor || '',
        metadata: { sourceId },
        geometry: normalizeCountryGeometry(raw.geometry) || raw.geometry,
      });
      const country = sovereignId ? countryFeatureById(sovereignId) : null;
      if (sovereignId && !country?.geometry) throw createGisImportError('소속 국가를 찾을 수 없습니다.', {
        category: RELIABILITY_ERROR_CATEGORIES.RELATION,
        objectIds: [id, sovereignId],
      });
      if (country?.geometry) {
        const countryGeometry = countryGeometryOverrides.get(sovereignId) || country.geometry;
        const overlap = polygonClipping.intersection(feature.geometry.coordinates, countryGeometry.coordinates);
        if (multiPolygonPlanarArea(overlap) <= 1e-12) throw createGisImportError('가져온 영역이 지정된 국가와 겹치지 않습니다.', {
          category: RELIABILITY_ERROR_CATEGORIES.GEOMETRY,
          objectIds: [id, sovereignId],
        });
        requireImportCoastResolution(await resolveTerritorialCoast(feature, country, countryGeometryOverrides));
      }
      imported.push(feature);
    }
    if (!imported.length) throw new Error('가져올 Polygon 또는 MultiPolygon 지방이 없습니다.');
    recordHistory();
    const changedCountryIds = new Set();
    for (const [countryId, geometry] of countryGeometryOverrides) {
      const country = countryFeatureById(countryId);
      if (!country) continue;
      country.geometry = geometry;
      state.historyDirtyCountryIds.add(countryId);
      changedCountryIds.add(countryId);
    }
    state.territorialUnits.push(...imported);
    normalizeProjectObjects();
    if (changedCountryIds.size) {
      refreshCountryCentroids(changedCountryIds);
      markCountryGeometriesChanged(changedCountryIds);
    }
    state.layerVisibility.regions = true;
    for (const feature of imported) delete state.itemVisibility.regions?.[String(feature.id)];
    markLayerTreeDirty();
    if (imported.length === 1) applyTerritorialUnitSelectionIntent(imported[0].id, true);
    else renderingDomain?.invalidateTerritorialPatch?.('region-import');
    queueAutosave();
    setActionStatus(`지방 ${imported.length}개를 가져왔습니다.`, 'success', 3800);
    return imported.map(feature => String(feature.id));
  }
  
  function importGeoJsonDistributions(features, type, mapping, fileName) {
    const layerMap = new Map(state.distributionLayers.map(layer => [layer.id, layer]));
    const entryIds = new Set(state.distributionEntries.map(entry => entry.id));
    const newLayers = [];
    const newEntries = [];
    const generatedLayerIds = new Map();
    const fallbackName = fileName.replace(/\.[^.]+$/, '') || DISTRIBUTION_TYPE_LABELS[type];
    for (let index = 0; index < features.length; index += 1) {
      const raw = features[index];
      if (!['Polygon', 'MultiPolygon'].includes(raw.geometry?.type)) continue;
      const properties = raw.properties || {};
      const name = String(mapping.nameField ? properties[mapping.nameField] || '' : properties.name || '').trim() || fallbackName;
      const sourceLayerId = String(properties.layer_id || '').trim();
      const layerKey = sourceLayerId || `name:${name}`;
      if (!generatedLayerIds.has(layerKey)) generatedLayerIds.set(layerKey, uid());
      const layerId = generatedLayerIds.get(layerKey);
      let layer = layerMap.get(layerId);
      if (layer && layer.type !== type) throw new Error(`분포 레이어 ID 충돌: ${layerId}`);
      if (!layer) {
        layer = createDistributionLayer({
          id: layerId,
          type,
          name,
          color: properties.color || DEFAULT_GENERIC_FEATURE_COLOR,
          metadata: { sourceId: sourceLayerId },
        });
        layerMap.set(layerId, layer);
        newLayers.push(layer);
      }
      const sourceEntryId = String(properties.entry_id ?? raw.id ?? '').trim();
      if (sourceEntryId && newEntries.some(entry => entry.metadata?.sourceId === sourceEntryId)) throw new Error(`외부 분포 엔트리 ID가 중복되었습니다: ${sourceEntryId}`);
      const entryId = uid();
      if (entryIds.has(entryId)) throw new Error(`분포 엔트리 ID 충돌: ${entryId}`);
      entryIds.add(entryId);
      const territorialUnitId = String(properties.territorial_unit_id || '').trim();
      const useTerritorial = !!territorialUnitId && !!territorialRepository.get(territorialUnitId);
      newEntries.push(createDistributionEntry({
        id: entryId,
        layerId,
        mode: useTerritorial ? DISTRIBUTION_MODES.TERRITORIAL : DISTRIBUTION_MODES.GEOMETRY,
        territorialUnitId: useTerritorial ? territorialUnitId : '',
        geometry: useTerritorial ? null : normalizeCountryGeometry(raw.geometry) || raw.geometry,
        share: properties.share ?? 100,
        certainty: properties.certainty || 'unknown',
        validFrom: properties.valid_from || properties.validFrom || null,
        validTo: properties.valid_to || properties.validTo || null,
        metadata: { sourceId: sourceEntryId },
      }));
    }
    if (!newEntries.length) throw new Error('가져올 Polygon 또는 MultiPolygon 분포가 없습니다.');
    distributionService.append({ layers: newLayers, entries: newEntries });
    markLayerTreeDirty();
    setActionStatus(`${DISTRIBUTION_TYPE_LABELS[type]} 분포 ${newEntries.length}개를 가져왔습니다.`, 'success', 3800);
  }
  
  async function importGeoJson(file, { parsed = null, target = 'generic', mapping = {} } = {}) {
    parsed ||= JSON.parse(await file.text());
    const features = parsed.type === 'FeatureCollection' ? parsed.features : parsed.type === 'Feature' ? [parsed] : [];
    const structuredIssues = features.filter(feature => ['Polygon', 'MultiPolygon'].includes(feature.geometry?.type)).flatMap(validateStructuredGeometry);
    if (structuredIssues.length) throw new Error(`가져온 geometry가 올바르지 않습니다. ${structuredIssues[0].message}`);
    if (target === 'subunit') {
      await importGeoJsonTerritorialUnits(features, TERRITORIAL_UNIT_TYPES.SUBUNIT, mapping);
      return;
    }
    if (target === 'region') {
      await importGeoJsonRegions(features, mapping);
      return;
    }
    if (Object.values(DISTRIBUTION_TYPES).includes(target)) {
      importGeoJsonDistributions(features, target, mapping, file.name);
      return;
    }
    const supported = [];
    for (const raw of features) {
      if (!['Point', 'MultiPoint', 'LineString', 'Polygon', 'MultiLineString', 'MultiPolygon'].includes(raw.geometry?.type)) continue;
      const f = deepClone(raw);
      const sourceId = String(f.id ?? f.properties?.id ?? '').trim();
      f.id = uid();
      if (['Polygon', 'MultiPolygon'].includes(f.geometry?.type)) f.geometry = normalizeCountryGeometry(f.geometry) || f.geometry;
      const p = f.properties || {};
      f.properties = {
        schemaVersion: p.schemaVersion ?? GENERIC_FEATURE_SCHEMA_VERSION,
        name: String(p.name || ''),
        color: p.color || DEFAULT_GENERIC_FEATURE_COLOR,
        role: p.role || 'generic',
        ownerId: String(p.ownerId || ''),
        parentId: String(p.parentId || ''),
        landBinding: p.landBinding || 'none',
        topologyGroup: String(p.topologyGroup || ''),
        locked: p.locked === true,
        notes: String(p.notes || ''),
        source: sourceId ? { id: sourceId } : p.source,
      };
      supported.push(normalizeGenericFeatureSemantics(f));
    }
    if (!supported.length) throw new Error('지원되는 점·선·면 지도 객체가 없습니다.');
    genericFeatureService.addMany(supported);
    state.layerFolders = Object.fromEntries(activeLayerFolderKeys().map(key => [key, key === 'genericFeatures']));
    markLayerTreeDirty();
    setActionStatus(`GeoJSON 기타 객체 ${supported.length}개를 가져왔습니다.`, 'success', 3200);
  }
  
  async function applyImportedReplacement(result) {
    const packageState = result.atlasMetadata?.projectState || {};
    const restoredOverrides = {
      ...importedCountryOverrides(result.countriesData),
      ...(packageState.countryOverrides || {}),
    };
    const mergedState = {
      ...packageState,
      countriesData: result.countriesData,
      countryOverrides: applyImportedPackageAssets(result.atlasMetadata, restoredOverrides),
      sourceInfo: result.atlasMetadata?.sourceInfo || result.sourceInfo,
    };
    await projectDomain.load(mergedState);
    setActionStatus(`국가 경계 ${state.countriesData.features.length}개를 새 프로젝트로 열었습니다.`, 'success', 3200);
  }
  
  async function commitGisMerge(result, plan) {
    const importedIds = new Set((result.countriesData?.features || []).map(feature => String(feature.id || '')));
    const packagedOverrides = applyImportedPackageAssets(result.atlasMetadata, {
      ...importedCountryOverrides(result.countriesData),
      ...(result.atlasMetadata?.projectState?.countryOverrides || {}),
    });
    const draftCountries = deepClone(plan.countriesData);
    const draftCountryIds = new Set((draftCountries.features || []).map(feature => String(feature.id || '')).filter(Boolean));
    const draftOverrides = Object.fromEntries(Object.entries(deepClone(state.countryOverrides || {}))
      .filter(([id]) => draftCountryIds.has(String(id))));
    for (const feature of result.countriesData?.features || []) {
      const id = String(feature.id || '');
      if (!id) continue;
      const existingOverride = draftOverrides[id] || {};
      const next = { ...(packagedOverrides[id] || {}), ...existingOverride };
      draftOverrides[id] = next;
    }
    for (const [countryId, update] of Object.entries(result.countryUpdates || {})) {
      const id = String(countryId || '');
      if (!id || !draftCountryIds.has(id) || !update || typeof update !== 'object') continue;
      const next = { ...(draftOverrides[id] || {}) };
      if (String(update.name || '').trim()) next.name = String(update.name).trim();
      draftOverrides[id] = next;
    }
    const validation = await validateGisCountryCollection(draftCountries, plan.affectedIds || importedIds);
    if (Number(validation?.overlapAreaKm2 || 0) > 0.001) {
      throw createGisImportError('가져온 국가가 다른 국가와 실제로 겹칩니다.', {
        category: RELIABILITY_ERROR_CATEGORIES.GEOMETRY,
        objectIds: validation?.firstOverlap || [...importedIds],
        technicalMessage: `Residual overlap: ${validation.overlapAreaKm2} km2`,
      });
    }
    result.assertCurrent?.();
    const preparedUnits = result.preparedTerritorialUnits || [];
    const existingIds = new Set([...draftCountries.features, ...(state.territorialUnits || [])].map(feature => String(feature.id)));
    for (const unit of preparedUnits) {
      if (existingIds.has(String(unit.id))) throw new Error('추가할 하위단위 ID가 중복됩니다.');
      existingIds.add(String(unit.id));
    }
    const before = snapshotEditable();
    try {
      state.countryOverrides = draftOverrides;
      state.countriesData = reindexCountries(draftCountries, true);
      const dependentTargetId = String(result.landDependentsTargetId || '');
      if (dependentTargetId && plan.transferredGeometry && Array.isArray(plan.donorIds)) {
        transferLandDependents(plan.transferredGeometry, plan.donorIds, dependentTargetId);
      }
      for (const transfer of result.landTransfers || []) {
        transferLandDependents(transfer.geometry, transfer.donorIds, transfer.targetId);
      }
      if (preparedUnits.length) {
        state.territorialUnits.push(...deepClone(preparedUnits));
        normalizeProjectObjects();
        markLayerTreeDirty();
      }
      pruneLayerItemVisibility();
      for (const key of Object.keys(state.labelSettings || {})) {
        if (key.startsWith('country:') && !draftCountryIds.has(key.slice('country:'.length))) delete state.labelSettings[key];
      }
      assertProjectReferenceIntegrity({
        countries: state.countriesData.features || [],
        countryOverrides: state.countryOverrides,
        territorialUnits: state.territorialUnits || [],
        territorialRelations: state.territorialRelations || [],
        distributionLayers: state.distributionLayers || [],
        distributionEntries: state.distributionEntries || [],
        labels: state.labels || [],
        genericFeatures: state.genericFeatures || [],
        itemVisibility: state.itemVisibility || {},
        labelSettings: state.labelSettings || {},
      });

      state.sourceInfo = appendImportedSourceInfo(state.sourceInfo, result.sourceInfo);
      scheduleCountryLabelAnchors(null, 10);
      markCountryGeometriesChanged(plan.affectedIds || importedIds, {
        presentation: plan.countryPatchPresentation || 'replace-scene',
      });
      commitHistorySnapshot(before);
    } catch (error) {
      restoreCountryEditSnapshot(before);
      throw error;
    }
    // Canonical data and history are committed. Notification/autosave failures
    // must not restore data while leaving an already committed Undo entry.
    try {
      selectionUiController.clear({ reason: 'gis-merge-selection-clear' });
      renderingDomain?.invalidateCountryPatch?.('gis-merge-committed');
      queueAutosave();
      setActionStatus(result.commitStatus || 'GIS 레이어를 한 번의 편집 작업으로 병합했습니다.', 'success', 3200);
    } catch (error) {
      console.error('[PL-LIB-POST-COMMIT]', error);
    }
    return {
      added: Number(plan.counts?.added || 0),
      subtracted: Number(plan.counts?.subtracted || 0),
      deleted: Number(plan.counts?.deleted || 0),
      affectedIds: [...new Set(plan.affectedIds || [])],
    };
  }

  return Object.freeze({
    applyImportedReplacement,
    commitGisMerge,
    commitTerritorialImportWithTransfer,
    importGeoJson,
    resolveTerritorialCoast,
    importGeoJsonTerritorialUnits,
  });
}
