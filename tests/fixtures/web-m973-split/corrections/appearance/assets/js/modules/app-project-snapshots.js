import { territorialSymbolGroup } from './layer-presentation.js';
import { createGeometrySnapshotPool } from './geometry-versions.js';
import { normalizeTerritorialIdentities } from './territorial-units.js';
import { snapshotTimelineStorage, restoreTimelineStorage } from './timeline-storage.js';
import { staticTimelineViews } from './timeline-static-view.js';
import { assertProjectReferenceIntegrity } from './project-invariants.js';
/** ProjectSnapshots: extracted application responsibility.
 * Dependencies are explicitly wired once by the composition modules.
 * Mutable bindings stay local; exported accessors retain live identity.
 */
export function createProjectSnapshots() {
  const geometrySnapshots = createGeometrySnapshotPool();
  let dependencies;
  let historyStore;
  let historyService;
  function connect(ports) {
    if (dependencies) throw new Error('project-snapshots already connected');
    dependencies = ports;
  }

  function configureDatasetSession(project = null) {
    geometrySnapshots.clear();
    const state = dependencies.projectState.state;
    state.autosaveMode = project?.territorialEntities && project.baseDataset !== dependencies.platformConfigurationA.BASE_DATASET ? 'full' : 'delta';
    const base = (0, dependencies.builtinCountries.materializePristineCountriesSync)().features;
    const byId = new Map(dependencies.builtinBaseline.projectBaseline.baseEntities.map(feature => [String(feature.id), feature]));
    const current = dependencies.territorialModel.entityRepository.list();
    const currentIds = new Set(current.map(feature => String(feature.id)));
    state.historyDirtyEntityIds = new Set(base.filter(feature => !currentIds.has(String(feature.id))).map(feature => String(feature.id)));
    const identities = new Map(dependencies.territorialModel.entityStore.identities().map(feature => [String(feature.id), feature]));
    for (const feature of current) {
      const id=String(feature.id), source=byId.get(id);
      if (!source || JSON.stringify(source.properties) !== JSON.stringify(identities.get(id).properties)
        || !(dependencies.builtinCountries.canonicalCountryStore ? dependencies.builtinCountries.canonicalCountryStore.geometryEquals(id, feature.geometry)
          : JSON.stringify(base.find(row => String(row.id) === id)?.geometry) === JSON.stringify(feature.geometry))) state.historyDirtyEntityIds.add(id);
    }
  }
  function buildEntityDelta() {
    const state=dependencies.projectState.state;
    const current=new Map(dependencies.territorialModel.entityStore.identities().map(feature=>[String(feature.id),feature]));
    const changed=[],removedIds=[];
    for(const id of state.historyDirtyEntityIds) {
      const feature=current.get(String(id));
      if(feature) changed.push(geometrySnapshots.clone(feature)); else removedIds.push(String(id));
    }
    return {changed,removedIds};
  }
  function restoreEntitiesFromSnapshot(snapshot) {
    if (!Array.isArray(snapshot.territorialEntities)) throw new TypeError('이력에는 territorialEntities 배열이 필요합니다.');
    snapshot = prepareEditable(snapshot);
    dependencies.territorialModel.entityStore.restoreProject(snapshot);
    dependencies.projectState.state.historyDirtyEntityIds=new Set(snapshot.historyDirtyEntityIds || []);
    return snapshot;
  }

  function prepareEditable(snapshot) {
    const { geometries, ...fields } = snapshot;
    const candidate = { ...structuredClone(fields), geometries };
    const identities = normalizeTerritorialIdentities(candidate.territorialEntities);
    const storage = restoreTimelineStorage({ schemaVersion: 1, records: candidate.timelineRecords,
      geometries: candidate.geometries }, identities.map(entity => ({ id: entity.id, entityKind: entity.properties.entityKind })),
    { reuse: dependencies.projectState.state.geometries });
    candidate.geometries = storage.geometries.snapshot();
    candidate.timelineRecords = storage.records;
    candidate.hydroEdits = dependencies.hydroModel.normalizeHydroEditCollection(candidate.hydroEdits);
    candidate.genericFeatures = dependencies.modelValidation.normalizeGenericFeatureCollection(candidate.genericFeatures);
    candidate.distributionLayers = dependencies.distributionServices.normalizeDistributionLayers(candidate.distributionLayers);
    const ids = new Set(candidate.distributionLayers.map(layer => layer.id));
    candidate.distributionEntries = dependencies.distributionServices.normalizeDistributionEntries(candidate.distributionEntries, {
      layerExists: id => ids.has(id), cloneGeometry: geometry => geometry,
    });
    assertProjectReferenceIntegrity({ ...candidate,
      territorialEntities: staticTimelineViews(identities, storage.records, storage.geometries) });
    return candidate;
  }

  function historyLabelSettings(labels = dependencies.projectState.state.labels) {
    const saved = {};
    for (const label of labels || []) {
      const key = (0, dependencies.labelPresentation.labelKey)('label', label.id);
      if (Object.hasOwn(dependencies.projectState.state.labelSettings || {}, key)) {
        saved[key] = (0, dependencies.platform.deepClone)(dependencies.projectState.state.labelSettings[key]);
      }
    }
    return saved;
  }

  function changedHistoryLabelIds(targetLabels, currentLabels) {
    const target = new Map((targetLabels || []).map(label => [String(label.id), JSON.stringify(label)]));
    const current = new Map((currentLabels || []).map(label => [String(label.id), JSON.stringify(label)]));
    const ids = new Set([...target.keys(), ...current.keys()]);
    return [...ids].filter(id => target.get(id) !== current.get(id));
  }

  function restoreHistoryLabelSettings(snapshot, currentLabels) {
    const saved = snapshot.historyLabelSettings || {};
    for (const id of changedHistoryLabelIds(snapshot.labels, currentLabels)) {
      const key = (0, dependencies.labelPresentation.labelKey)('label', id);
      if (Object.hasOwn(saved, key)) dependencies.projectState.state.labelSettings[key] = (0, dependencies.platform.deepClone)(saved[key]);
      else delete dependencies.projectState.state.labelSettings[key];
    }
  }

  function historyTerritorialAppearance() {
    const state = dependencies.projectState.state;
    const saved = Object.create(null);
    for (const feature of dependencies.territorialModel.entityRepository.list()) {
      const id = String(feature.id), group = territorialSymbolGroup(feature);
      const entry = {};
      if (Object.hasOwn(state.itemVisibility?.[group] || {}, id)) entry.visibility = { group, value: state.itemVisibility[group][id] };
      const key = `territorial:entity:${id}`;
      if (Object.hasOwn(state.layerPresentation?.objectStyles || {}, key)) entry.style = state.layerPresentation.objectStyles[key];
      if (Object.keys(entry).length) saved[id] = (0, dependencies.platform.deepClone)(entry);
    }
    return saved;
  }

  function restoreHistoryTerritorialAppearance(snapshot, previousEntities) {
    const state = dependencies.projectState.state;
    const previous = new Map(previousEntities.map(feature => [String(feature.id), feature]));
    const appearances = snapshot.historyTerritorialAppearance || {};
    const current = new Map(dependencies.territorialModel.entityRepository.list().map(feature => [String(feature.id), feature]));
    for (const id of new Set([...previous.keys(), ...current.keys()])) {
      // Presentation remains view state for surviving objects. Only an object
      // disappearing or reappearing participates in its deletion history.
      if (previous.has(id) && current.has(id)) continue;
      const group = territorialSymbolGroup(current.get(id) || previous.get(id));
      const key = `territorial:entity:${id}`;
      const saved = current.has(id) && Object.hasOwn(appearances, id) ? appearances[id] : null;
      if (saved?.visibility?.group === group) {
        state.itemVisibility ||= {};
        state.itemVisibility[group] ||= {};
        Object.defineProperty(state.itemVisibility[group], id, { value: saved.visibility.value,
          writable: true, enumerable: true, configurable: true });
      } else delete state.itemVisibility?.[group]?.[id];
      if (saved && Object.hasOwn(saved, 'style')) {
        state.layerPresentation ||= {};
        state.layerPresentation.objectStyles ||= {};
        state.layerPresentation.objectStyles[key] = (0, dependencies.platform.deepClone)(saved.style);
      } else delete state.layerPresentation?.objectStyles?.[key];
    }
  }

  function restoreEditTransactionSnapshot(snapshot) {
    const previousEntities = dependencies.territorialModel.entityRepository.list();
    const changedIds = new Set(dependencies.projectState.state.historyDirtyEntityIds);
    snapshot = restoreEntitiesFromSnapshot(snapshot);
    applySharedProjectFields(snapshot, 'history');
    normalizeProjectObjects();
    restoreHistoryTerritorialAppearance(snapshot, previousEntities);
    const restoredDirtyIds = new Set(dependencies.projectState.state.historyDirtyEntityIds);
    for (const id of dependencies.projectState.state.historyDirtyEntityIds) changedIds.add(String(id));
    (0, dependencies.spatialQuery.markCountryGeometriesChanged)(changedIds);
    dependencies.projectState.state.historyDirtyEntityIds = restoredDirtyIds;
    (0, dependencies.geometryPreview.rebuildBoundaryTopology)(dependencies.projectState.state.tool === 'territorial-border' ? dependencies.projectState.state.boundaryEditEntityIds : dependencies.projectState.state.coastEditCountryId);
    dependencies.domains.renderingDomain?.invalidateCountryPatch?.('country-edit-snapshot-restored');
  }

  function snapshotEditable() {
    return {
      territorialEntities: dependencies.territorialModel.entityStore.identities(),
      historyDirtyEntityIds: [...dependencies.projectState.state.historyDirtyEntityIds],
      historyLabelSettings: historyLabelSettings(),
      historyTerritorialAppearance: historyTerritorialAppearance(),
      ...(0, dependencies.projectServices.pickProjectFields)(dependencies.projectState.state, { scope: 'history', clone: geometrySnapshots.clone }),
    };
  }

  function applySharedProjectFields(source, scope = 'project') {
    const fieldCopy = (key, value) => scope === 'history'
      ? geometrySnapshots.restore(value || [], dependencies.projectState.state[key]) : (0, dependencies.platform.deepClone)(value || []);
    return (0, dependencies.projectServices.applyProjectFields)(dependencies.projectState.state, source, {
      scope,
      clone: dependencies.platform.deepClone,
      normalizers: {
        labelSettings: value => (0, dependencies.platform.deepClone)(value || {}),
        genericFeatures: value => fieldCopy('genericFeatures', value),
        hydroEdits: value => fieldCopy('hydroEdits', value),
        timelineRecords: (_value, current) => current,
        geometries: (_value, current) => current,
        distributionLayers: value => (0, dependencies.platform.deepClone)(value || []),
        distributionEntries: value => fieldCopy('distributionEntries', value),
        distributionSettings: value => ({
          renderMode: value?.renderMode === dependencies.applicationConstantsA.DISTRIBUTION_RENDER_MODES.SINGLE ? dependencies.applicationConstantsA.DISTRIBUTION_RENDER_MODES.SINGLE : dependencies.applicationConstantsA.DISTRIBUTION_RENDER_MODES.OVERLAP,
          activeLayerId: String(value?.activeLayerId || ''),
          boundaryVisible: value?.boundaryVisible !== false,
        }),
        physicalSettings: (value, current) => (0, dependencies.hydroModel.normalizePhysicalSettings)(value || current),
        layerVisibility: (value, current) => (0, dependencies.projectSession.normalizeLayerVisibility)(value, current),
        itemVisibility: value => (0, dependencies.layerTree.normalizeLayerItemState)(value),
        layerPresentation: value => (0, dependencies.modelValidation.normalizeLayerPresentation)(value),
      },
    });
  }

  function normalizeProjectObjects({ history = false } = {}) {
    dependencies.projectState.state.hydroEdits = (0, dependencies.hydroModel.normalizeHydroEditCollection)(dependencies.projectState.state.hydroEdits);
    dependencies.projectState.state.genericFeatures = (0, dependencies.modelValidation.normalizeGenericFeatureCollection)(dependencies.projectState.state.genericFeatures || [], history ? { cloneFeature: feature => ({ ...feature }) } : {});
    dependencies.projectState.state.distributionLayers = (0, dependencies.distributionServices.normalizeDistributionLayers)(dependencies.projectState.state.distributionLayers);
    const distributionLayerIds = new Set(dependencies.projectState.state.distributionLayers.map(layer => layer.id));
    dependencies.projectState.state.distributionEntries = (0, dependencies.distributionServices.normalizeDistributionEntries)(dependencies.projectState.state.distributionEntries, {
      layerExists: id => distributionLayerIds.has(id),
      ...(history ? { cloneGeometry: geometry => geometry } : {}),
    });
    dependencies.projectState.state.distributionSettings = {
      renderMode: dependencies.projectState.state.distributionSettings?.renderMode === dependencies.applicationConstantsA.DISTRIBUTION_RENDER_MODES.SINGLE ? dependencies.applicationConstantsA.DISTRIBUTION_RENDER_MODES.SINGLE : dependencies.applicationConstantsA.DISTRIBUTION_RENDER_MODES.OVERLAP,
      activeLayerId: distributionLayerIds.has(String(dependencies.projectState.state.distributionSettings?.activeLayerId || ''))
        ? String(dependencies.projectState.state.distributionSettings.activeLayerId) : '',
      boundaryVisible: dependencies.projectState.state.distributionSettings?.boundaryVisible !== false,
    };
    dependencies.projectState.state.selectedDistributionLayerId = distributionLayerIds.has(String(dependencies.projectState.state.selectedDistributionLayerId || ''))
      ? String(dependencies.projectState.state.selectedDistributionLayerId)
      : '';
    snapshotTimelineStorage(dependencies.projectState.state.timelineRecords, dependencies.projectState.state.geometries,
      dependencies.territorialModel.entityStore.identities().map(entity => ({ id: entity.id, entityKind: entity.properties.entityKind })));
    const distributionValidation = (0, dependencies.distributionServices.validateDistributionModel)(dependencies.projectState.state.distributionLayers, dependencies.projectState.state.distributionEntries, {
      territorialExists: id => !!dependencies.territorialModel.entityRepository.get(id),
    });
    if (!distributionValidation.ok) throw new Error(distributionValidation.issues[0] || '분포 참조가 올바르지 않습니다.');
    dependencies.projectState.state.layerFolders = (0, dependencies.layerTree.normalizeLayerFolderState)(dependencies.projectState.state.layerFolders);
  }

  function normalizeHistoryMetadata(meta = {}) {
    const primary = dependencies.domains.selectionDomain.primary();
    const info = primary ? (0, dependencies.objectOperationsA.objectDisplayInfo)(primary) : null;
    return {
      id: (0, dependencies.surfaces.uid)('history'),
      timestamp: new Date().toISOString(),
      type: String(meta.type || 'edit'),
      description: String(meta.description || (info ? `${info.name} 편집` : '지도 편집')),
      targetName: String(meta.targetName || info?.name || ''),
      affectedIds: [...new Set((meta.affectedIds || (primary ? [primary.id] : [])).map(String))],
    };
  }

  function restoreEditable(snapshot, { mode = 'history' } = {}) {
    const previousEntities = dependencies.territorialModel.entityRepository.list();
    const previousGeometries = new Map(dependencies.territorialModel.entityRepository.list({ kind: 'general', parentId: '' })
      .map(feature => [String(feature.id), feature.geometry]));
    const currentLabels = (0, dependencies.platform.deepClone)(dependencies.projectState.state.labels || []);
    snapshot = restoreEntitiesFromSnapshot(snapshot);
    applySharedProjectFields(snapshot, 'history');
    restoreHistoryLabelSettings(snapshot, currentLabels);
    dependencies.rendering.gpuMapRenderer.invalidateHydroVisibility();
    (0, dependencies.hydroModel.syncPhysicalControls)();
    normalizeProjectObjects({ history: true });
    restoreHistoryTerritorialAppearance(snapshot, previousEntities);
    const restoredDirtyIds = new Set(dependencies.projectState.state.historyDirtyEntityIds);
    const restoredGeometries = new Map(dependencies.territorialModel.entityRepository.list({ kind: 'general', parentId: '' })
      .map(feature => [String(feature.id), feature.geometry]));
    // Persistent delta IDs describe differences from the built-in dataset, not
    // changes made by this Undo. History reuses unchanged geometry references.
    const changedCountryIds = new Set([...previousGeometries.keys(), ...restoredGeometries.keys()]
      .filter(id => previousGeometries.get(id) !== restoredGeometries.get(id)));
    (0, dependencies.layerTree.pruneLayerItemVisibility)();
    (0, dependencies.countries.scheduleCountryLabelAnchors)(null, 10);
    (0, dependencies.layers.markLayerTreeDirty)();
    dependencies.domains.layerTreeController?.cancelSearch?.();
    dependencies.domains.layerTreeController?.render?.(true);
    dependencies.domains.selectionDomain.clear({ reason: `${mode}-clear-selection` });
    dependencies.projectState.state.coastEditCountryId = null;
    dependencies.projectState.state.coastEditScopeGenericFeatureId = null;
    dependencies.projectState.state.coastEditReturnSelection = null;
    (0, dependencies.countryEditingB.resetBoundaryEditState)();
    (0, dependencies.countryEditingB.resetMergeState)();
    (0, dependencies.countryEditingB.resetGenericFeatureMergeState)();
    (0, dependencies.countryEditingB.resetTerritorialUnitEditState)();
    dependencies.projectState.state.genericFeatureSplitSourceId = null;
    (0, dependencies.countryEditingB.resetTerritoryEditingState)(true);
    dependencies.projectState.state.tool = 'select';
    dependencies.domainControllers.objectPropertyController.show(null);
    (0, dependencies.platform.$)('selectionStatus').textContent = '';
    dependencies.projectState.state.boundaryPreparation?.cancel();
    dependencies.projectState.state.boundaryPreparation = null;
    dependencies.spatialQuery.mapEditClient?.invalidateBoundaryCache?.();

    (0, dependencies.taskUi.updateModeButtons)();
    if (changedCountryIds.size) (0, dependencies.spatialQuery.markCountryGeometriesChanged)(changedCountryIds);
    dependencies.projectState.state.historyDirtyEntityIds = restoredDirtyIds;
  }

  function initializeHistoryStore() {
    (historyStore = {
      get history() { return dependencies.projectState.state.history; },
      set history(value) { dependencies.projectState.state.history = value; },
      get historyMeta() { return dependencies.projectState.state.historyMeta; },
      set historyMeta(value) { dependencies.projectState.state.historyMeta = value; },
      get future() { return dependencies.projectState.state.future; },
      set future(value) { dependencies.projectState.state.future = value; },
      get futureMeta() { return dependencies.projectState.state.futureMeta; },
      set futureMeta(value) { dependencies.projectState.state.futureMeta = value; },
    });

    (historyService = (0, dependencies.projectServices.createHistoryService)({
      store: historyStore,
      maxEntries: dependencies.platformConfigurationB.MAX_HISTORY,
      snapshot: snapshotEditable,
      restore: restoreEditable,
      normalizeMetadata: normalizeHistoryMetadata,
      onRecord: () => dependencies.projectSession.saveState.markContentChanged(),
      onChange: (...args) => dependencies.lifecycleUi.projectUi.syncHistory(...args),
    }));
  }

  return Object.freeze({
    connect,
    initializeHistoryStore,
    get applySharedProjectFields() { return applySharedProjectFields; },
    get buildEntityDelta() { return buildEntityDelta; },
    get configureDatasetSession() { return configureDatasetSession; },
    get historyService() { return historyService; },
    get historyStore() { return historyStore; },
    get normalizeProjectObjects() { return normalizeProjectObjects; },
    get restoreEntitiesFromSnapshot() { return restoreEntitiesFromSnapshot; },
    get restoreEditable() { return restoreEditable; },
    get restoreEditTransactionSnapshot() { return restoreEditTransactionSnapshot; },
    get snapshotEditable() { return snapshotEditable; },
  });
}
