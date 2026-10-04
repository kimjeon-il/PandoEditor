import { geometryAreaKm2 } from './geometry-metrics.js';
import { clippingOperationWithPrecisionRetry } from './polygon-clipping-calculation.js';

function coordinateKey(point, precision = 8) {
  return `${Number(point?.[0] || 0).toFixed(precision)},${Number(point?.[1] || 0).toFixed(precision)}`;
}

function closedRing(raw) {
  if (!Array.isArray(raw) || !raw.length) return [];
  const ring = raw.map(point => [Number(point[0]), Number(point[1])]);
  const first = ring[0], last = ring[ring.length - 1];
  if (first[0] !== last[0] || first[1] !== last[1]) ring.push(first.slice());
  return ring;
}

function signedArea(ring) {
  let area = 0;
  for (let i = 0; i < ring.length - 1; i += 1) area += ring[i][0] * ring[i + 1][1] - ring[i + 1][0] * ring[i][1];
  return area / 2;
}

function unwrapRing(raw) {
  const ring = closedRing(raw);
  if (!ring.length) return ring;
  const output = [ring[0].slice()];
  for (let i = 1; i < ring.length; i += 1) {
    let longitude = ring[i][0];
    while (longitude - output[i - 1][0] > 180) longitude -= 360;
    while (longitude - output[i - 1][0] < -180) longitude += 360;
    output.push([longitude, ring[i][1]]);
  }
  return output;
}

function orientation(a, b, c) {
  return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}

function properIntersection(a, b, c, d, epsilon = 1e-10) {
  const abC = orientation(a, b, c), abD = orientation(a, b, d);
  const cdA = orientation(c, d, a), cdB = orientation(c, d, b);
  return ((abC > epsilon && abD < -epsilon) || (abC < -epsilon && abD > epsilon))
    && ((cdA > epsilon && cdB < -epsilon) || (cdA < -epsilon && cdB > epsilon));
}

function ringSelfIntersects(raw) {
  const ring = unwrapRing(raw);
  const count = Math.max(0, ring.length - 1);
  if (count < 4) return false;
  const segments = [];
  for (let index = 0; index < count; index += 1) {
    const a = ring[index], b = ring[index + 1];
    segments.push({ index, a, b, minX: Math.min(a[0], b[0]), maxX: Math.max(a[0], b[0]), minY: Math.min(a[1], b[1]), maxY: Math.max(a[1], b[1]) });
  }
  segments.sort((a, b) => a.minX - b.minX || a.minY - b.minY || a.index - b.index);
  let active = [];
  for (const segment of segments) {
    active = active.filter(other => other.maxX >= segment.minX);
    for (const other of active) {
      if (Math.abs(segment.index - other.index) <= 1 || (Math.min(segment.index, other.index) === 0 && Math.max(segment.index, other.index) === count - 1)) continue;
      if (other.maxY < segment.minY || other.minY > segment.maxY) continue;
      if (properIntersection(segment.a, segment.b, other.a, other.b)) return true;
    }
    active.push(segment);
  }
  return false;
}

function polygonSets(geometry) {
  if (geometry?.type === 'Polygon') return [geometry.coordinates || []];
  if (geometry?.type === 'MultiPolygon') return geometry.coordinates || [];
  return [];
}

function validGeometry(geometry) {
  const polygons = polygonSets(geometry);
  if (!polygons.length) return false;
  return polygons.every(polygon => polygon?.length && polygon.every(raw => {
    const ring = closedRing(raw);
    if (ring.length < 4 || new Set(ring.slice(0, -1).map(point => coordinateKey(point))).size < 3) return false;
    if (!ring.every(point => Number.isFinite(point[0]) && Number.isFinite(point[1]) && point[0] >= -180.000001 && point[0] <= 180.000001 && point[1] >= -90.000001 && point[1] <= 90.000001)) return false;
    return Math.abs(signedArea(ring)) > 1e-14 && !ringSelfIntersects(ring);
  }));
}

function ringBounds(ring) {
  const bounds = [Infinity, Infinity, -Infinity, -Infinity];
  for (const point of ring || []) {
    bounds[0] = Math.min(bounds[0], Number(point[0]));
    bounds[1] = Math.min(bounds[1], Number(point[1]));
    bounds[2] = Math.max(bounds[2], Number(point[0]));
    bounds[3] = Math.max(bounds[3], Number(point[1]));
  }
  return bounds;
}

function boundsOverlap(a, b) {
  return a[0] <= b[2] && a[2] >= b[0] && a[1] <= b[3] && a[3] >= b[1];
}

function polygonBounds(geometry) {
  return polygonSets(geometry).map(polygon => ringBounds(polygon?.[0] || []));
}

function canOverlap(a, b) {
  return a.some(first => b.some(second => boundsOverlap(first, second)));
}

const robustPolygonIntersection = (clipper, left, right) => (
  clippingOperationWithPrecisionRetry(clipper, 'intersection', left, right)
);

function planarArea(multiPolygon) {
  return (multiPolygon || []).reduce((total, polygon) => {
    if (!polygon?.length) return total;
    const exterior = Math.abs(signedArea(closedRing(polygon[0])));
    const holes = polygon.slice(1).reduce((sum, ring) => sum + Math.abs(signedArea(closedRing(ring))), 0);
    return total + Math.max(0, exterior - holes);
  }, 0);
}

function featureName(feature) {
  const properties = feature?.properties || {};
  return properties.pandolab_name || properties.NAME_KO || properties.NAME_0 || properties.NAME || properties.name || properties.pandolab_id || feature?.id || '국가';
}

function featureId(feature, index) {
  const properties = feature?.properties || {};
  return String(properties.pandolab_id || properties.ADM0_A3 || properties.ISO_A3 || properties.GID_0 || feature?.id || index + 1);
}

export function validateCollection(collection, affectedIds = null, clipper = globalThis.polygonClipping) {
  const features = collection?.features || [];
  const ids = features.map(featureId);
  if (ids.some(id => !id) || new Set(ids).size !== ids.length) throw new Error('국가 ID가 비어 있거나 중복되었습니다.');
  const affected = Array.isArray(affectedIds) && affectedIds.length ? new Set(affectedIds.map(String)) : null;
  const componentBounds = [];
  for (let index = 0; index < features.length; index += 1) {
    if ((!affected || affected.has(ids[index])) && !validGeometry(features[index].geometry)) {
      throw new Error(`${featureName(features[index])}의 Polygon 경계가 유효하지 않습니다.`);
    }
    componentBounds.push(polygonBounds(features[index].geometry));
  }
  for (let i = 0; i < features.length; i += 1) {
    for (let j = i + 1; j < features.length; j += 1) {
      if (affected && !affected.has(ids[i]) && !affected.has(ids[j])) continue;
      if (!canOverlap(componentBounds[i], componentBounds[j])) continue;
      const overlap = [];
      let overlapPlanarArea = 0;
      const leftPolygons = polygonSets(features[i].geometry);
      const rightPolygons = polygonSets(features[j].geometry);
      for (let leftIndex = 0; leftIndex < leftPolygons.length; leftIndex += 1) {
        for (let rightIndex = 0; rightIndex < rightPolygons.length; rightIndex += 1) {
          if (!boundsOverlap(componentBounds[i][leftIndex], componentBounds[j][rightIndex])) continue;
          const componentOverlap = robustPolygonIntersection(
            clipper,
            [leftPolygons[leftIndex]],
            [rightPolygons[rightIndex]],
          );
          if (!componentOverlap?.length) continue;
          overlap.push(...componentOverlap);
          overlapPlanarArea += planarArea(componentOverlap);
        }
      }
      if (overlapPlanarArea <= 1e-8) continue;
      const overlapGeometry = { type: 'MultiPolygon', coordinates: overlap };
      const areaKm2 = geometryAreaKm2(overlapGeometry);
      return { overlapAreaKm2: areaKm2, firstOverlap: [ids[i], ids[j]] };
    }
  }
  return { overlapAreaKm2: 0, firstOverlap: null };
}
