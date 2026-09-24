/** ObjectPicking: extracted application responsibility.
 * Dependencies are explicitly wired once by the composition modules.
 * Mutable bindings stay local; exported accessors retain live identity.
 */
export function createObjectPicking() {
  let dependencies;
  let objectPickRevision = 0;
  let objectChooserPresentationRevision = 0;
  let objectChooserIntentMode = 'replace';

  function connect(ports) {
    if (dependencies) throw new Error('object-picking already connected');
    dependencies = ports;
  }

  function createCountryFeature(name, rawRing, color = null, geometryOverride = null) {
    const id = (0, dependencies.surfaces.uid)('USR');
    const geometry = geometryOverride
      ? (0, dependencies.platform.deepClone)(geometryOverride)
      : { type: 'Polygon', coordinates: [(0, dependencies.applicationServicesB.orientRing)(rawRing, true)] };
    const feature = {
      type: 'Feature',
      id,
      properties: { name },
      geometry,
    };
    if (color) dependencies.projectState.state.countryOverrides[id] = { ...(dependencies.projectState.state.countryOverrides[id] || {}), color };
    return feature;
  }

  function projectedPointDistance(left, right) {
    return Math.hypot(Number(left?.[0]) - Number(right?.[0]), Number(left?.[1]) - Number(right?.[1]));
  }

  function pointSegmentDistance(point, start, end) {
    const dx = end[0] - start[0];
    const dy = end[1] - start[1];
    if (!dx && !dy) return projectedPointDistance(point, start);
    const t = (0, dependencies.platform.clamp)(((point[0] - start[0]) * dx + (point[1] - start[1]) * dy) / (dx * dx + dy * dy), 0, 1);
    return Math.hypot(point[0] - (start[0] + t * dx), point[1] - (start[1] + t * dy));
  }

  function projectedLineDistance(geometry, screenPoint) {
    if (!geometry) return Infinity;
    const lines = geometry.type === 'LineString' ? [geometry.coordinates]
      : geometry.type === 'MultiLineString' ? geometry.coordinates
        : geometry.type === 'Polygon' ? geometry.coordinates
          : geometry.type === 'MultiPolygon' ? geometry.coordinates.flat()
            : [];
    let minimum = Infinity;
    for (const line of lines) {
      let previous = null;
      for (const coordinate of line || []) {
        const projected = (0, dependencies.mapView.activeProjection)()(coordinate);
        if (previous && projected) minimum = Math.min(minimum, pointSegmentDistance(screenPoint, previous, projected));
        previous = projected;
      }
    }
    return minimum;
  }

  function geometryHitsScreenPoint(geometry, coord, screenPoint, tolerance = 8) {
    if (!geometry) return false;
    let bounds = dependencies.spatialRecords.geometryBoundsCache.get(geometry);
    if (!bounds) {
      bounds = (0, dependencies.cutGeometry.coordinateBounds)(geometry.coordinates);
      dependencies.spatialRecords.geometryBoundsCache.set(geometry, bounds);
    }
    const longitudeSpan = Number(bounds?.[2]) - Number(bounds?.[0]);
    const geographicTolerance = Math.max(0.15, 4 / Math.max(1, (0, dependencies.labelPresentation.currentMapZoom)()));
    if (bounds?.every(Number.isFinite) && longitudeSpan < 350 && (coord[0] < bounds[0] - geographicTolerance || coord[0] > bounds[2] + geographicTolerance || coord[1] < bounds[1] - geographicTolerance || coord[1] > bounds[3] + geographicTolerance)) return false;
    if (geometry.type === 'Point') {
      const projected = (0, dependencies.mapView.activeProjection)()(geometry.coordinates);
      return !!projected && projectedPointDistance(projected, screenPoint) <= tolerance;
    }
    if (geometry.type === 'MultiPoint') return geometry.coordinates.some(point => {
      const projected = (0, dependencies.mapView.activeProjection)()(point);
      return !!projected && projectedPointDistance(projected, screenPoint) <= tolerance;
    });
    if (geometry.type.includes('Polygon')) {
      if ((0, dependencies.landRelations.pointInCountryFeature)(coord, { type: 'Feature', properties: {}, geometry })) return true;
    }
    return projectedLineDistance(geometry, screenPoint) <= tolerance;
  }

  function objectRefSelectable(value) {
    const ref = (0, dependencies.selectionServices.normalizeObjectRef)(value);
    if (!ref || !(0, dependencies.objectLookup.objectRefExists)(ref)) return false;
    if (ref.domain === 'territorial') {
      const group = ref.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY ? 'countries'
        : ref.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.REGION ? 'regions' : 'subunits';
      return dependencies.projectState.state.layerVisibility[group] !== false && (0, dependencies.layerPresentation.isLayerItemVisible)(group, ref.id);
    }
    if (ref.domain === 'distribution') {
      const group = dependencies.distributionPresentation.DISTRIBUTION_TYPE_GROUPS[ref.type] || `${ref.type}s`;
      return dependencies.projectState.state.layerVisibility[group] !== false && (0, dependencies.layerPresentation.isLayerItemVisible)(group, ref.id);
    }
    if (ref.domain === 'generic') return dependencies.projectState.state.layerVisibility.genericFeatures !== false
      && (0, dependencies.layerPresentation.isLayerItemVisible)('genericFeatures', ref.id);
    if (ref.domain === 'label') return dependencies.projectState.state.layerVisibility.labels !== false
      && (0, dependencies.layerPresentation.isLayerItemVisible)('labels', ref.id);
    if (ref.domain === 'hydro') return (0, dependencies.physicalServices.isHydroFeatureVisible)((0, dependencies.hydroModel.hydroFeatureById)(ref.id));
    return false;
  }

  function selectableVisualRank(ref) {
    const order = dependencies.projectState.state.layerPresentation?.overlayOrder || dependencies.renderScene.OVERLAY_GROUPS;
    let group = '';
    if (ref.domain === 'generic') group = 'genericFeatures';
    else if (ref.domain === 'distribution') group = dependencies.distributionPresentation.DISTRIBUTION_TYPE_GROUPS[ref.type] || `${ref.type}s`;
    else if (ref.domain === 'territorial') group = ref.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.SUBUNIT ? 'subunits' : ref.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.REGION ? 'regions' : ref.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY ? 'countries' : 'subunits';
    else if (ref.domain === 'label') group = 'labels';
    else if (ref.domain === 'hydro') group = 'hydro';
    const index = order.indexOf(group);
    if (index >= 0) return 1000 - index;
    return { labels: 1300, hydro: 850, countries: 500 }[group] || 700;
  }

  async function selectableObjectsAt(screenPoint, coord, { seedRefs = [] } = {}) {
    const candidates = [];
    const add = ref => {
      const normalized = (0, dependencies.selectionServices.normalizeObjectRef)(ref);
      if (objectRefSelectable(normalized) && !candidates.some(candidate => candidate.key === normalized.key)) candidates.push(normalized);
    };
    seedRefs.forEach(add);
    dependencies.rendering.selectionPerformanceMetrics.exactHitTestCount = 0;
    const indexed = (0, dependencies.spatialRecords.indexedMapObjectCandidates)(screenPoint);
    for (const entry of indexed) {
      if (entry.domain === 'label') {
        if (!dependencies.projectState.state.layerVisibility.labels || !(0, dependencies.layerPresentation.isLayerItemVisible)('labels', entry.id)) continue;
        const label = dependencies.projectState.state.labels.find(item => String(item.id) === entry.id);
        const projected = label ? (0, dependencies.mapView.activeProjection)()(label.coordinates) : null;
        dependencies.rendering.selectionPerformanceMetrics.exactHitTestCount += 1;
        if (projected && projectedPointDistance(projected, screenPoint) <= ((0, dependencies.surfaces.isMobile)() ? 18 : 11)) add({ domain: 'label', type: label.kind || 'label', id: label.id });
        continue;
      }
      if (entry.domain === 'generic') {
        if (!dependencies.projectState.state.layerVisibility.genericFeatures || !(0, dependencies.layerPresentation.isLayerItemVisible)('genericFeatures', entry.id)) continue;
        const feature = dependencies.projectState.state.genericFeatures.find(item => String(item.id) === entry.id);
        dependencies.rendering.selectionPerformanceMetrics.exactHitTestCount += 1;
        if (feature && geometryHitsScreenPoint(feature.geometry, coord, screenPoint, (0, dependencies.surfaces.isMobile)() ? 14 : 8)) add({ domain: 'generic', type: 'feature', id: feature.id });
        continue;
      }
      if (entry.domain === 'distribution') {
        const row = (0, dependencies.spatialRecords.indexedDistributionRow)(entry.id);
        dependencies.rendering.selectionPerformanceMetrics.exactHitTestCount += 1;
        if (row && geometryHitsScreenPoint(row.geometry, coord, screenPoint, (0, dependencies.surfaces.isMobile)() ? 12 : 7)) add({ domain: 'distribution', type: row.layer.type, id: row.layer.id });
        continue;
      }
      if (entry.domain === 'territorial') {
        const feature = (0, dependencies.objectPresentation.territorialUnitById)(entry.id);
        if (!feature) continue;
        const group = feature.properties?.unitType === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.SUBUNIT ? 'subunits' : feature.properties?.unitType === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.REGION ? 'regions' : 'subunits';
        if (dependencies.projectState.state.layerVisibility[group] === false || !(0, dependencies.layerPresentation.isLayerItemVisible)(group, feature.id)) continue;
        dependencies.rendering.selectionPerformanceMetrics.exactHitTestCount += 1;
        if (geometryHitsScreenPoint(feature.geometry, coord, screenPoint, (0, dependencies.surfaces.isMobile)() ? 12 : 7)) add({ domain: 'territorial', type: feature.properties?.unitType || dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.SUBUNIT, id: feature.id });
        continue;
      }
      if (entry.domain === 'hydro') {
        const feature = (0, dependencies.hydroPresentation.hydroEditById)(entry.id);
        dependencies.rendering.selectionPerformanceMetrics.exactHitTestCount += 1;
        if (feature && (0, dependencies.physicalServices.isHydroFeatureVisible)(feature) && geometryHitsScreenPoint(feature.geometry, coord, screenPoint, (0, dependencies.surfaces.isMobile)() ? 14 : 8)) add({ domain: 'hydro', type: feature.properties?.category || 'river', id: feature.id });
      }
    }
    const canReuseHover = dependencies.pointerInteractionA.lastHoverHit?.ref?.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY && dependencies.pointerInteractionA.lastHoverHit.feature
      && dependencies.pointerInteractionA.lastHoverPickViewRevision === dependencies.mapLayout.viewRevision && dependencies.pointerInteractionA.lastHoverPickPoint
      && Math.hypot(screenPoint[0] - dependencies.pointerInteractionA.lastHoverPickPoint[0], screenPoint[1] - dependencies.pointerInteractionA.lastHoverPickPoint[1]) < 3;
    dependencies.rendering.selectionPerformanceMetrics.pickCacheHit = !!canReuseHover;
    if (dependencies.projectState.state.layerVisibility.countries) {
      let country;
      if (canReuseHover) country = dependencies.pointerInteractionA.lastHoverHit.feature;
      else {
        const pickStartedAt = performance.now();
        country = (0, dependencies.pointerInteractionA.countryAtScreenPoint)(screenPoint, coord, { verify: false });
        dependencies.rendering.selectionPerformanceMetrics.gpuPickMs = performance.now() - pickStartedAt;
      }
      if (country) add({ domain: 'territorial', type: dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY, id: country.id });
    }
    if (dependencies.projectState.state.layerVisibility.rivers || dependencies.projectState.state.layerVisibility.lakes) {
      const hydro = await (0, dependencies.pointerInteractionA.hydroAtScreenPoint)(screenPoint, coord);
      if (hydro) add({ domain: 'hydro', type: hydro.properties?.category || 'river', id: hydro.properties?.pandolab_id || hydro.id });
    }
    return candidates.sort((left, right) => selectableVisualRank(right) - selectableVisualRank(left) || (0, dependencies.objectOperationsA.objectDisplayInfo)(left).name.localeCompare((0, dependencies.objectOperationsA.objectDisplayInfo)(right).name, 'ko'));
  }

  function closeObjectChooser({ restoreFocus = false, cancelPending = true } = {}) {
    if (cancelPending) objectPickRevision += 1;
    objectChooserPresentationRevision += 1;
    objectChooserIntentMode = 'replace';
    const chooser = (0, dependencies.platform.$)('objectChooser');
    if (!chooser) return;
    chooser.classList.add('hidden');
    chooser.removeAttribute('style');
    dependencies.surfaceCommands.replaceObjectChooserCandidates([]);
    if (restoreFocus) (0, dependencies.platform.$)('map')?.focus();
  }

  function openObjectChooser(candidates, screenPoint, intentMode) {
    const chooser = (0, dependencies.platform.$)('objectChooser');
    const list = (0, dependencies.platform.$)('objectChooserList');
    if (!chooser || !list || candidates.length < 2) return closeObjectChooser();
    if ((0, dependencies.surfaces.isMobile)() && dependencies.workspaceUiB.surfaceController.activeMobileSheet) (0, dependencies.workspaceUiA.closeActiveMobileSheet)();
    objectChooserIntentMode = intentMode === 'toggle' ? 'toggle' : 'replace';
    dependencies.surfaceCommands.replaceObjectChooserCandidates(candidates.slice());
    list.replaceChildren(...candidates.map((ref, index) => {
      const info = (0, dependencies.objectOperationsA.objectDisplayInfo)(ref);
      const button = document.createElement('button');
      button.type = 'button';
      button.className = `ui-button ui-row ui-card ui-selectable-row object-chooser-item${dependencies.domains.selectionDomain.has(ref) ? ' is-selected' : ''}`;
      button.dataset.objectChooserIndex = String(index);
      button.setAttribute('role', 'option');
      button.setAttribute('aria-selected', String(dependencies.domains.selectionDomain.has(ref)));
      const name = document.createElement('span');
      const type = document.createElement('small');
      name.textContent = info.name;
      type.textContent = [info.type, info.detail].filter(Boolean).join(' · ');
      button.append(name, type);
      return button;
    }));
    chooser.classList.remove('hidden');
    const presentationRevision = ++objectChooserPresentationRevision;
    if ((0, dependencies.surfaces.isMobile)()) {
      chooser.removeAttribute('style');
      requestAnimationFrame(() => {
        if (presentationRevision !== objectChooserPresentationRevision || chooser.classList.contains('hidden')) return;
        list.querySelector('[role="option"]')?.focus({ preventScroll: true });
      });
      return;
    }
    const bounds = (0, dependencies.platform.$)('map')?.getBoundingClientRect();
    if (!bounds) return;
    const edge = 8;
    requestAnimationFrame(() => {
      if (presentationRevision !== objectChooserPresentationRevision || chooser.classList.contains('hidden')) return;
      const width = chooser.offsetWidth || 300;
      const height = chooser.offsetHeight || 200;
      chooser.style.left = `${(0, dependencies.platform.clamp)(screenPoint[0] + 12, edge, Math.max(edge, bounds.width - width - edge))}px`;
      chooser.style.top = `${(0, dependencies.platform.clamp)(screenPoint[1] + 12, edge, Math.max(edge, bounds.height - height - edge))}px`;
    });
  }

  function chooseObjectCandidate(index, { toggle = false } = {}) {
    const ref = dependencies.projectSession.objectChooserCandidates[Number(index)];
    if (!objectRefSelectable(ref)) return false;
    const mode = toggle || objectChooserIntentMode === 'toggle' ? 'toggle' : 'replace';
    dependencies.domains.selectionUiController.applyIntent(ref, { mode, scope: 'map' });
    return true;
  }

  async function handleObjectSelectionAt(screenPoint, { sourceEvent = dependencies.platform.d3.event, hitRef = null } = {}) {
    const requestRevision = ++objectPickRevision;
    const projectGeneration = dependencies.domains.projectDomain?.getGeneration?.() ?? 0;
    const pickViewRevision = dependencies.mapLayout.viewRevision;
    const source = sourceEvent?.sourceEvent || sourceEvent || {};
    const intentMode = source.ctrlKey || source.metaKey ? 'toggle' : 'replace';
    closeObjectChooser({ cancelPending: false });
    const inputStartedAt = performance.now();
    const performanceBefore = (0, dependencies.renderServices.selectionPerformanceCounterSnapshot)();
    Object.assign(dependencies.rendering.selectionPerformanceMetrics, {
      handlerMs: 0, indexQueryMs: 0, indexedCandidateCount: 0, exactHitTestCount: 0,
      gpuPickMs: 0, pickCacheHit: false, direct: false,
    });
    const normalizedHit = (0, dependencies.selectionServices.normalizeObjectRef)(hitRef);
    const coord = (0, dependencies.mapView.screenToGeo)(screenPoint);
    if (!coord) {
      closeObjectChooser();
      dependencies.domains.selectionUiController.clear({ reason: 'map-background-selection-clear' });
      (0, dependencies.workspaceUiA.closeSurface)('editor', { restoreFocus: false });
      return false;
    }
    const candidates = (await selectableObjectsAt(screenPoint, coord, { seedRefs: [normalizedHit] })).filter(objectRefSelectable);
    if (requestRevision !== objectPickRevision
      || projectGeneration !== (dependencies.domains.projectDomain?.getGeneration?.() ?? 0)
      || pickViewRevision !== dependencies.mapLayout.viewRevision
      || dependencies.projectState.state.tool !== 'select' || dependencies.projectState.state.labelPlacementMode) return false;
    if (!candidates.length) {
      closeObjectChooser({ cancelPending: false });
      dependencies.domains.selectionUiController.clear({ reason: 'map-background-selection-clear' });
      (0, dependencies.workspaceUiA.closeSurface)('editor', { restoreFocus: false });
      return false;
    }
    if (candidates.length > 1) {
      openObjectChooser(candidates, screenPoint, intentMode);
      return true;
    }
    const target = candidates[0];
    dependencies.domains.selectionUiController.applyIntent(target, { mode: intentMode, scope: 'map' });
    dependencies.rendering.selectionPerformanceMetrics.handlerMs = performance.now() - inputStartedAt;
    requestAnimationFrame(() => {
      (0, dependencies.renderServices.publishSelectionPerformanceSample)(inputStartedAt, performanceBefore, `map:${target.key}`);
    });
    return true;
  }

  async function handleMapClick(screenPoint) {
    if (dependencies.projectState.state.spacePanActive || dependencies.projectState.state.tool === 'move') return;
    const rawCoord = (0, dependencies.mapView.screenToGeo)(screenPoint);
    if (!rawCoord) return;
    const pointerType = dependencies.platform.d3.event?.pointerType === 'touch' || dependencies.platform.d3.event?.changedTouches ? 'touch' : 'mouse';
    const coord = rawCoord;
    if (dependencies.projectState.state.labelPlacementMode) {
      (0, dependencies.genericEditingA.addLabelAt)(coord);
      return;
    }
    if (dependencies.projectState.state.tool === 'select' && !dependencies.projectState.state.labelPlacementMode) return handleObjectSelectionAt(screenPoint);
    const territoryCountryPicking = (0, dependencies.territorySelectionB.territorySelectionCountryPickingActive)();
    const needsCountryHit = (dependencies.projectState.state.tool === 'select' && !dependencies.projectState.state.labelPlacementMode) ||
      territoryCountryPicking ||
      (dependencies.projectState.state.tool === 'country-border' && dependencies.projectState.state.boundaryEditPhase === 'selecting') ||
      (dependencies.projectState.state.tool === 'merge-country' && !!dependencies.projectState.state.mergeSourceCountryId);
    const clickedCountry = needsCountryHit && dependencies.projectState.state.layerVisibility.countries
      ? (0, dependencies.pointerInteractionA.countryAtScreenPoint)(screenPoint, coord)
      : null;
    if (territoryCountryPicking) {
      if (clickedCountry) (0, dependencies.territorySelectionC.toggleTerritorySelectionSourceCountry)(clickedCountry.id);
      else (0, dependencies.feedback.setActionStatus)((0, dependencies.territorySelectionB.territorySelectionCountryInstruction)(), 'error', 2600);
      return;
    }
    if (dependencies.projectState.state.tool === 'merge-country' && dependencies.projectState.state.mergeSourceCountryId) {
      if (clickedCountry) (0, dependencies.countryEditingC.toggleMergeTarget)(clickedCountry.id);
      else (0, dependencies.feedback.setActionStatus)('합병 대상을 선택할 수 없습니다. 국가 영토 안쪽을 선택하세요.', 'error', 2600);
      return;
    }
    if (dependencies.projectState.state.tool === 'country-border' && dependencies.projectState.state.boundaryEditPhase === 'selecting') {
      if (clickedCountry) (0, dependencies.countryEditingC.toggleBoundaryEditCountry)(clickedCountry.id);
      else (0, dependencies.feedback.setActionStatus)('접경국을 선택할 수 없습니다. 국가 영토 안쪽을 선택하세요.', 'error', 2600);
      return;
    }
    if (dependencies.projectState.state.tool === 'select' && !dependencies.projectState.state.labelPlacementMode && clickedCountry) return;
    const territorySession = dependencies.projectState.state.territorySelectionSession;
    if (territorySession) {
      if (territorySession.stage !== 'selection' || territorySession.activePhase !== 'drawing'
        || !['line', 'polygon'].includes(territorySession.activeMethod)) return;
      if ((0, dependencies.draftPresentation.editingDraftSnapshot)().inputPhase !== 'draw') return;
      dependencies.domains.editingDomain?.appendDraftScreenPoint?.(screenPoint, pointerType, { dedupe: true });
      return;
    }
    if ((0, dependencies.surfaces.isGenericFeatureDraftTool)(dependencies.projectState.state.tool)) {
      if ((0, dependencies.draftPresentation.editingDraftSnapshot)().inputPhase !== 'draw') return;
      dependencies.domains.editingDomain?.appendDraftScreenPoint?.(screenPoint, pointerType);
      return;
    }
    if (dependencies.projectState.state.tool === 'point') {
      const feature = {
        type: 'Feature', id: (0, dependencies.surfaces.uid)('point'),
        geometry: { type: 'Point', coordinates: coord },
        properties: { name: '', color: dependencies.colorModel.DEFAULT_GENERIC_FEATURE_COLOR, role: 'generic', landBinding: 'none', schemaVersion: dependencies.applicationConstantsA.GENERIC_FEATURE_SCHEMA_VERSION },
      };
      dependencies.objectModelA.genericFeatureService.add(feature);
      dependencies.domains.editingDomain?.setTool('select');
      (0, dependencies.propertyEditingA.applyGenericSelectionIntent)(String(feature.id));
      (0, dependencies.feedback.setActionStatus)('점 기타 객체를 추가했습니다.', 'success');
      return;
    }
    if (dependencies.projectState.state.tool === 'select') dependencies.domains.selectionUiController.clear({ reason: 'map-background-selection-clear' });
  }



  return Object.freeze({
    connect,

    get closeObjectChooser() { return closeObjectChooser; },
    get chooseObjectCandidate() { return chooseObjectCandidate; },
    get createCountryFeature() { return createCountryFeature; },
    get geometryHitsScreenPoint() { return geometryHitsScreenPoint; },
    get handleMapClick() { return handleMapClick; },
    get handleObjectSelectionAt() { return handleObjectSelectionAt; },
    get projectedLineDistance() { return projectedLineDistance; },
  });
}
