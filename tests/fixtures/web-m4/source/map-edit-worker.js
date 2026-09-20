'use strict';

function versionedWorkerAssetUrl(relativePath) {
  const url = new URL(relativePath, self.location.href);
  const revision = new URL(self.location.href).searchParams.get('v');
  if (revision) url.searchParams.set('v', revision);
  return url.href;
}

importScripts(
  versionedWorkerAssetUrl('../modules/country-geometry.js'),
  versionedWorkerAssetUrl('../modules/territorial-edit-plan.js'),
  versionedWorkerAssetUrl('../vendor/polygon-clipping.min.js'),
);

const {
  hasCanonicalCountryWinding,
  normalizeCountryGeometry,
  ringSignedArea,
} = self.PandoLabCountryGeometry;

const countries = new Map();
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

const clone = value => value == null ? value : JSON.parse(JSON.stringify(value));
const featureId = feature => String(feature?.id || '');

function multiCoordinates(geometry) {
  if (!geometry) return [];
  if (geometry.type === 'Polygon') return [geometry.coordinates || []];
  return geometry.type === 'MultiPolygon' ? geometry.coordinates || [] : [];
}

function quantizePolygonCoordinates(value, precision) {
  const factor = 10 ** precision;
  const visit = item => {
    if (Array.isArray(item) && item.length >= 2 && Number.isFinite(Number(item[0])) && Number.isFinite(Number(item[1]))) {
      return [Math.round(Number(item[0]) * factor) / factor, Math.round(Number(item[1]) * factor) / factor];
    }
    return Array.isArray(item) ? item.map(visit) : item;
  };
  return visit(value);
}

function clippingOperation(method, ...inputs) {
  try {
    return self.polygonClipping[method](...inputs);
  } catch (error) {
    const message = String(error?.message || error || '');
    if (!/SweepLine tree|Unable to find segment/i.test(message)) throw error;
    for (const precision of [9, 8, 7, 6]) {
      try {
        return self.polygonClipping[method](...inputs.map(input => quantizePolygonCoordinates(input, precision)));
      } catch (retryError) {
        if (!/SweepLine tree|Unable to find segment/i.test(String(retryError?.message || retryError || ''))) throw retryError;
      }
    }
    throw error;
  }
}

function area(value) {
  const polygons = value?.type ? multiCoordinates(value) : (Array.isArray(value) ? value : []);
  return polygons.reduce((total, polygon) => total + Math.max(0,
    Math.abs(ringSignedArea(polygon[0] || [])) - (polygon || []).slice(1).reduce((sum, ring) => sum + Math.abs(ringSignedArea(ring)), 0),
  ), 0);
}

const geometryBoundsCache = new WeakMap();
function geometryBounds(geometry) {
  if (geometry && geometryBoundsCache.has(geometry)) return geometryBoundsCache.get(geometry);
  const bounds = [Infinity, Infinity, -Infinity, -Infinity];
  const visit = value => {
    if (!Array.isArray(value)) return;
    if (value.length >= 2 && Number.isFinite(Number(value[0])) && Number.isFinite(Number(value[1]))) {
      bounds[0] = Math.min(bounds[0], Number(value[0])); bounds[1] = Math.min(bounds[1], Number(value[1]));
      bounds[2] = Math.max(bounds[2], Number(value[0])); bounds[3] = Math.max(bounds[3], Number(value[1]));
      return;
    }
    value.forEach(visit);
  };
  visit(geometry?.coordinates);
  if (geometry) geometryBoundsCache.set(geometry, bounds);
  return bounds;
}

function boundsOverlap(left, right) {
  return left[0] <= right[2] && left[2] >= right[0] && left[1] <= right[3] && left[3] >= right[1];
}

function polygonBounds(polygon) {
  return geometryBounds({ type: 'Polygon', coordinates: polygon });
}

function areaPolygonsNearFeatures(features, areaCoordinates) {
  const areaBounds = geometryBounds({ type: 'MultiPolygon', coordinates: areaCoordinates });
  const polygons = [];
  for (const feature of features) {
    for (const polygon of multiCoordinates(feature.geometry)) {
      if (boundsOverlap(polygonBounds(polygon), areaBounds)) polygons.push([polygon]);
    }
  }
  return polygons;
}

// Translate before summing: tiny slivers at large longitude/latitude otherwise
// lose their area to cancellation. This is an existence test, not a tolerance.
function localRingArea(ring, scaleX = 1, scaleY = 1) {
  const origin = ring?.[0];
  if (!origin) return 0;
  let sum = 0;
  for (let i = 1; i < ring.length; i += 1) {
    sum += (ring[i - 1][0] - origin[0]) * (ring[i][1] - origin[1])
      - (ring[i][0] - origin[0]) * (ring[i - 1][1] - origin[1]);
  }
  return Math.abs(sum / 2) * scaleX * scaleY;
}

function positivePolygonArea(polygon) {
  return localRingArea(polygon[0]) - polygon.slice(1).reduce((sum, ring) => sum + localRingArea(ring), 0) > 0;
}

function sliverAreaM2(polygon) {
  const bounds = polygonBounds(polygon);
  // Skip uncertain wraps/polar geometry rather than approximating it away.
  if (bounds[2] - bounds[0] > 1 || bounds[3] - bounds[1] > 1 || Math.max(Math.abs(bounds[1]), Math.abs(bounds[3])) > 80) return Infinity;
  const meters = 6371008.8 * Math.PI / 180;
  const x = meters * Math.cos((bounds[1] + bounds[3]) / 2 * Math.PI / 180);
  return Math.max(0, localRingArea(polygon[0], x, meters)
    - polygon.slice(1).reduce((sum, ring) => sum + localRingArea(ring, x, meters), 0));
}

function sharesBoundary(polygon, others) {
  // Only near-collinear overlapping edges, never proximity or point contact.
  const epsilon = 1e-11;
  for (const ring of polygon) for (let i = 1; i < ring.length; i += 1) {
    const a = ring[i - 1], b = ring[i];
    const dx = b[0] - a[0], dy = b[1] - a[1], length = Math.hypot(dx, dy);
    if (length <= epsilon) continue;
    for (const other of others) for (const otherRing of other) for (let j = 1; j < otherRing.length; j += 1) {
      const c = otherRing[j - 1], d = otherRing[j];
      if (Math.abs(dx * (c[1] - a[1]) - dy * (c[0] - a[0])) / length > epsilon
        || Math.abs(dx * (d[1] - a[1]) - dy * (d[0] - a[0])) / length > epsilon) continue;
      const start = ((c[0] - a[0]) * dx + (c[1] - a[1]) * dy) / length;
      const end = ((d[0] - a[0]) * dx + (d[1] - a[1]) * dy) / length;
      if (Math.min(length, Math.max(start, end)) - Math.max(0, Math.min(start, end)) > epsilon) return true;
    }
  }
  return false;
}

function subtractAreaFromGeometry(geometry, areaCoordinates, { riverComponents = [], slivers = null } = {}) {
  const areaBounds = geometryBounds({ type: 'MultiPolygon', coordinates: areaCoordinates });
  const polygons = [];
  let affected = false;
  for (const [polygonIndex, polygon] of multiCoordinates(geometry).entries()) {
    if (!boundsOverlap(polygonBounds(polygon), areaBounds)) {
      polygons.push(clone(polygon));
      continue;
    }
    const source = [polygon];
    if (!clippingOperation('intersection', source, areaCoordinates).some(positivePolygonArea)) {
      polygons.push(clone(polygon));
      continue;
    }
    affected = true;
    const pieces = clippingOperation('difference', source, areaCoordinates);
    const component = riverComponents.find(row => row.polygonIndex === polygonIndex);
    const unselected = component?.unselectedGeometries?.flatMap(multiCoordinates);
    for (const piece of pieces) {
      const size = slivers && unselected ? sliverAreaM2(piece) : Infinity;
      if (size > 0 && size <= 1 && slivers.areaM2 + size <= 10
        && !clippingOperation('intersection', [piece], unselected).some(positivePolygonArea)
        && sharesBoundary(piece, areaCoordinates) && !sharesBoundary(piece, unselected)) {
        slivers.polygons.push(piece);
        slivers.areaM2 += size;
      } else polygons.push(piece);
    }
  }
  return { affected, geometry: normalizeCountryGeometry(polygons) };
}

function unionAreaWithGeometry(geometry, areaCoordinates) {
  const areaBounds = geometryBounds({ type: 'MultiPolygon', coordinates: areaCoordinates });
  const untouched = [];
  const nearby = [];
  for (const polygon of multiCoordinates(geometry)) {
    if (boundsOverlap(polygonBounds(polygon), areaBounds)) nearby.push([polygon]);
    else untouched.push(clone(polygon));
  }
  const merged = nearby.length ? clippingOperation('union', ...nearby, areaCoordinates) : clone(areaCoordinates);
  return normalizeCountryGeometry([...untouched, ...merged]);
}

function boundaryLength(geometry) {
  let length = 0;
  for (const polygon of multiCoordinates(geometry)) for (const ring of polygon || []) {
    for (let index = 0; index < ring.length - 1; index += 1) {
      let dx = Number(ring[index + 1][0]) - Number(ring[index][0]);
      if (dx > 180) dx -= 360;
      if (dx < -180) dx += 360;
      length += Math.hypot(dx, Number(ring[index + 1][1]) - Number(ring[index][1]));
    }
  }
  return length;
}

function geometryValid(geometry) {
  const polygons = multiCoordinates(geometry);
  if (!polygons.length || !hasCanonicalCountryWinding(geometry)) return false;
  return polygons.every(polygon => polygon?.length && polygon.every(ring => {
    if (!Array.isArray(ring) || ring.length < 4) return false;
    const first = ring[0], last = ring[ring.length - 1];
    if (!first || !last || Math.abs(first[0] - last[0]) > 1e-7 || Math.abs(first[1] - last[1]) > 1e-7) return false;
    return new Set(ring.slice(0, -1).map(coord => `${Number(coord[0]).toFixed(8)},${Number(coord[1]).toFixed(8)}`)).size >= 3
      && Math.abs(ringSignedArea(ring)) > 1e-14;
  }));
}

function unionFeatures(map, ids) {
  const inputs = [...ids].map(id => map.get(String(id))?.geometry?.coordinates).filter(Boolean);
  return inputs.length ? clippingOperation('union', ...inputs) : [];
}

function captureBaseline(map, affectedIds) {
  const affected = new Set([...affectedIds].map(String));
  const overlaps = new Map();
  const all = [...map.values()];
  for (const left of all) {
    const leftId = featureId(left);
    if (!affected.has(leftId)) continue;
    const leftBounds = geometryBounds(left.geometry);
    for (const right of all) {
      const rightId = featureId(right);
      if (!rightId || rightId === leftId || !boundsOverlap(leftBounds, geometryBounds(right.geometry))) continue;
      const key = leftId < rightId ? `${leftId}|${rightId}` : `${rightId}|${leftId}`;
      if (!overlaps.has(key)) overlaps.set(key, area(clippingOperation('intersection', left.geometry.coordinates, right.geometry.coordinates)));
    }
  }
  return {
    union: unionFeatures(map, affected),
    overlaps,
    boundaryLength: all.filter(feature => affected.has(featureId(feature))).reduce((sum, feature) => sum + boundaryLength(feature.geometry), 0),
  };
}

function validateResult(map, affectedIds, baseline, { allowAreaChange = false } = {}) {
  const affected = new Set([...affectedIds].map(String));
  const ids = [...map.keys()];
  if (ids.some(id => !id) || new Set(ids).size !== ids.length) throw new Error('국가 ID가 비어 있거나 중복되었습니다.');
  for (const id of affected) {
    const feature = map.get(id);
    if (feature && !geometryValid(feature.geometry)) throw new Error(`${feature.properties?.name || id}의 경계가 유효하지 않습니다.`);
  }
  const tolerance = Math.max(1e-8, Number(baseline.boundaryLength || 0) * 2e-7);
  const tested = new Set();
  for (const id of affected) {
    const feature = map.get(id);
    if (!feature) continue;
    const featureBounds = geometryBounds(feature.geometry);
    for (const other of map.values()) {
      const otherId = featureId(other);
      if (otherId === id || !boundsOverlap(featureBounds, geometryBounds(other.geometry))) continue;
      const key = id < otherId ? `${id}|${otherId}` : `${otherId}|${id}`;
      if (tested.has(key)) continue;
      tested.add(key);
      const overlap = area(clippingOperation('intersection', feature.geometry.coordinates, other.geometry.coordinates));
      if (overlap > Number(baseline.overlaps.get(key) || 0) + tolerance) throw new Error('편집 결과에 새로운 국가 간 중첩이 생겼습니다. 범위를 조정하세요.');
    }
  }
  if (!allowAreaChange) {
    const changed = area(clippingOperation('xor', baseline.union, unionFeatures(map, affected)));
    if (changed > tolerance) throw new Error('편집 영역에 새로운 빈틈 또는 면적 변화가 생겼습니다. 범위를 조정하세요.');
  }
}

function applyPatch(map, features, removedIds, { cloneValues = false } = {}) {
  for (const id of removedIds || []) map.delete(String(id));
  for (const feature of features || []) map.set(featureId(feature), cloneValues ? clone(feature) : feature);
}

function executeAnnex(message, working) {
  const targetId = String(message.targetId || '');
  const allowUnclaimed = message.allowUnclaimed === true;
  const donorIds = [...new Set((message.donorIds || []).map(String))].filter(id => id && id !== targetId);
  const target = working.get(targetId);
  const donors = donorIds.map(id => working.get(id)).filter(Boolean);
  if (!target?.geometry || donors.length !== donorIds.length || (!allowUnclaimed && !donors.length)) throw new Error('편입할 국가 데이터를 찾을 수 없습니다. 대상을 다시 선택하세요.');
  let transferred = multiCoordinates(message.transferredGeometry);
  const transferredArea = area(transferred);
  if (!transferred.some(positivePolygonArea)) throw new Error('편입할 유효한 영토가 없습니다.');
  const donorInputs = areaPolygonsNearFeatures(donors, transferred);
  const donorUnion = donorInputs.length ? clippingOperation('union', ...donorInputs) : [];
  if (!allowUnclaimed && area(clippingOperation('difference', transferred, donorUnion)) > Math.max(1e-10, transferredArea * 1e-10)) {
    throw new Error('선택 영역이 영토를 가져올 국가 밖으로 벗어났습니다. 범위를 다시 지정하세요.');
  }
  const updates = [];
  const removedIds = [];
  const affectedDonorIds = [];
  const slivers = { polygons: [], areaM2: 0 };
  for (const donor of donors) {
    if (cancelled.has(message.requestId)) throw new Error('CANCELLED');
    const id = featureId(donor);
    const riverComponents = Array.isArray(message.riverSliverContext)
      ? message.riverSliverContext.filter(row => row.donorId === id && Number.isInteger(row.polygonIndex)
        && Array.isArray(row.unselectedGeometries)) : [];
    const subtraction = subtractAreaFromGeometry(donor.geometry, transferred, { riverComponents, slivers });
    if (!subtraction.affected) continue;
    affectedDonorIds.push(id);
    const remainder = subtraction.geometry;
    if (!remainder) removedIds.push(id);
    else {
      const next = clone(donor);
      next.geometry = remainder;
      updates.push(next);
    }
  }
  if (!affectedDonorIds.length && !allowUnclaimed) throw new Error('선택 영역과 겹치는 국가가 없습니다.');
  if (slivers.polygons.length) transferred = clippingOperation('union', transferred, slivers.polygons);
  const nextTarget = clone(target);
  nextTarget.geometry = unionAreaWithGeometry(target.geometry, transferred);
  updates.unshift(nextTarget);
  const affectedIds = new Set([targetId, ...affectedDonorIds]);
  const baseline = captureBaseline(working, affectedIds);
  applyPatch(working, updates, removedIds);
  validateResult(working, new Set([targetId, ...affectedDonorIds]), baseline, { allowAreaChange: allowUnclaimed });
  return {
    features: updates, removedIds, affectedIds: [targetId, ...affectedDonorIds], affectedDonorIds,
    transferredArea: area(transferred), transferredGeometry: { type: 'MultiPolygon', coordinates: transferred },
    autoIncludedSlivers: { count: slivers.polygons.length, areaM2: slivers.areaM2 },
  };
}

function executeAnnexBatch(message, working) {
  const affectedIds = new Set();
  const affectedDonorIds = new Set();
  const transferredAreas = [];
  for (const item of message.operations || []) {
    const result = executeAnnex({ ...message, ...item }, working);
    for (const id of result.affectedIds || []) affectedIds.add(String(id));
    for (const id of result.affectedDonorIds || []) affectedDonorIds.add(String(id));
    transferredAreas.push(Number(result.transferredArea || 0));
  }
  const features = [...affectedIds].map(id => working.get(id)).filter(Boolean).map(clone);
  const removedIds = [...affectedIds].filter(id => !working.has(id));
  return {
    features,
    removedIds,
    affectedIds: [...affectedIds],
    affectedDonorIds: [...affectedDonorIds],
    transferredAreas,
  };
}

function executeMerge(message, working) {
  const sourceId = String(message.sourceId || '');
  const targetIds = [...new Set((message.targetIds || []).map(String))].filter(id => id && id !== sourceId);
  const source = working.get(sourceId);
  const targets = targetIds.map(id => working.get(id)).filter(Boolean);
  if (!source?.geometry || targets.length !== targetIds.length || !targets.length) throw new Error('합병할 국가를 찾을 수 없습니다. 대상을 다시 선택하세요.');
  const affectedIds = new Set([sourceId, ...targetIds]);
  const baseline = captureBaseline(working, affectedIds);
  const next = clone(source);
  next.geometry = normalizeCountryGeometry(clippingOperation('union', source.geometry.coordinates, ...targets.map(feature => feature.geometry.coordinates)));
  applyPatch(working, [next], targetIds);
  validateResult(working, affectedIds, baseline);
  return { features: [next], removedIds: targetIds, affectedIds: [...affectedIds], seamless: true };
}

function executeNewCountry(message, working) {
  const sourceIds = [...new Set((message.sourceIds || []).map(String))].filter(Boolean);
  const sources = sourceIds.map(id => working.get(id)).filter(Boolean);
  const newFeature = clone(message.newFeature);
  const newId = featureId(newFeature);
  if (!sources.length || sources.length !== sourceIds.length || !newFeature?.geometry || !newId) throw new Error('새 국가의 원본 국가 데이터를 찾을 수 없습니다.');
  const transferred = multiCoordinates(message.transferredGeometry);
  const transferredArea = area(transferred);
  const sourceInputs = areaPolygonsNearFeatures(sources, transferred);
  const sourceUnion = sourceInputs.length ? clippingOperation('union', ...sourceInputs) : [];
  if (area(clippingOperation('difference', transferred, sourceUnion)) > Math.max(1e-10, transferredArea * 1e-10)) {
    throw new Error('선택 영역이 영토를 가져올 국가 밖으로 벗어났습니다.');
  }
  const updates = [];
  const removedIds = [];
  const affectedSourceIds = [];
  for (const source of sources) {
    const id = featureId(source);
    const subtraction = subtractAreaFromGeometry(source.geometry, transferred);
    if (!subtraction.affected) continue;
    affectedSourceIds.push(id);
    const remainder = subtraction.geometry;
    if (!remainder) removedIds.push(id);
    else {
      const next = clone(source);
      next.geometry = remainder;
      updates.push(next);
    }
  }
  if (!affectedSourceIds.length) throw new Error('선택 영역과 겹치는 국가가 없습니다.');
  const baseline = captureBaseline(working, new Set(affectedSourceIds));
  newFeature.geometry = normalizeCountryGeometry(transferred);
  updates.push(newFeature);
  applyPatch(working, updates, removedIds);
  const affectedIds = new Set([...affectedSourceIds, newId]);
  validateResult(working, affectedIds, baseline);
  return { features: updates, removedIds, affectedIds: [...affectedIds], affectedSourceIds, transferredArea, newCountryId: newId };
}

self.onmessage = async event => {
  const message = event.data || {};
  try {
    if (message.type === 'edit-sync') { syncEditSources(message); return; }
    if (message.type === 'rebase') {
      displayService = null; cutSources.clear(); coastTopologyCache = null; coastResultCache = new WeakMap();
      editSources.clear(); validatedPreviews.clear();
      syncEditSources(message.editSources || { patches: [], sourceRevision: 0 });
      boundaryService = null;
      boundaryFeatures.clear();
      for (const feature of message.boundaryFeatures || message.features || []) boundaryFeatures.set(featureId(feature), feature);
      countries.clear();
      for (const feature of message.features || []) countries.set(featureId(feature), feature);
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
      applyPatch(boundaryFeatures, message.features || [], message.removedIds || []);
      return;
    }
    if (message.type === 'sync-patch') {
      if (Number(message.dataRevision || 0) < currentDataRevision) return;
      if (message.editSources) syncEditSources(message.editSources);
      applyPatch(countries, message.features || [], message.removedIds || []);
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
        applyPatch(countries, pending.result.features, pending.result.removedIds);
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
    if (Number(message.dataRevision || 0) !== currentDataRevision || cancelled.has(Number(message.requestId))) {
      throw new Error('CANCELLED');
    }
    const boundaryOperation = message.operation === 'boundary-prepare' || message.operation === 'boundary-move';
    const componentOperation = ['territory-components', 'territory-selection', 'territory-slivers'].includes(message.operation);
    const readOnly = boundaryOperation || componentOperation || message.operation.startsWith('territorial-');
    if (message.sourceRevision != null && message.sourceRevision !== sourceRevision) throw new Error('CANCELLED');
    const working = readOnly ? null : new Map(countries);
    let result;
    if (boundaryOperation) {
      if (!boundaryService) {
        const { createBoundaryPreparation } = await import(versionedWorkerAssetUrl('../modules/boundary-preparation.js'));
        boundaryService ||= createBoundaryPreparation();
      }
      const service = boundaryService;
      let lastYield = performance.now();
      const checkpoint = async () => {
        if (performance.now() - lastYield >= 8) { await new Promise(resolve => setTimeout(resolve, 0)); lastYield = performance.now(); }
        if (service !== boundaryService || cancelled.has(Number(message.requestId)) || Number(message.dataRevision) !== currentDataRevision) throw new Error('CANCELLED');
      };
      await service.sync([...boundaryFeatures.values()], checkpoint);
      result = message.operation === 'boundary-prepare' ? await service.prepare(message.payload, checkpoint) : service.move(message.payload);
    } else if (componentOperation) {
      const { createTerritoryComponentPlan } = await import(versionedWorkerAssetUrl('../modules/territory-component-plan.js'));
      let lastYield = performance.now();
      const plan = createTerritoryComponentPlan({ clipper: self.polygonClipping, normalize: normalizeCountryGeometry,
        checkpoint: async () => {
          if (performance.now() - lastYield >= 8) {
            await new Promise(resolve => setTimeout(resolve, 0));
            lastYield = performance.now();
          }
          if (cancelled.has(Number(message.requestId)) || Number(message.dataRevision) !== currentDataRevision) throw new Error('CANCELLED');
        },
      });
      const method = message.operation === 'territory-components' ? 'prepare'
        : message.operation === 'territory-selection' ? 'selection' : 'slivers';
      result = await plan[method](message.payload);
    } else if (message.operation === 'territorial-library-batch') {
      const [{ createCountryImportMergePlanner }, { validateCollection }, { geometryAreaKm2 }] = await Promise.all([
        import(versionedWorkerAssetUrl('../modules/import-service.js')),
        import(versionedWorkerAssetUrl('../modules/gis-geometry-validation.js')),
        import(versionedWorkerAssetUrl('../modules/geometry-metrics.js')),
      ]);
      const planner = createCountryImportMergePlanner({ clipper: self.polygonClipping, clone: structuredClone,
        featureCountryId: featureId, countryName: feature => feature.properties?.name || featureId(feature),
        geometryBounds, boundsOverlap, normalizeGeometry: normalizeCountryGeometry, geometryCoordinates: multiCoordinates,
        planarArea: area, areaKm2: geometryAreaKm2, validateCountryCollection: () => ({ overlapAreaKm2: 0 }) });
      const originals = sourceFeatures('country');
      let draft = { type: 'FeatureCollection', features: originals };
      const affected = new Set(), donors = new Set(), transfers = [], impacts = [];
      let deleted = 0;
      const merge = async (feature, geometry = feature.geometry) => {
        const plan = await planner(draft, { type: 'FeatureCollection', features: [feature] }, 'territory-replacement');
        for (const id of plan.affectedIds) {
          if (editSources.get(`country:${id}`)?.feature.properties?.locked) throw new Error(`${id}: 잠긴 국가의 영토를 변경할 수 없습니다.`);
          affected.add(id);
        }
        for (const id of plan.donorIds) {
          donors.add(id);
          const before = draft.features.find(item => featureId(item) === id), after = plan.countriesData.features.find(item => featureId(item) === id);
          impacts.push({ id, name: before.properties?.name || id, area: geometryAreaKm2(before.geometry) - geometryAreaKm2(after?.geometry), deleted: !after });
        }
        deleted += plan.counts.deleted;
        transfers.push({ targetId: featureId(feature), geometry, donorIds: plan.donorIds });
        draft = plan.countriesData;
        await new Promise(resolve => setTimeout(resolve, 0));
        if (message.sourceRevision !== sourceRevision || cancelled.has(Number(message.requestId))) throw new Error('CANCELLED');
      };
      for (const feature of message.payload.countries) await merge(feature);
      const units = message.payload.units, unitIds = new Set(units.map(featureId)), groups = new Map();
      for (const unit of units.filter(unit => unit.properties.unitType === 'subunit' && !unitIds.has(String(unit.properties.parentId)))) {
        const id = String(unit.properties.sovereignId);
        if (!groups.has(id)) groups.set(id, []);
        groups.get(id).push(unit.geometry);
      }
      for (const [id, geometries] of groups) {
        const owner = draft.features.find(feature => featureId(feature) === id);
        if (!owner) throw new Error('소속 국가가 영토 변경으로 사라집니다. 소속을 다시 선택하세요.');
        const geometry = normalizeCountryGeometry(self.polygonClipping.union(...geometries.map(multiCoordinates)));
        if (!self.polygonClipping.difference(multiCoordinates(geometry), multiCoordinates(owner.geometry)).length) continue;
        impacts.push({ id, name: owner.properties?.name || id, expansion: true });
        await merge({ ...owner, geometry: normalizeCountryGeometry(self.polygonClipping.union(multiCoordinates(owner.geometry), multiCoordinates(geometry))) }, geometry);
      }
      const byId = new Map([...draft.features, ...sourceFeatures('territorial'), ...units].map(feature => [featureId(feature), feature]));
      for (const unit of units.filter(unit => unit.properties.unitType === 'subunit')) {
        const parent = byId.get(String(unit.properties.parentId));
        if (!parent || !draft.features.some(country => featureId(country) === String(unit.properties.sovereignId))
          || self.polygonClipping.difference(multiCoordinates(unit.geometry), multiCoordinates(parent.geometry)).length) throw new Error(`${unit.properties.name}: 상위 단위에 포함되지 않습니다.`);
      }
      if (validateCollection(draft, [...affected]).overlapAreaKm2 > 0.001) throw new Error('영토 변경 후 국가 간 중첩이 남아 추가할 수 없습니다.');
      const kept = new Set(draft.features.map(featureId));
      result = { features: draft.features.filter(feature => affected.has(featureId(feature))), removedIds: originals.filter(feature => !kept.has(featureId(feature))).map(featureId),
        affectedIds: [...affected], transfers, impacts, donorIds: [...donors], deleted };
    } else if (message.operation === 'territorial-preview') {
      const [{ buildGeometryPreview }, { validateGeometry }] = await Promise.all([
        import(versionedWorkerAssetUrl('../modules/geometry-preview.js')),
        import(versionedWorkerAssetUrl('../modules/geometry-validation.js')),
      ]);
      const { beforeIds = [], afterFeatures = [], removedIds = [], operation, transferredGeometry } = message.payload;
      const beforeFeatures = beforeIds.map(id => editSources.get(`territorial:${id}`)?.feature || editSources.get(`generic:${id}`)?.feature).filter(Boolean);
      if (beforeFeatures.length !== beforeIds.length || beforeFeatures.some(feature => feature.properties?.locked)) throw new Error('편집 대상이 변경되었거나 잠겨 있습니다.');
      const issues = afterFeatures.flatMap(validateGeometry);
      result = buildGeometryPreview({ operation, beforeFeatures, afterFeatures, removedIds, transferredGeometry, clipper: self.polygonClipping });
      result.validation = { issues, blocking: issues.some(issue => issue.severity !== 'warning') };
      result.preparationId = `preview:${++previewSequence}`;
      if (!result.validation.blocking) {
        validatedPreviews.set(result.preparationId, { sourceRevision });
        while (validatedPreviews.size > 8) validatedPreviews.delete(validatedPreviews.keys().next().value);
      }
    } else if (message.operation === 'territorial-region-merge') {
      const { createTerritorialGeometryKernel } = await import(versionedWorkerAssetUrl('../modules/territorial-geometry.js'));
      const source = editSources.get(`territorial:${message.payload.targetId}`)?.feature;
      const targets = message.payload.targetIds.map(id => editSources.get(`territorial:${id}`)?.feature);
      if (!source || targets.some(feature => !feature) || [source, ...targets].some(feature => feature.properties?.locked || feature.properties?.unitType !== 'region')) throw new Error('합칠 지방이 변경되었거나 잠겨 있습니다.');
      result = createTerritorialGeometryKernel(self.polygonClipping).mergeUnits(source, targets);
    } else if (message.operation === 'territorial-region-redraw') {
      const { targetId, containerId, siblingIds, draft } = message.payload;
      const source = editSources.get(`territorial:${targetId}`)?.feature;
      const container = editSources.get(`territorial:${containerId}`)?.feature || editSources.get(`country:${containerId}`)?.feature;
      if (!source || !container || source.properties?.locked || source.properties?.unitType !== 'region') throw new Error('지방 편집 대상이 변경되었습니다.');
      const geometry = normalizeCountryGeometry({ type: 'MultiPolygon', coordinates: self.polygonClipping.intersection(draft.coordinates, container.geometry.coordinates) });
      if (!geometry) throw new Error('그린 영역이 상위 영역 안에 없습니다.');
      for (const id of siblingIds) {
        const sibling = editSources.get(`territorial:${id}`)?.feature;
        if (!sibling) throw new Error('다른 지방이 변경되었습니다.');
        if (area(self.polygonClipping.intersection(geometry.coordinates, sibling.geometry.coordinates)) > 1e-9) throw new Error('다른 지방과 영역이 겹칩니다.');
      }
      result = { feature: { ...source, geometry } };
    } else if (message.operation === 'territorial-drawn') {
      const { validateGeometry } = await import(versionedWorkerAssetUrl('../modules/geometry-validation.js'));
      const draft = normalizeCountryGeometry(message.payload.draft);
      if (!draft) throw new Error('그린 영역을 닫힌 Polygon으로 만들 수 없습니다.');
      const issues = validateGeometry({ type: 'Feature', id: 'draft', properties: {}, geometry: draft });
      if (issues.length) throw new Error(issues[0].message);
      const source = message.payload.source;
      const geometry = source ? normalizeCountryGeometry({ type: 'MultiPolygon', coordinates: self.polygonClipping.intersection(multiCoordinates(draft), multiCoordinates(source)) }) : draft;
      if (!geometry) throw new Error('그린 영역이 기준 영역 안에 없습니다.');
      result = { geometry };
    } else if (message.operation === 'territorial-snap') {
      const { geometrySegmentIndex } = await import(versionedWorkerAssetUrl('../modules/geometry-segment-index.js'));
      const { coordinate, margin, activeOwnerIds = [], sourceKey, source } = message.payload;
      if (source) cutSources.set(sourceKey, source);
      const sourceGeometry = cutSources.get(sourceKey);
      if (sourceKey && !sourceGeometry) throw new Error('스냅 원본을 다시 준비하세요.');
      while (cutSources.size > 8) cutSources.delete(cutSources.keys().next().value);
      const bounds = [coordinate[0] - margin, coordinate[1] - margin, coordinate[0] + margin, coordinate[1] + margin];
      const candidates = [], seenVertices = new Set();
      const active = new Set(activeOwnerIds);
      const features = [...editSources.values()].filter(row => ['country', 'territorial', 'generic'].includes(row.kind)).map(row => row.feature);
      if (sourceGeometry) features.push({ id: sourceKey, geometry: sourceGeometry });
      for (const feature of features) {
        if (!['Polygon', 'MultiPolygon'].includes(feature.geometry?.type)) continue;
        const featureBounds = geometryBounds(feature.geometry);
        const wrapsLongitude = featureBounds && featureBounds[2] - featureBounds[0] > 180;
        if (wrapsLongitude) {
          if (featureBounds[3] < bounds[1] || featureBounds[1] > bounds[3]) continue;
        } else if (![-360, 0, 360].some(shift => boundsOverlap([bounds[0] + shift, bounds[1], bounds[2] + shift, bounds[3]], featureBounds))) continue;
        await new Promise(resolve => setTimeout(resolve, 0));
        if (message.sourceRevision !== sourceRevision || cancelled.has(Number(message.requestId))) throw new Error('CANCELLED');
        const ownerIds = feature.id === sourceKey ? activeOwnerIds : [featureId(feature)];
        for (const edge of geometrySegmentIndex(feature.geometry).query(bounds)) {
          for (const vertex of [edge.a, edge.b]) {
            const nodeKey = vertex.map(value => Number(value).toFixed(7)).join(',');
            if (seenVertices.has(nodeKey)) continue;
            seenVertices.add(nodeKey);
            candidates.push({ kind: 'vertex', coordinate: vertex, ownerIds, nodeKey });
          }
          candidates.push({ kind: feature.id === sourceKey ? 'boundary' : active.size && !active.has(featureId(feature)) ? 'neighbor' : 'edge',
            a: edge.a, b: edge.b, ownerIds, segmentKey: `${feature.id}:${edge.polygonIndex}:${edge.ringIndex}:${edge.segmentIndex}` });
        }
      }
      const [{ createCutGeometry }, { createCountryValidation }, { createBoundarySpatialIndex, segmentBounds }] = await Promise.all([
        import(versionedWorkerAssetUrl('../modules/app-cut-geometry.js')),
        import(versionedWorkerAssetUrl('../modules/app-country-validation.js')),
        import(versionedWorkerAssetUrl('../modules/boundary-spatial-index.js')),
      ]);
      const cut = createCutGeometry(), validation = createCountryValidation();
      cut.connect({ clamp: (v, min, max) => Math.max(min, Math.min(max, v)), interpolateCoordinate: validation.interpolateCoordinate });
      const edges = candidates.filter(candidate => candidate.a && candidate.b);
      const index = createBoundarySpatialIndex();
      edges.forEach((edge, id) => index.insert(id, { edge, id }, segmentBounds(edge)));
      for (const [id, edge] of edges.entries()) for (const other of index.query(segmentBounds(edge))) {
        if (other.id <= id || edge.segmentKey === other.edge.segmentKey) continue;
        const hit = cut.segmentIntersectionDetail(edge.a, edge.b, other.edge.a, other.edge.b);
        if (!hit || hit.overlap || hit.lineT <= 1e-7 || hit.lineT >= 1 - 1e-7 || hit.boundaryT <= 1e-7 || hit.boundaryT >= 1 - 1e-7) continue;
        candidates.push({ kind: 'intersection', coordinate: hit.coord, ownerIds: [...new Set([...edge.ownerIds, ...other.edge.ownerIds])] });
      }
      result = { candidates };
    } else if (message.operation === 'territorial-cut') {
      if (!self.d3) importScripts(versionedWorkerAssetUrl('../vendor/d3.min.js'));
      if (message.payload.source) cutSources.set(message.payload.sourceKey, message.payload.source);
      const source = cutSources.get(message.payload.sourceKey);
      if (!source) throw new Error('분할 원본을 다시 준비하세요.');
      while (cutSources.size > 8) cutSources.delete(cutSources.keys().next().value);
      const { prepareCutInWorker } = await import(versionedWorkerAssetUrl('../modules/cut-worker-preparation.js'));
      result = prepareCutInWorker({ ...message.payload, source }, self.PandoLabCountryGeometry, self.d3, self.polygonClipping);
    } else if (message.operation === 'territorial-display') {
      const { createEditDisplayPreparation } = await import(versionedWorkerAssetUrl('../modules/edit-display-preparation.js'));
      displayService ||= createEditDisplayPreparation();
      const service = displayService;
      result = await service.prepare(message.payload, sourceFeatures('country'), sourceFeatures('territorial'), async () => {
        await new Promise(resolve => setTimeout(resolve, 0));
        if (service !== displayService || message.sourceRevision !== sourceRevision || cancelled.has(Number(message.requestId))) throw new Error('CANCELLED');
      }, key => editSources.get(key)?.feature);
    } else if (message.operation === 'territorial-land-clip') {
      const feature = editSources.get(`generic:${message.payload.targetId}`)?.feature;
      if (!feature) throw new Error('영역 객체를 찾을 수 없습니다.');
      const ownerId = String(feature.properties?.ownerId || '');
      const owner = editSources.get(`country:${ownerId}`)?.feature;
      const bounds = geometryBounds(feature.geometry);
      const nearby = owner ? [owner] : sourceFeatures('country');
      const pieces = [];
      for (const country of nearby) {
        if (!boundsOverlap(bounds, geometryBounds(country.geometry))) continue;
        pieces.push(...self.polygonClipping.intersection(multiCoordinates(feature.geometry), multiCoordinates(country.geometry)));
      }
      result = { geometry: normalizeCountryGeometry(pieces) };
    } else if (message.operation === 'territorial-parents') {
      const feature = editSources.get(`territorial:${message.payload.targetId}`)?.feature;
      if (!feature) throw new Error('하위단위를 찾을 수 없습니다.');
      result = { ids: (message.payload.candidateIds || []).filter(id => {
        const parent = editSources.get(`territorial:${id}`)?.feature || editSources.get(`country:${id}`)?.feature;
        return parent?.geometry && self.polygonClipping.difference(multiCoordinates(feature.geometry), multiCoordinates(parent.geometry)).length === 0;
      }) };
    } else if (message.operation === 'territorial-validation') {
      const receipt = validatedPreviews.get(message.payload.preparationId);
      if (!receipt || receipt.sourceRevision !== sourceRevision) throw new Error('미리보기 원본이 변경되었습니다. 다시 계산하세요.');
      result = { valid: true, preparationId: message.payload.preparationId, sourceRevision };
    } else if (message.operation === 'territorial-coast-availability') {
      if (message.payload.unitId) message.payload = { countries: sourceFeatures('country'),
        unit: editSources.get(`territorial:${message.payload.unitId}`)?.feature };
      if (!message.payload.unit) throw new Error('하위단위를 찾을 수 없습니다.');
      const [{ buildBoundaryTopology }, { analyzeAdminCountryCoast }] = await Promise.all([
        import(versionedWorkerAssetUrl('../modules/boundary-topology.js')),
        import(versionedWorkerAssetUrl('../modules/coast-reconciliation.js')),
      ]);
      const coastCountries = message.payload.countries;
      if (!coastTopologyCache || coastTopologyCache.geometries.length !== coastCountries.length || coastCountries.some((feature, index) => coastTopologyCache.geometries[index] !== feature.geometry)) {
        coastTopologyCache = { geometries: coastCountries.map(feature => feature.geometry), topology: buildBoundaryTopology(coastCountries) };
        coastResultCache = new WeakMap();
      }
      const countryTopology = coastTopologyCache.topology;
      const country = message.payload.countries.find(feature => featureId(feature) === String(message.payload.unit.properties.sovereignId));
      const coastCached = coastResultCache.get(message.payload.unit.geometry);
      if (coastCached?.countryId === featureId(country)) result = coastCached.result;
      else {
      const kernel = self.PandoLabTerritorialEdit.createKernel(self.polygonClipping);
      const coastal = [...countryTopology.segments.values()].some(segment => segment.kind === 'coast'
        && segment.ownerIds.has(featureId(country)) && kernel.adjacent(message.payload.unit.geometry,
          { type: 'Polygon', coordinates: [[segment.a, segment.b]] }));
      const analysis = analyzeAdminCountryCoast({ adminFeature: message.payload.unit, countryFeature: country, countryTopology });
      result = { coastal, reconciliation: analysis.status !== 'unavailable' && !!analysis.conflicts?.length };
        coastResultCache.set(message.payload.unit.geometry, { countryId: featureId(country), result });
      }
    } else if (message.operation === 'territorial-source') {
      const { parent, children } = message.payload.parentId ? {
        parent: editSources.get(`territorial:${message.payload.parentId}`)?.feature || editSources.get(`country:${message.payload.parentId}`)?.feature,
        children: sourceFeatures('territorial').filter(feature => String(feature.properties?.parentId) === String(message.payload.parentId) && feature.properties?.unitType === 'subunit'),
      } : message.payload;
      if (!parent) throw new Error('상위 단위를 찾을 수 없습니다.');
      const occupied = children.length ? self.polygonClipping.union(...children.map(feature => multiCoordinates(feature.geometry))) : [];
      result = { geometry: normalizeCountryGeometry({ type: 'MultiPolygon', coordinates: occupied.length
        ? self.polygonClipping.difference(multiCoordinates(parent.geometry), occupied) : multiCoordinates(parent.geometry) }) };
    } else result = message.operation === 'territorial-edit'
      ? self.PandoLabTerritorialEdit.createKernel(self.polygonClipping, { normalize: normalizeCountryGeometry,
        segmentCandidates: (await import(versionedWorkerAssetUrl('../modules/geometry-segment-index.js'))).territorialSegmentCandidates }).plan({ ...message.payload,
        countries: sourceFeatures('country'), units: sourceFeatures('territorial') })
      : message.operation === 'merge'
      ? executeMerge(message, working)
      : message.operation === 'new-country'
        ? executeNewCountry(message, working)
        : message.operation === 'annex-batch'
          ? executeAnnexBatch(message, working)
          : executeAnnex(message, working);
    if (message.operation === 'territorial-edit') {
      const [{ buildGeometryPreview }, { validateGeometry }] = await Promise.all([
        import(versionedWorkerAssetUrl('../modules/geometry-preview.js')),
        import(versionedWorkerAssetUrl('../modules/geometry-validation.js')),
      ]);
      if (message.sourceRevision !== sourceRevision) throw new Error('CANCELLED');
      const beforeFeatures = [...sourceFeatures('country'), ...sourceFeatures('territorial')].filter(feature => result.affectedIds.includes(featureId(feature)));
      const issues = result.features.flatMap(validateGeometry);
      result.preview = buildGeometryPreview({ operation: `territorial-${message.payload.operation}`, beforeFeatures,
        afterFeatures: result.features, removedIds: result.removedIds, clipper: self.polygonClipping });
      result.preview.validation = { issues, blocking: issues.some(issue => issue.severity !== 'warning') };
      result.preparationId = `territorial:${currentDataRevision}:${++previewSequence}`;
      if (!result.preview.validation.blocking && !result.impacts.some(impact => impact.kind === 'coast-owner')) {
        validatedPreviews.set(result.preparationId, { sourceRevision });
        while (validatedPreviews.size > 8) validatedPreviews.delete(validatedPreviews.keys().next().value);
      }
    }
    if (!readOnly) {
      const [{ buildGeometryPreview }, { validateTerritorialGeometry }] = await Promise.all([
        import(versionedWorkerAssetUrl('../modules/geometry-preview.js')),
        import(versionedWorkerAssetUrl('../modules/geometry-validation.js')),
      ]);
      const affectedIds = new Set(result.affectedIds.map(String));
      const before = [...countries.values()], after = [...working.values()];
      const issueKey = issue => `${issue.kind}:${[...(issue.entityRefs || [])].sort().join('|')}`;
      const baseline = new Set(validateTerritorialGeometry(before, { clipper: self.polygonClipping, affectedIds }).map(issueKey));
      const issues = validateTerritorialGeometry(after, { clipper: self.polygonClipping, affectedIds }).filter(issue => !baseline.has(issueKey(issue)));
      result.preview = buildGeometryPreview({ operation: message.operation, beforeFeatures: before.filter(feature => affectedIds.has(featureId(feature))),
        afterFeatures: result.features, removedIds: result.removedIds, clipper: self.polygonClipping,
        transferredGeometry: result.transferredGeometry || message.previewTransferredGeometry });
      result.preview.validation = { issues, blocking: issues.some(issue => issue.severity !== 'warning') };
    }
    if (Number(message.dataRevision || 0) !== currentDataRevision) throw new Error('CANCELLED');
    if (cancelled.has(Number(message.requestId))) throw new Error('CANCELLED');
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
    cancelled.delete(Number(message.requestId || 0));
  }
};
