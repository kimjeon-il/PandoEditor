import { normalizePolygonGeometry, multiCoordinates, area, geometryBounds, boundsOverlap, polygonBounds, featureId, clone } from './map-edit-geometry.js';
import { ringSignedArea, hasCanonicalPolygonWinding } from './map-edit-geometry.js';
import { clippingOperationWithPrecisionRetry } from './polygon-clipping-calculation.js';

/** Calculation kernel; every command owns a private working map. */
export function createCountryCommandCalculator(clipper) {
  const geometryBoundsCache = new WeakMap();
  const boundsFor = geometry => {
    if (!geometry || typeof geometry !== 'object') return geometryBounds(geometry);
    let bounds = geometryBoundsCache.get(geometry);
    if (!bounds) {
      bounds = geometryBounds(geometry);
      geometryBoundsCache.set(geometry, bounds);
    }
    return bounds;
  };
  const clippingOperation = (method, ...inputs) => clippingOperationWithPrecisionRetry(clipper, method, ...inputs);

  function areaPolygonsNearFeatures(features, areaCoordinates) {
    const areaBounds = boundsFor({ type: 'MultiPolygon', coordinates: areaCoordinates });
    const polygons = [];
    for (const feature of features) {
      for (const polygon of multiCoordinates(feature.geometry)) {
        if (boundsOverlap(polygonBounds(polygon), areaBounds)) polygons.push([polygon]);
      }
    }
    return polygons;
  }

  function containmentTolerance(coordinates) {
    const geometry = { type: 'MultiPolygon', coordinates };
    // Use the same scale-aware tolerance as the post-edit topology check.
    // River partitions are produced through a separate clipping pass, so an
    // otherwise-contained cell can retain a sub-grid fringe at a shared border.
    return Math.max(1e-8, boundaryLength(geometry) * 2e-7);
  }

  function transferWithinSourceUnion(transferred, sourceUnion, message) {
    const outside = clippingOperation('difference', transferred, sourceUnion);
    if (area(outside) > containmentTolerance(transferred)) throw new Error(message);
    // Do not carry a numerical fringe into subtraction or the receiving
    // country. Meaningful out-of-source area is rejected above.
    return clippingOperation('intersection', transferred, sourceUnion);
  }

  function transferFromRiverPartitions(transferred, sourceUnion, message) {
    // River cells have already been selected from a source-country component.
    // Their geometry is produced by a separate clipping pass, however, so use
    // the command's authoritative source union for the final boundary.  This
    // is deliberately not used for freehand selections: those must still
    // report a meaningful out-of-source drawing to the user.
    const normalized = clippingOperation('intersection', transferred, sourceUnion);
    if (!normalized.some(positivePolygonArea)) throw new Error(message);
    return normalized;
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
    const areaBounds = boundsFor({ type: 'MultiPolygon', coordinates: areaCoordinates });
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
    return { affected, geometry: normalizePolygonGeometry(polygons) };
  }

  function unionAreaWithGeometry(geometry, areaCoordinates) {
    const areaBounds = boundsFor({ type: 'MultiPolygon', coordinates: areaCoordinates });
    const untouched = [];
    const nearby = [];
    for (const polygon of multiCoordinates(geometry)) {
      if (boundsOverlap(polygonBounds(polygon), areaBounds)) nearby.push([polygon]);
      else untouched.push(clone(polygon));
    }
    const merged = nearby.length ? clippingOperation('union', ...nearby, areaCoordinates) : clone(areaCoordinates);
    return normalizePolygonGeometry([...untouched, ...merged]);
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
    if (!polygons.length || !hasCanonicalPolygonWinding(geometry)) return false;
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
      const leftBounds = boundsFor(left.geometry);
      for (const right of all) {
        const rightId = featureId(right);
        if (!rightId || rightId === leftId || !boundsOverlap(leftBounds, boundsFor(right.geometry))) continue;
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
      const featureBounds = boundsFor(feature.geometry);
      for (const other of map.values()) {
        const otherId = featureId(other);
        if (otherId === id || !boundsOverlap(featureBounds, boundsFor(other.geometry))) continue;
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
    if (!transferred.some(positivePolygonArea)) throw new Error('편입할 유효한 영토가 없습니다.');
    const donorInputs = areaPolygonsNearFeatures(donors, transferred);
    const donorUnion = donorInputs.length ? clippingOperation('union', ...donorInputs) : [];
    if (!allowUnclaimed) {
      const outsideMessage = '선택 범위가 원본 국가 밖입니다.';
      const hasRiverPartitionProvenance = Array.isArray(message.riverSliverContext)
        && message.riverSliverContext.some(row => donorIds.includes(String(row?.donorId || '')));
      transferred = hasRiverPartitionProvenance
        ? transferFromRiverPartitions(transferred, donorUnion, outsideMessage)
        : transferWithinSourceUnion(transferred, donorUnion, outsideMessage);
    }
    const updates = [];
    const removedIds = [];
    const affectedDonorIds = [];
    const slivers = { polygons: [], areaM2: 0 };
    for (const donor of donors) {
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
    const transferredGeometry = normalizePolygonGeometry(transferred);
    if (!transferredGeometry) throw new Error('편입 결과의 표시 형상이 유효하지 않습니다.');
    return {
      features: updates, removedIds, affectedIds: [targetId, ...affectedDonorIds], affectedDonorIds,
      transferredArea: area(transferred), transferredGeometry: { type: 'MultiPolygon', coordinates: multiCoordinates(transferredGeometry) },
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
    next.geometry = normalizePolygonGeometry(clippingOperation('union', source.geometry.coordinates, ...targets.map(feature => feature.geometry.coordinates)));
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
    let transferred = multiCoordinates(message.transferredGeometry);
    const sourceInputs = areaPolygonsNearFeatures(sources, transferred);
    const sourceUnion = sourceInputs.length ? clippingOperation('union', ...sourceInputs) : [];
    transferred = transferWithinSourceUnion(transferred, sourceUnion,
      '선택 영역이 영토를 가져올 국가 밖으로 벗어났습니다.');
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
    newFeature.geometry = normalizePolygonGeometry(transferred);
    updates.push(newFeature);
    applyPatch(working, updates, removedIds);
    const affectedIds = new Set([...affectedSourceIds, newId]);
    validateResult(working, affectedIds, baseline);
    return { features: updates, removedIds, affectedIds: [...affectedIds], affectedSourceIds, transferredArea: area(transferred), newCountryId: newId };
  }

  function calculate(message, countries) {
    const working = new Map(countries);
    const operation = message.operation === 'merge' ? executeMerge
      : message.operation === 'new-country' ? executeNewCountry
      : message.operation === 'annex-batch' ? executeAnnexBatch : executeAnnex;
    const result = operation(message, working);
    return { result, afterFeatures: [...working.values()] };
  }
  return { calculate, subtractAreaFromGeometry, sliverAreaM2, sharesBoundary };
}
