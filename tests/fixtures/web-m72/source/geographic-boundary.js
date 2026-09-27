const STROKE_EPSILON = 1e-7;
export const MAX_BOUNDARY_EDGE_DEGREES = 0.499;

export function normalizeLongitude(value) {
  let longitude = Number(value);
  if (!Number.isFinite(longitude)) return NaN;
  while (longitude > 180) longitude -= 360;
  while (longitude < -180) longitude += 360;
  return longitude;
}

export function normalizeLongitudeNear(value, reference) {
  let longitude = normalizeLongitude(value);
  if (!Number.isFinite(longitude)) return NaN;
  const anchor = Number(reference);
  if (!Number.isFinite(anchor)) return longitude;
  while (longitude - anchor > 180) longitude -= 360;
  while (longitude - anchor < -180) longitude += 360;
  return longitude;
}

function validPoint(point) {
  return Array.isArray(point)
    && Number.isFinite(Number(point[0]))
    && Number.isFinite(Number(point[1]));
}

function atPole(point) {
  return Math.abs(Math.abs(Number(point[1])) - 90) <= STROKE_EPSILON;
}

function atDateLine(point) {
  const longitude = normalizeLongitude(point[0]);
  return Number.isFinite(longitude) && Math.abs(Math.abs(longitude) - 180) <= STROKE_EPSILON;
}

/** True for an edge that closes a polygon through a pole or artificial seam. */
export function isArtificialBoundaryEdge(a, b) {
  if (!validPoint(a) || !validPoint(b)) return true;
  if (Math.abs(Number(a[0]) - Number(b[0])) <= STROKE_EPSILON
    && Math.abs(Number(a[1]) - Number(b[1])) <= STROKE_EPSILON) return true;
  if (atPole(a) || atPole(b)) return true;
  return (atDateLine(a) && atDateLine(b)) && Math.abs(Number(a[1]) - Number(b[1])) > STROKE_EPSILON;
}

function appendEdge(lines, a, b, { closingEdge = false } = {}) {
  if (!validPoint(a) || !validPoint(b) || isArtificialBoundaryEdge(a, b)) return;
  if (closingEdge && atDateLine(a) && atDateLine(b)) return;
  const start = [normalizeLongitude(a[0]), Number(a[1])];
  const endLongitude = normalizeLongitudeNear(b[0], start[0]);
  const end = [endLongitude, Number(b[1])];
  if (!Number.isFinite(endLongitude)) return;
  const delta = endLongitude - start[0];
  // A legitimate antimeridian crossing is split into two short lines. This
  // keeps SVG, Canvas and screen-space WebGL renderers from genericFeature a chord.
  if (Math.abs(delta) > 180) return;
  if (endLongitude > 180 || endLongitude < -180) {
    const seam = delta > 0 ? 180 : -180;
    const ratio = (seam - start[0]) / delta;
    const latitude = start[1] + (end[1] - start[1]) * ratio;
    lines.push([[start[0], start[1]], [seam, latitude]]);
    lines.push([[delta > 0 ? -180 : 180, latitude], [normalizeLongitude(endLongitude), end[1]]]);
    return;
  }
  lines.push([start, [endLongitude, end[1]]]);
}

function appendPolygon(lines, polygon) {
  for (const ring of polygon || []) {
    if (!Array.isArray(ring) || ring.length < 2) continue;
    const first = ring[0];
    const last = ring[ring.length - 1];
    const explicitlyClosed = validPoint(first) && validPoint(last)
      && Math.abs(Number(first[0]) - Number(last[0])) <= STROKE_EPSILON
      && Math.abs(Number(first[1]) - Number(last[1])) <= STROKE_EPSILON;
    const edgeCount = explicitlyClosed ? ring.length - 1 : ring.length;
    for (let index = 0; index < edgeCount; index += 1) {
      const next = (index + 1) % ring.length;
      appendEdge(lines, ring[index], ring[next], { closingEdge: next === 0 });
    }
  }
}

function appendGeometry(lines, geometry) {
  if (!geometry) return;
  if (geometry.type === 'Polygon') appendPolygon(lines, geometry.coordinates);
  else if (geometry.type === 'MultiPolygon') {
    for (const polygon of geometry.coordinates || []) appendPolygon(lines, polygon);
  } else if (geometry.type === 'LineString') {
    const coordinates = geometry.coordinates || [];
    for (let index = 0; index < coordinates.length - 1; index += 1) appendEdge(lines, coordinates[index], coordinates[index + 1]);
  } else if (geometry.type === 'MultiLineString') {
    for (const line of geometry.coordinates || []) {
      for (let index = 0; index < line.length - 1; index += 1) appendEdge(lines, line[index], line[index + 1]);
    }
  } else if (geometry.type === 'GeometryCollection') {
    for (const child of geometry.geometries || []) appendGeometry(lines, child);
  }
}

export function buildRenderableBoundarySegments(value) {
  const lines = [];
  if (value?.type === 'FeatureCollection') {
    for (const feature of value.features || []) appendGeometry(lines, feature?.geometry);
  } else appendGeometry(lines, value?.type === 'Feature' ? value.geometry : value);
  return lines;
}

export function buildRenderableBoundaryGeometry(value) {
  return { type: 'MultiLineString', coordinates: buildRenderableBoundarySegments(value) };
}

export function buildRenderableStrokeFeature(value) {
  return {
    type: 'Feature',
    properties: value?.type === 'Feature' ? (value.properties || {}) : {},
    geometry: buildRenderableBoundaryGeometry(value),
  };
}

export function densifyBoundarySegmentsForProjection(segments, maxEdgeDegrees = MAX_BOUNDARY_EDGE_DEGREES) {
  const limit = Math.max(0.01, Number(maxEdgeDegrees) || MAX_BOUNDARY_EDGE_DEGREES);
  return (segments || []).flatMap(([a, b]) => {
    const distance = Math.max(Math.abs(Number(b[0]) - Number(a[0])), Math.abs(Number(b[1]) - Number(a[1])));
    const count = Math.max(1, Math.ceil(distance / limit));
    if (count === 1) return [[a, b]];
    const pieces = [];
    for (let index = 0; index < count; index += 1) {
      const t0 = index / count;
      const t1 = (index + 1) / count;
      pieces.push([[a[0] + (b[0] - a[0]) * t0, a[1] + (b[1] - a[1]) * t0], [a[0] + (b[0] - a[0]) * t1, a[1] + (b[1] - a[1]) * t1]]);
    }
    return pieces;
  });
}
