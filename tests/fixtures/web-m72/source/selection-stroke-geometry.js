import { buildRenderableBoundarySegments, densifyBoundarySegmentsForProjection } from './geographic-boundary.js';

const RIBBON_VERTEX_SCALAR_COUNT = 6;
const RIBBON_SEGMENT_SCALAR_COUNT = RIBBON_VERTEX_SCALAR_COUNT * 6;
const RIBBON_MIN_SEGMENT_LENGTH_DEGREES = 1e-12;
const RIBBON_SIDE_PATTERN = Object.freeze([-1, 1, -1, -1, 1, 1]);
const RIBBON_ENDPOINT_PATTERN = Object.freeze([0, 0, 1, 1, 0, 1]);

export function buildSelectionBoundarySegments(geometry, { densify = false } = {}) {
  const segments = buildRenderableBoundarySegments(geometry);
  return densify ? densifyBoundarySegmentsForProjection(segments) : segments;
}

function appendSelectionRibbonSegment(values, startLon, startLat, endLon, endLat) {
  const coordinates = [startLon, startLat, endLon, endLat].map(Number);
  if (!coordinates.every(Number.isFinite)) return false;
  const [safeStartLon, safeStartLat, safeEndLon, safeEndLat] = coordinates;
  if (Math.hypot(safeEndLon - safeStartLon, safeEndLat - safeStartLat) <= RIBBON_MIN_SEGMENT_LENGTH_DEGREES) return false;
  values.push(
    safeStartLon, safeStartLat, safeEndLon, safeEndLat, -1, 0,
    safeStartLon, safeStartLat, safeEndLon, safeEndLat, 1, 0,
    safeStartLon, safeStartLat, safeEndLon, safeEndLat, -1, 1,
    safeStartLon, safeStartLat, safeEndLon, safeEndLat, -1, 1,
    safeStartLon, safeStartLat, safeEndLon, safeEndLat, 1, 0,
    safeStartLon, safeStartLat, safeEndLon, safeEndLat, 1, 1,
  );
  return true;
}

function ribbonVerticesForSelectionSegments(segments) {
  const values = [];
  for (const [[startLon, startLat], [endLon, endLat]] of segments || []) {
    appendSelectionRibbonSegment(values, startLon, startLat, endLon, endLat);
  }
  return values;
}

export function validateSelectionRibbonVertices(vertices, expectedSegmentCount = null) {
  const values = Array.from(vertices || []);
  if (!values.length) return { valid: false, segmentCount: 0, reason: 'empty-ribbon' };
  if (values.length % RIBBON_SEGMENT_SCALAR_COUNT !== 0) {
    return { valid: false, segmentCount: 0, reason: 'invalid-ribbon-length' };
  }
  if (!values.every(value => Number.isFinite(Number(value)))) {
    return { valid: false, segmentCount: 0, reason: 'non-finite-ribbon-value' };
  }
  const segmentCount = values.length / RIBBON_SEGMENT_SCALAR_COUNT;
  if (expectedSegmentCount != null && Number(expectedSegmentCount) !== segmentCount) {
    return { valid: false, segmentCount: 0, reason: 'segment-count-mismatch' };
  }
  for (let segmentOffset = 0; segmentOffset < values.length; segmentOffset += RIBBON_SEGMENT_SCALAR_COUNT) {
    const startLon = Number(values[segmentOffset]);
    const startLat = Number(values[segmentOffset + 1]);
    const endLon = Number(values[segmentOffset + 2]);
    const endLat = Number(values[segmentOffset + 3]);
    if (Math.hypot(endLon - startLon, endLat - startLat) <= RIBBON_MIN_SEGMENT_LENGTH_DEGREES) {
      return { valid: false, segmentCount: 0, reason: 'zero-length-ribbon-segment' };
    }
    for (let vertex = 0; vertex < 6; vertex += 1) {
      const offset = segmentOffset + vertex * RIBBON_VERTEX_SCALAR_COUNT;
      if (Number(values[offset]) !== startLon || Number(values[offset + 1]) !== startLat
        || Number(values[offset + 2]) !== endLon || Number(values[offset + 3]) !== endLat) {
        return { valid: false, segmentCount: 0, reason: 'inconsistent-ribbon-endpoints' };
      }
      if (Number(values[offset + 4]) !== RIBBON_SIDE_PATTERN[vertex]
        || Number(values[offset + 5]) !== RIBBON_ENDPOINT_PATTERN[vertex]) {
        return { valid: false, segmentCount: 0, reason: 'invalid-ribbon-vertex-pattern' };
      }
    }
  }
  return { valid: true, segmentCount, reason: '' };
}

export function buildSelectionRibbonVertices(geometry) {
  return ribbonVerticesForSelectionSegments(buildSelectionBoundarySegments(geometry));
}

function flattenSelectionGeometry(feature) {
  if (!feature) return [];
  if (feature.type === 'FeatureCollection') return (feature.features || []).flatMap(flattenSelectionGeometry);
  if (feature.type === 'Feature') return flattenSelectionGeometry(feature.geometry);
  return [feature];
}

export function buildSelectionBoundaryBufferData(nextItems = []) {
  const values = [];
  let segmentCount = 0;
  const renderedKeys = [];
  const missingKeys = [];
  for (const item of nextItems) {
    const key = String(item?.key || '');
    const itemValueOffset = values.length;
    let itemSegmentCount = 0;
    try {
      if (item?.ribbonVertices?.length) {
        const validation = validateSelectionRibbonVertices(item.ribbonVertices, item.segmentCount ?? null);
        if (!validation.valid) throw new Error(validation.reason);
        for (const value of item.ribbonVertices) values.push(Number(value));
        itemSegmentCount = validation.segmentCount;
      } else if (!item?.missing) {
        for (const geometry of flattenSelectionGeometry(item?.geometry)) {
          const segments = buildSelectionBoundarySegments(geometry, { densify: true });
          const ribbonValues = ribbonVerticesForSelectionSegments(segments);
          if (!ribbonValues.length) continue;
          const validation = validateSelectionRibbonVertices(ribbonValues);
          if (!validation.valid) throw new Error(validation.reason);
          itemSegmentCount += validation.segmentCount;
          for (const value of ribbonValues) values.push(value);
        }
      }
    } catch (_) {
      itemSegmentCount = 0;
      values.length = itemValueOffset;
    }
    if (itemSegmentCount > 0 && values.length > itemValueOffset) {
      segmentCount += itemSegmentCount;
      if (key) renderedKeys.push(key);
    } else if (key) {
      values.length = itemValueOffset;
      missingKeys.push(key);
    }
  }
  return { values, segmentCount, renderedKeys, missingKeys };
}

export function buildSelectionChannelSignature(name, nextItems = []) {
  const itemSignature = item => [
    String(item?.key || ''),
    String(item?.geometryRevision ?? item?.revision ?? 0),
    String(item?.ribbonRevision ?? ''),
    item?.missing ? 'missing' : 'ready',
  ].join('@');
  return `${String(name || '')}:${nextItems.map(itemSignature).join('|')}`;
}
