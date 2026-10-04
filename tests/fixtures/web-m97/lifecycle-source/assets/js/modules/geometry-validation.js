import { territorialRootId } from './territorial-units.js';
import { coordinateBounds } from './coordinate-bounds.js';

const clone = value => value == null ? value : structuredClone(value);
const featureId = (feature, fallback = '') => String(feature?.id || fallback);

function polygons(geometry) {
  if (geometry?.type === 'Polygon') return [geometry.coordinates || []];
  return geometry?.type === 'MultiPolygon' ? geometry.coordinates || [] : [];
}

function multiCoordinates(geometry) {
  return polygons(geometry);
}

function sampleRing(ring = [], maximumVertices = 16) {
  const count = Math.max(0, ring.length - 1);
  if (count <= maximumVertices) return clone(ring);
  const sampled = [];
  for (let index = 0; index < maximumVertices; index += 1) sampled.push(clone(ring[Math.floor(index * count / maximumVertices)]));
  if (sampled.length) sampled.push(clone(sampled[0]));
  return sampled;
}

function coarseMultiCoordinates(geometry) {
  return polygons(geometry).map(polygon => (polygon || []).map(ring => sampleRing(ring)));
}

function verificationMultiCoordinates(geometry) {
  return polygons(geometry).map(polygon => (polygon || []).map(ring => sampleRing(ring, 512)));
}

function createRingPointIndex(ring = []) {
  const edges = (ring || []).slice(0, -1).map((a, index) => ({ a, b: ring[index + 1] }));
  const minY = edges.reduce((value, edge) => Math.min(value, edge.a[1], edge.b[1]), Infinity);
  const maxY = edges.reduce((value, edge) => Math.max(value, edge.a[1], edge.b[1]), -Infinity);
  const binCount = Math.min(256, Math.max(8, Math.ceil(Math.sqrt(edges.length || 1))));
  const span = Math.max(1e-12, maxY - minY);
  const buckets = Array.from({ length: binCount }, () => []);
  const binFor = value => Math.max(0, Math.min(binCount - 1, Math.floor((value - minY) / span * binCount)));
  edges.forEach(edge => {
    const start = binFor(Math.min(edge.a[1], edge.b[1]));
    const end = binFor(Math.max(edge.a[1], edge.b[1]));
    for (let bin = start; bin <= end; bin += 1) buckets[bin].push(edge);
  });
  return { minY, maxY, binFor, buckets };
}

function pointRelationToRingIndex(point, index, epsilon = 1e-9) {
  if (!index || point[1] < index.minY - epsilon || point[1] > index.maxY + epsilon) return -1;
  let inside = false;
  for (const { a, b } of index.buckets[index.binFor(point[1])] || []) {
    const dx = b[0] - a[0];
    const dy = b[1] - a[1];
    const cross = (point[0] - a[0]) * dy - (point[1] - a[1]) * dx;
    if (Math.abs(cross) <= epsilon * Math.max(1, Math.hypot(dx, dy))
      && point[0] >= Math.min(a[0], b[0]) - epsilon && point[0] <= Math.max(a[0], b[0]) + epsilon
      && point[1] >= Math.min(a[1], b[1]) - epsilon && point[1] <= Math.max(a[1], b[1]) + epsilon) return 0;
    if ((a[1] > point[1]) !== (b[1] > point[1])
      && point[0] < (b[0] - a[0]) * (point[1] - a[1]) / ((b[1] - a[1]) || Number.EPSILON) + a[0]) inside = !inside;
  }
  return inside ? 1 : -1;
}

function createPolygonPointIndex(polygon = []) {
  return (polygon || []).map(createRingPointIndex);
}

function pointStrictlyInPolygonIndex(point, polygonIndex = []) {
  if (pointRelationToRingIndex(point, polygonIndex[0]) !== 1) return false;
  return polygonIndex.slice(1).every(ringIndex => pointRelationToRingIndex(point, ringIndex) === -1);
}

function coarsePolygonsMayOverlap(leftSample, rightSample, leftIndex, rightIndex, leftBounds, rightBounds) {
  const leftOuter = (leftSample?.[0] || []).slice(0, -1);
  const rightOuter = (rightSample?.[0] || []).slice(0, -1);
  if (leftOuter.some(point => pointStrictlyInPolygonIndex(point, rightIndex))
    || rightOuter.some(point => pointStrictlyInPolygonIndex(point, leftIndex))) return true;
  const center = [
    (Math.max(leftBounds[0], rightBounds[0]) + Math.min(leftBounds[2], rightBounds[2])) / 2,
    (Math.max(leftBounds[1], rightBounds[1]) + Math.min(leftBounds[3], rightBounds[3])) / 2,
  ];
  if (pointStrictlyInPolygonIndex(center, leftIndex) && pointStrictlyInPolygonIndex(center, rightIndex)) return true;
  return false;
}

function coordinateEqual(a, b, epsilon = 1e-10) {
  return Math.abs(Number(a?.[0]) - Number(b?.[0])) <= epsilon && Math.abs(Number(a?.[1]) - Number(b?.[1])) <= epsilon;
}

function bounds(geometry) {
  const output = coordinateBounds(geometry?.coordinates);
  return output.every(Number.isFinite) ? output : null;
}

function boundsOverlap(a, b) {
  return !!a && !!b && a[0] <= b[2] && a[2] >= b[0] && a[1] <= b[3] && a[3] >= b[1];
}

function segmentIntersection(a, b, c, d, epsilon = 1e-12) {
  const denominator = (Number(a[0]) - Number(b[0])) * (Number(c[1]) - Number(d[1]))
    - (Number(a[1]) - Number(b[1])) * (Number(c[0]) - Number(d[0]));
  if (Math.abs(denominator) <= epsilon) return null;
  const determinant1 = Number(a[0]) * Number(b[1]) - Number(a[1]) * Number(b[0]);
  const determinant2 = Number(c[0]) * Number(d[1]) - Number(c[1]) * Number(d[0]);
  const x = (determinant1 * (Number(c[0]) - Number(d[0])) - (Number(a[0]) - Number(b[0])) * determinant2) / denominator;
  const y = (determinant1 * (Number(c[1]) - Number(d[1])) - (Number(a[1]) - Number(b[1])) * determinant2) / denominator;
  const inside = (point, left, right) => point[0] >= Math.min(left[0], right[0]) - epsilon
    && point[0] <= Math.max(left[0], right[0]) + epsilon
    && point[1] >= Math.min(left[1], right[1]) - epsilon
    && point[1] <= Math.max(left[1], right[1]) + epsilon;
  return inside([x, y], a, b) && inside([x, y], c, d) ? [x, y] : null;
}

function ringArea(ring = []) {
  let sum = 0;
  for (let index = 0; index < ring.length - 1; index += 1) {
    sum += Number(ring[index][0]) * Number(ring[index + 1][1]) - Number(ring[index + 1][0]) * Number(ring[index][1]);
  }
  return sum / 2;
}

function geometryPlanarArea(geometry) {
  return polygons(geometry).reduce((total, polygon) => total + Math.max(0,
    Math.abs(ringArea(polygon[0] || [])) - (polygon || []).slice(1).reduce((sum, ring) => sum + Math.abs(ringArea(ring)), 0),
  ), 0);
}

function issue(kind, message, details = {}) {
  const target = details.entityRefs?.join('|') || (details.vertexIndex ?? details.segmentIndex ?? 'map');
  return { id: `${kind}:${target}:${details.sequence || 0}`, kind, severity: details.severity || 'error', message, ...details };
}

function validateRing(ring, entityRefs, polygonIndex, ringIndex) {
  const issues = [];
  if (!Array.isArray(ring) || ring.length < 4) {
    issues.push(issue('invalid-geometry', '고리를 구성하는 꼭짓점이 부족합니다.', { entityRefs, coordinate: ring?.[0], polygonIndex, ringIndex }));
    return issues;
  }
  if (!coordinateEqual(ring[0], ring[ring.length - 1])) {
    issues.push(issue('open-ring', 'Polygon 고리가 닫혀 있지 않습니다.', { entityRefs, coordinate: ring[ring.length - 1], polygonIndex, ringIndex }));
  }
  if (Math.abs(ringArea(ring)) <= 1e-14) {
    issues.push(issue('zero-area', '면적이 없거나 지나치게 작은 고리입니다.', { entityRefs, coordinate: ring[0], polygonIndex, ringIndex }));
  }
  for (let index = 1; index < ring.length; index += 1) {
    if (coordinateEqual(ring[index - 1], ring[index])) issues.push(issue('duplicate-vertex', '연속된 중복 꼭짓점이 있습니다.', {
      entityRefs, coordinate: ring[index], polygonIndex, ringIndex, vertexIndex: index,
    }));
  }
  const segmentCount = ring.length - 1;
  const segments = Array.from({ length: segmentCount }, (_, index) => {
    const a = ring[index];
    const b = ring[index + 1];
    return {
      index, a, b,
      minX: Math.min(a[0], b[0]), maxX: Math.max(a[0], b[0]),
      minY: Math.min(a[1], b[1]), maxY: Math.max(a[1], b[1]),
    };
  }).sort((left, right) => left.minX - right.minX || left.minY - right.minY);
  let active = [];
  for (const current of segments) {
    active = active.filter(candidate => candidate.maxX >= current.minX - 1e-12);
    for (const candidate of active) {
      const adjacent = Math.abs(candidate.index - current.index) === 1
        || (candidate.index === 0 && current.index === segmentCount - 1)
        || (current.index === 0 && candidate.index === segmentCount - 1);
      if (adjacent || candidate.maxY < current.minY - 1e-12 || candidate.minY > current.maxY + 1e-12) continue;
      const intersection = segmentIntersection(candidate.a, candidate.b, current.a, current.b);
      if (!intersection) continue;
      issues.push(issue('self-intersection', '경계가 자기 자신과 교차합니다.', {
        entityRefs, coordinate: intersection, polygonIndex, ringIndex, segmentIndex: candidate.index,
      }));
      return issues;
    }
    active.push(current);
  }
  return issues;
}

export function validateGeometry(feature) {
  const id = featureId(feature, 'unknown');
  const geometry = feature?.geometry;
  if (!geometry || !['Polygon', 'MultiPolygon'].includes(geometry.type)) {
    return [issue('invalid-geometry', 'Polygon 또는 MultiPolygon geometry가 필요합니다.', { entityRefs: [id], bounds: bounds(geometry) })];
  }
  const issues = [];
  polygons(geometry).forEach((polygon, polygonIndex) => {
    if (!polygon?.length) issues.push(issue('invalid-geometry', '빈 Polygon component가 있습니다.', { entityRefs: [id], polygonIndex }));
    (polygon || []).forEach((ring, ringIndex) => issues.push(...validateRing(ring, [id], polygonIndex, ringIndex)));
  });
  return issues;
}

function clipGeometry(type, coordinates) {
  return { type, coordinates: clone(coordinates) };
}

export function validateTerritorialGeometry(features = [], {
  clipper = null, baselineUnion = null, affectedIds = null, pairKeys = null, diagnostics = null, overlapOnly = false,
} = {}) {
  const issues = [];
  const seen = new Map();
  const affected = affectedIds ? new Set([...affectedIds].map(String)) : null;
  const candidates = affected ? features.filter(feature => affected.has(featureId(feature))) : features;
  if (!overlapOnly) {
    features.forEach((feature, index) => {
      const id = featureId(feature, index);
      if (seen.has(id)) issues.push(issue('duplicate-id', `중복 ID ${id}가 있습니다.`, { entityRefs: [seen.get(id), id], bounds: bounds(feature.geometry) }));
      else seen.set(id, id);
    });
    for (const feature of candidates) issues.push(...validateGeometry(feature));
  }
  if (!clipper?.intersection) return issues;

  const tested = new Set();
  const coarseCoordinates = new WeakMap();
  const verificationCoordinates = new WeakMap();
  const componentBoundsCache = new WeakMap();
  const polygonPointIndexCache = new WeakMap();
  const coarseFor = feature => {
    if (!coarseCoordinates.has(feature)) coarseCoordinates.set(feature, coarseMultiCoordinates(feature.geometry));
    return coarseCoordinates.get(feature);
  };
  const verificationFor = feature => {
    if (!verificationCoordinates.has(feature)) verificationCoordinates.set(feature, verificationMultiCoordinates(feature.geometry));
    return verificationCoordinates.get(feature);
  };
  const componentBoundsFor = feature => {
    if (!componentBoundsCache.has(feature)) {
      componentBoundsCache.set(feature, polygons(feature.geometry).map(coordinates => bounds({ coordinates })));
    }
    return componentBoundsCache.get(feature);
  };
  const polygonPointIndexesFor = feature => {
    if (!polygonPointIndexCache.has(feature)) {
      polygonPointIndexCache.set(feature, polygons(feature.geometry).map(createPolygonPointIndex));
    }
    return polygonPointIndexCache.get(feature);
  };
  const overlappingComponentPairs = (left, right) => {
    const output = [];
    const leftBounds = componentBoundsFor(left);
    const rightBounds = componentBoundsFor(right);
    leftBounds.forEach((leftBox, leftIndex) => rightBounds.forEach((rightBox, rightIndex) => {
      if (Math.min(leftBox[2], rightBox[2]) - Math.max(leftBox[0], rightBox[0]) > 1e-10
        && Math.min(leftBox[3], rightBox[3]) - Math.max(leftBox[1], rightBox[1]) > 1e-10) output.push([leftIndex, rightIndex]);
    }));
    return output;
  };
  for (const feature of candidates) {
    const id = featureId(feature);
    const featureBounds = bounds(feature.geometry);
    for (const other of features) {
      const otherId = featureId(other);
      if (!id || id === otherId || !boundsOverlap(featureBounds, bounds(other.geometry))) continue;
      if (diagnostics) diagnostics.boundsCandidatePairs = (diagnostics.boundsCandidatePairs || 0) + 1;
      const pairKey = id < otherId ? `${id}|${otherId}` : `${otherId}|${id}`;
      if (tested.has(pairKey)) continue;
      tested.add(pairKey);
      if (pairKeys && !pairKeys.has(pairKey)) continue;
      const componentPairs = overlappingComponentPairs(feature, other);
      if (diagnostics) diagnostics.componentCandidatePairs = (diagnostics.componentCandidatePairs || 0) + componentPairs.length;
      if (!componentPairs.length) continue;
      // Most country pairs whose bounds touch only share a border. A small,
      // noncanonical preflight rejects those cheaply; canonical coordinates are
      // still used for every pair that could contain an overlap.
      let coarseMayOverlap = !!affected;
      if (pairKeys && !affected) {
        for (const [leftIndex, rightIndex] of componentPairs) {
          let verification;
          try {
            verification = clipper.intersection(
              [verificationFor(feature)[leftIndex]], [verificationFor(other)[rightIndex]],
            );
          } catch (_) {
            coarseMayOverlap = true;
            break;
          }
          if (geometryPlanarArea(clipGeometry('MultiPolygon', verification)) > 1e-8) {
            coarseMayOverlap = true;
            break;
          }
        }
      } else {
        for (const [leftIndex, rightIndex] of componentPairs) {
          if (coarseMayOverlap || coarsePolygonsMayOverlap(
            coarseFor(feature)[leftIndex], coarseFor(other)[rightIndex],
            polygonPointIndexesFor(feature)[leftIndex], polygonPointIndexesFor(other)[rightIndex],
            componentBoundsFor(feature)[leftIndex], componentBoundsFor(other)[rightIndex],
          )) {
            coarseMayOverlap = true;
            break;
          }
        }
      }
      if (!coarseMayOverlap) continue;
      if (diagnostics) diagnostics.preciseIntersectionPairs = (diagnostics.preciseIntersectionPairs || 0) + 1;
      const coordinates = [];
      let intersectionFailed = false;
      try {
        const leftPolygons = multiCoordinates(feature.geometry);
        const rightPolygons = multiCoordinates(other.geometry);
        for (const [leftIndex, rightIndex] of componentPairs) {
          const result = clipper.intersection([leftPolygons[leftIndex]], [rightPolygons[rightIndex]]);
          if (result?.length) coordinates.push(...result);
        }
      } catch (_) {
        intersectionFailed = true;
        issues.push(issue('invalid-geometry', `${id}와 ${otherId}의 중첩 검사를 완료하지 못했습니다.`, {
          entityRefs: [id, otherId], bounds: featureBounds,
        }));
      }
      if (intersectionFailed) continue;
      const overlap = clipGeometry('MultiPolygon', coordinates);
      const overlapPlanarArea = geometryPlanarArea(overlap);
      if (overlapPlanarArea <= 1e-10) continue;
      issues.push(issue('overlap', `${id}와 ${otherId} 사이에 중첩 영역이 있습니다.`, {
        entityRefs: [id, otherId], geometry: overlap, bounds: bounds(overlap), planarArea: overlapPlanarArea,
      }));
    }
  }
  if (baselineUnion && clipper.xor && candidates.length) {
    const currentUnion = clipper.union(...candidates.map(feature => multiCoordinates(feature.geometry)));
    const changed = clipGeometry('MultiPolygon', clipper.xor(multiCoordinates(baselineUnion), currentUnion));
    if (geometryPlanarArea(changed) > 1e-10) issues.push(issue('gap', '편집 전 영역과 비교해 빈틈 또는 면적 손실이 생겼습니다.', {
      entityRefs: candidates.map(featureId), geometry: changed, bounds: bounds(changed),
    }));
  }
  return issues;
}

function validateAdministrativeContainment(units = [], countries = [], { clipper = null } = {}) {
  const issues = [];
  const countryMap = new Map(countries.map(feature => [featureId(feature), feature]));
  const unitMap = new Map(units.map(feature => [String(feature.id), feature]));
  const seenIds = new Set();
  for (const [unitIndex, unit] of units.entries()) {
    const id = String(unit.id || '');
    const properties = unit.properties || {};
    if (id && seenIds.has(id)) issues.push(issue('duplicate-id', `중복 영토 객체 ID ${id}가 있습니다.`, {
      entityRefs: [id], bounds: bounds(unit.geometry), sequence: unitIndex,
    }));
    if (id) seenIds.add(id);
    let countryId = '';
    try { countryId = territorialRootId(unit, key => unitMap.get(key) || countryMap.get(key)); }
    catch (error) { issues.push(issue('invalid-parent', error.message, { entityRefs: [id] })); }
    const parentId = String(properties.parentId || '');
    if (countryId && !countryMap.has(countryId)) issues.push(issue('invalid-sovereign', `${properties.name || id}의 countryId가 존재하지 않습니다.`, {
      entityRefs: [id, countryId], coordinate: bounds(unit.geometry)?.slice(0, 2),
    }));
    const parent = unitMap.get(parentId) || countryMap.get(parentId) || countryMap.get(countryId);
    if (parentId && !unitMap.has(parentId) && !countryMap.has(parentId)) issues.push(issue('orphan-administrative', `${properties.name || id}의 parentId가 존재하지 않습니다.`, {
      entityRefs: [id, parentId], bounds: bounds(unit.geometry),
    }));
    if (properties.coverageMode !== 'explicit' && clipper?.difference && parent?.geometry && unit.geometry) {
      const outside = clipGeometry('MultiPolygon', clipper.difference(multiCoordinates(unit.geometry), multiCoordinates(parent.geometry)));
      if (geometryPlanarArea(outside) > 1e-10) issues.push(issue('outside-parent', `${properties.name || id}이(가) 부모 영역 밖에 있습니다.`, {
        entityRefs: [id, featureId(parent)], geometry: outside, bounds: bounds(outside),
      }));
    }
  }
  return issues;
}

function validateDistributionReference(entries = [], units = []) {
  const unitIds = new Set(units.map(feature => String(feature.id)));
  const seenIds = new Set();
  const issues = [];
  entries.forEach((entry, entryIndex) => {
    const id = String(entry.id || '');
    if (id && seenIds.has(id)) issues.push(issue('duplicate-id', `중복 분포 ID ${id}가 있습니다.`, {
      entityRefs: [id], sequence: entryIndex,
    }));
    if (id) seenIds.add(id);
    if (entry.mode === 'territorial' && !unitIds.has(String(entry.territorialUnitId || ''))) {
      issues.push(issue('missing-territorial-reference', `분포가 존재하지 않는 territorialUnitId ${entry.territorialUnitId || '—'}를 참조합니다.`, {
        entityRefs: [id, String(entry.territorialUnitId || '')], severity: 'error', sequence: entryIndex,
      }));
    }
  });
  return issues;
}

export function runMapAudit({
  countries = [], coarseCountries = [], preciseAffectedIds = [], units = [], distributionEntries = [],
  clipper = null, affectedIds = null, baselineUnion = null,
} = {}) {
  const startedAt = globalThis.performance?.now?.() || Date.now();
  const diagnostics = {};
  const structuralAffectedIds = coarseCountries.length ? preciseAffectedIds : affectedIds;
  const structuralIssues = validateTerritorialGeometry(countries, {
    clipper: null,
    affectedIds: structuralAffectedIds,
    baselineUnion,
  });
  let overlapIssues;
  if (clipper && coarseCountries.length) {
    // The bundled canonical world is the validated baseline. Preview geometry
    // is intentionally simplified and can create false sliver overlaps, so a
    // project-wide audit performs exact clipping only for countries changed
    // since that baseline (imports are marked dirty as well).
    diagnostics.baselineCountryCount = coarseCountries.length;
    diagnostics.preciseAffectedCount = preciseAffectedIds.length;
    overlapIssues = [];
    if (preciseAffectedIds.length) {
      overlapIssues.push(...validateTerritorialGeometry(countries, {
        clipper, affectedIds: preciseAffectedIds, diagnostics,
      }).filter(item => item.kind === 'overlap'));
    }
    const seen = new Set();
    overlapIssues = overlapIssues.filter(item => {
      const key = `${item.kind}:${[...(item.entityRefs || [])].sort().join('|')}`;
      if (seen.has(key)) return false;
      seen.add(key);
      return true;
    });
  } else {
    overlapIssues = validateTerritorialGeometry(countries, { clipper, affectedIds, baselineUnion, diagnostics })
      .filter(item => item.kind === 'overlap' || item.kind === 'gap');
  }
  const issues = [
    ...structuralIssues,
    ...overlapIssues,
    ...validateAdministrativeContainment(units, countries, { clipper }),
    ...validateDistributionReference(distributionEntries, units),
  ];
  const counts = issues.reduce((output, item) => {
    output[item.kind] = (output[item.kind] || 0) + 1;
    return output;
  }, {});
  return {
    revision: 0,
    scope: affectedIds ? 'affected' : 'project',
    counts,
    issues,
    durationMs: (globalThis.performance?.now?.() || Date.now()) - startedAt,
    diagnostics,
    cancelled: false,
  };
}
