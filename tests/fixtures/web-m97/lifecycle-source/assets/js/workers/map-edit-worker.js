'use strict';

function versionedWorkerAssetUrl(relativePath) {
  const url = new URL(relativePath, self.location.href);
  const revision = new URL(self.location.href).searchParams.get('v');
  if (revision) url.searchParams.set('v', revision);
  return url.href;
}

importScripts(
  versionedWorkerAssetUrl('../modules/polygon-geometry.js'),
  versionedWorkerAssetUrl('../modules/territorial-edit-plan.js'),
  versionedWorkerAssetUrl('../vendor/polygon-clipping.min.js'),
);

const calculationsReady = Promise.all([
  'coordinate-bounds.js', 'polygon-clipping-calculation.js',
  'map-edit-preparation-operations.js', 'map-edit-library-calculation.js',
  'map-edit-preview-calculations.js', 'map-edit-territorial-commands.js',
  'map-edit-snap-calculation.js', 'cut-worker-preparation.js',
  'edit-display-preparation.js', 'map-edit-query-calculations.js',
  'boundary-topology.js', 'map-edit-country-commands.js',
].map(name => import(versionedWorkerAssetUrl('../modules/' + name))))
  .then(modules => Object.assign({}, ...modules));
// Report initialization failures through the existing execute error envelope.
calculationsReady.catch(() => {});

const editSources = new Map();
const cutSources = new Map();
const validatedPreviews = new Map();
let sourceRevision = 0;
let previewSequence = 0;
function syncEditSources(patch) {
  for (const key of patch.removedKeys || []) editSources.delete(key);
  for (const row of patch.patches || []) {
    const geometry = Object.hasOwn(row, 'geometry') ? row.geometry : editSources.get(row.key)?.feature.geometry;
    editSources.set(row.key, { kind: row.kind, feature: { ...row.metadata, geometry } });
  }
  if (sourceRevision !== patch.sourceRevision) validatedPreviews.clear();
  sourceRevision = patch.sourceRevision;
}
function applyEntityPatch(features, removedIds) {
  for (const id of removedIds || []) editSources.delete('territorial:' + id);
  for (const feature of features || []) editSources.set('territorial:' + feature.id, { kind: 'territorial', feature });
}
function sourceFeatures(kind) {
  return [...editSources.values()].filter(row => row.kind === kind).map(row => row.feature);
}
const boundaryFeatures = new Map();
let boundaryService = null;
let displayService = null;
let coastTopologyCache = null;
let coastResultCache = new WeakMap();
const pendingResults = new Map();
const cancelled = new Set();
let currentDataRevision = 0;
let projectEpoch = 0;

const featureId = feature => String(feature?.id || '');

function applyPatch(map, features, removedIds) {
  for (const id of removedIds || []) map.delete(String(id));
  for (const feature of features || []) map.set(featureId(feature), feature);
}

function sourceFeature(id, kinds = ['territorial']) {
  for (const kind of kinds) {
    const feature = editSources.get(kind + ':' + id)?.feature;
    if (feature) return feature;
  }
}

function retainReceipt(preparationId, message, epoch) {
  assertRequestCurrent(message, epoch);
  validatedPreviews.set(preparationId, { sourceRevision });
  while (validatedPreviews.size > 8) validatedPreviews.delete(validatedPreviews.keys().next().value);
}

function rememberCutSource(payload) {
  if (payload.source) cutSources.set(payload.sourceKey, payload.source);
  const source = cutSources.get(payload.sourceKey);
  while (cutSources.size > 8) cutSources.delete(cutSources.keys().next().value);
  return source;
}

function assertRequestCurrent(message, epoch, serviceCurrent = () => true) {
  if (epoch !== projectEpoch || !serviceCurrent() || cancelled.has(Number(message.requestId))
    || Number(message.dataRevision || 0) !== currentDataRevision
    || (message.sourceRevision != null && message.sourceRevision !== sourceRevision)) throw new Error('CANCELLED');
}

function requestCheckpoint(message, epoch, sourceSensitive = false, serviceCurrent = () => true) {
  let lastYield = performance.now();
  return async () => {
    if (sourceSensitive || performance.now() - lastYield >= 8) {
      await new Promise(resolve => setTimeout(resolve, 0));
      lastYield = performance.now();
    }
    assertRequestCurrent(message, epoch, serviceCurrent);
  };
}

self.onmessage = async event => {
  const message = event.data || {};
  const epoch = projectEpoch;
  try {
    if (message.type === 'edit-sync') { syncEditSources(message); return; }
    if (message.type === 'rebase') {
      projectEpoch += 1;
      displayService = null; cutSources.clear(); coastTopologyCache = null; coastResultCache = new WeakMap();
      editSources.clear(); validatedPreviews.clear();
      syncEditSources(message.editSources || { patches: [], sourceRevision: 0 });
      boundaryService = null;
      boundaryFeatures.clear();
      const boundaryIds = new Set(message.boundaryIds || []);
      for (const feature of sourceFeatures('territorial')) if (boundaryIds.has(featureId(feature))) boundaryFeatures.set(featureId(feature), feature);
      pendingResults.clear();
      currentDataRevision = Number(message.dataRevision || 0);
      self.postMessage({
        type: 'ready',
        dataRevision: currentDataRevision,
        geometryRevision: Number(message.geometryRevision || currentDataRevision),
        targetRevision: Number(message.targetRevision || 0),
      });
      return;
    }
    if (message.type === 'boundary-invalidate') { boundaryService = null; return; }
    if (message.type === 'boundary-sync') {
      applyEntityPatch(message.features || [], message.removedIds || []);
      applyPatch(boundaryFeatures, message.features || [], message.removedIds || []);
      return;
    }
    if (message.type === 'sync-patch') {
      if (Number(message.dataRevision || 0) < currentDataRevision) return;
      if (message.editSources) syncEditSources(message.editSources);
      applyEntityPatch(message.features || [], message.removedIds || []);
      currentDataRevision = Number(message.dataRevision || currentDataRevision);
      return;
    }
    if (message.type === 'cancel') {
      cancelled.add(Number(message.requestId));
      return;
    }
    if (message.type === 'commit') {
      const pending = pendingResults.get(Number(message.requestId));
      if (pending && Number(pending.dataRevision) === currentDataRevision) {
        applyEntityPatch(pending.result.features, pending.result.removedIds);
        currentDataRevision = Number(message.nextDataRevision || currentDataRevision + 1);
      }
      pendingResults.delete(Number(message.requestId));
      return;
    }
    if (message.type === 'discard') {
      pendingResults.delete(Number(message.requestId));
      return;
    }
    if (message.type !== 'execute') return;
    assertRequestCurrent(message, epoch);
    const {
      createBoundaryPreparation, prepareBoundaryOperation, prepareComponentOperation, calculateLibraryBatch,
      calculateTerritorialPreview, calculateRegionRedraw, calculateDrawnGeometry,
      calculateSnapCandidates, prepareCutInWorker, createEditDisplayPreparation,
      buildBoundaryTopology, calculateCoastAvailability, calculateUncoveredSource,
      calculateTerritorialEdit, createCountryCommandCalculator, calculateEditPreview, calculateCountryPreview,
    } = await calculationsReady;
    assertRequestCurrent(message, epoch);
    const boundaryOperation = message.operation === 'boundary-prepare' || message.operation === 'boundary-move';
    const componentOperation = ['territory-components', 'territory-selection', 'territory-slivers'].includes(message.operation);
    const readOnly = boundaryOperation || componentOperation || message.operation.startsWith('territorial-');
    let result;
    let afterFeatures;
    if (boundaryOperation) {
      boundaryService ||= createBoundaryPreparation();
      const service = boundaryService;
      result = await prepareBoundaryOperation(message.operation, message.payload, [...boundaryFeatures.values()], service,
        requestCheckpoint(message, epoch, false, () => service === boundaryService));
    } else if (componentOperation) {
      result = await prepareComponentOperation(message.operation, message.payload, self.polygonClipping, requestCheckpoint(message, epoch));
    } else if (message.operation === 'territorial-library-batch') {
      result = await calculateLibraryBatch(message.payload, sourceFeatures('territorial').filter(feature => (feature.properties.entityKind === 'general' && !feature.properties.parentId)), sourceFeatures('territorial').filter(feature => !(feature.properties.entityKind === 'general' && !feature.properties.parentId)), self.polygonClipping, requestCheckpoint(message, epoch, true));
    } else if (message.operation === 'territorial-preview') {
      const before = (message.payload.beforeIds || []).map(id => sourceFeature(id, ['territorial', 'generic'])).filter(Boolean);
      result = calculateTerritorialPreview(message.payload, before, self.polygonClipping);
      result.preparationId = 'preview:' + ++previewSequence;
      if (!result.validation.blocking) retainReceipt(result.preparationId, message, epoch);
    } else if (message.operation === 'territorial-region-redraw') {
      const { targetId, draft } = message.payload;
      result = calculateRegionRedraw(sourceFeature(targetId, ['territorial']), draft);
    } else if (message.operation === 'territorial-drawn') {
      result = calculateDrawnGeometry(message.payload, self.polygonClipping);
    } else if (message.operation === 'territorial-snap') {
      const source = rememberCutSource(message.payload);
      if (message.payload.sourceKey && !source) throw new Error('스냅 원본을 다시 준비하세요.');
      result = await calculateSnapCandidates(message.payload, [...editSources.values()].filter(row => ['country', 'territorial', 'generic'].includes(row.kind)).map(row => row.feature), source, requestCheckpoint(message, epoch, true));
    } else if (message.operation === 'territorial-cut') {
      if (!self.d3) importScripts(versionedWorkerAssetUrl('../vendor/d3.min.js'));
      const source = rememberCutSource(message.payload);
      if (!source) throw new Error('분할 원본을 다시 준비하세요.');
      result = prepareCutInWorker({ ...message.payload, source }, self.PandoLabPolygonGeometry, self.d3, self.polygonClipping);
    } else if (message.operation === 'territorial-display') {
      displayService ||= createEditDisplayPreparation();
      const service = displayService;
      result = await service.prepare(message.payload, sourceFeatures('territorial').filter(feature => (feature.properties.entityKind === 'general' && !feature.properties.parentId)), sourceFeatures('territorial').filter(feature => !(feature.properties.entityKind === 'general' && !feature.properties.parentId)),
        requestCheckpoint(message, epoch, true, () => service === displayService), key => editSources.get(key)?.feature);
    } else if (message.operation === 'territorial-validation') {
      const receipt = validatedPreviews.get(message.payload.preparationId);
      if (!receipt || receipt.sourceRevision !== sourceRevision) throw new Error('미리보기 원본이 변경되었습니다. 다시 계산하세요.');
      result = { valid: true, preparationId: message.payload.preparationId, sourceRevision };
    } else if (message.operation === 'territorial-coast-availability') {
      const payload = message.payload.unitId ? { countries: sourceFeatures('territorial').filter(feature => (feature.properties.entityKind === 'general' && !feature.properties.parentId)), unit: sourceFeature(message.payload.unitId, ['territorial']) } : message.payload;
      if (!payload.unit) throw new Error('하위단위를 찾을 수 없습니다.');
      const coastCountries = payload.countries;
      if (!coastTopologyCache || coastTopologyCache.geometries.length !== coastCountries.length || coastCountries.some((feature, index) => coastTopologyCache.geometries[index] !== feature.geometry)) {
        coastTopologyCache = { geometries: coastCountries.map(feature => feature.geometry), topology: buildBoundaryTopology(coastCountries) };
        coastResultCache = new WeakMap();
      }
      const country = coastCountries.find(feature => featureId(feature) === self.PandoLabTerritorialEdit.territorialRootId(payload.unit, id => sourceFeature(id, ['territorial'])));
      const cached = coastResultCache.get(payload.unit.geometry);
      if (cached?.countryId === featureId(country)) result = cached.result;
      else {
        result = calculateCoastAvailability(payload.unit, country, coastTopologyCache.topology, self.polygonClipping);
        coastResultCache.set(payload.unit.geometry, { countryId: featureId(country), result });
      }
    } else if (message.operation === 'territorial-source') {
      const { parent, children } = message.payload.parentId ? {
        parent: sourceFeature(message.payload.parentId),
        children: sourceFeatures('territorial').filter(feature => String(feature.properties?.parentId) === String(message.payload.parentId) && (feature.properties?.entityKind === 'general' && !!feature.properties?.parentId)),
      } : message.payload;
      result = calculateUncoveredSource(parent, children, self.polygonClipping);
    } else if (message.operation === 'territorial-edit') {
      result = calculateTerritorialEdit(message.payload, sourceFeatures('territorial'), self.polygonClipping);
    } else {
      ({ result, afterFeatures } = createCountryCommandCalculator(self.polygonClipping).calculate(message, new Map(sourceFeatures('territorial').filter(feature => (feature.properties.entityKind === 'general' && !feature.properties.parentId)).map(feature => [featureId(feature), feature]))));
    }
    assertRequestCurrent(message, epoch);
    if (message.operation === 'territorial-edit') {
      result.preview = calculateEditPreview(message.payload.operation, result, sourceFeatures('territorial'), self.polygonClipping);
      result.preparationId = 'territorial:' + currentDataRevision + ':' + ++previewSequence;
      if (!result.preview.validation.blocking && !result.impacts.some(impact => impact.kind === 'coast-owner')) retainReceipt(result.preparationId, message, epoch);
    }
    if (!readOnly) {
      result.preview = calculateCountryPreview(message, result, sourceFeatures('territorial').filter(feature => (feature.properties.entityKind === 'general' && !feature.properties.parentId)), afterFeatures, self.polygonClipping);
    }
    assertRequestCurrent(message, epoch);
    if (!readOnly) pendingResults.set(Number(message.requestId), {
      result,
      dataRevision: Number(message.dataRevision || 0),
      geometryRevision: Number(message.geometryRevision || message.dataRevision || 0),
      targetRevision: Number(message.targetRevision || 0),
      jobKey: String(message.jobKey || ''),
    });
    self.postMessage({
      type: 'result',
      ok: true,
      requestId: Number(message.requestId),
      jobKey: String(message.jobKey || ''),
      dataRevision: Number(message.dataRevision || 0),
      geometryRevision: Number(message.geometryRevision || message.dataRevision || 0),
      targetRevision: Number(message.targetRevision || 0),
      result,
    });
  } catch (error) {
    const cancelledRequest = error?.message === 'CANCELLED';
    self.postMessage({
      type: 'result', ok: false, cancelled: cancelledRequest,
      requestId: Number(message.requestId || 0), jobKey: String(message.jobKey || ''),
      dataRevision: Number(message.dataRevision || 0),
      geometryRevision: Number(message.geometryRevision || message.dataRevision || 0),
      targetRevision: Number(message.targetRevision || 0),
      message: cancelledRequest ? '작업을 취소했습니다.' : (error?.message || String(error)),
    });
  } finally {
    if (message.type === 'execute') cancelled.delete(Number(message.requestId || 0));
  }
};
