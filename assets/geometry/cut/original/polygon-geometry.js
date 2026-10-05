(function initializeCountryGeometry(root) {
  'use strict';

  const COORDINATE_TOLERANCE = 1e-10;
  const MIN_RING_AREA = 1e-14;

  function coordinatesNear(left, right, tolerance = COORDINATE_TOLERANCE) {
    return Array.isArray(left) && Array.isArray(right)
      && Math.abs(Number(left[0]) - Number(right[0])) <= tolerance
      && Math.abs(Number(left[1]) - Number(right[1])) <= tolerance;
  }

  function pointOnSegment(point, start, end, tolerance = COORDINATE_TOLERANCE) {
    const dx = Number(end?.[0]) - Number(start?.[0]);
    const dy = Number(end?.[1]) - Number(start?.[1]);
    const lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= tolerance * tolerance) return coordinatesNear(point, start, tolerance);
    const offsetX = Number(point?.[0]) - Number(start?.[0]);
    const offsetY = Number(point?.[1]) - Number(start?.[1]);
    const position = (offsetX * dx + offsetY * dy) / lengthSquared;
    if (position < -tolerance || position > 1 + tolerance) return false;
    return Math.abs(offsetX * dy - offsetY * dx) <= tolerance * Math.sqrt(lengthSquared);
  }

  function removeCollinearBacktracks(rawRing) {
    const vertices = rawRing.slice(0, -1);
    let changed = true;
    while (changed && vertices.length >= 3) {
      changed = false;
      for (let index = 0; index < vertices.length; index += 1) {
        const previous = vertices[(index + vertices.length - 1) % vertices.length];
        const current = vertices[index];
        const next = vertices[(index + 1) % vertices.length];
        if (pointOnSegment(next, previous, current)
          || pointOnSegment(previous, current, next)) {
          vertices.splice(index, 1);
          changed = true;
          break;
        }
      }
    }
    return vertices.length ? [...vertices, vertices[0].slice()] : [];
  }

  function ensureClosedRing(rawRing) {
    const coordinates = (rawRing || [])
      .filter(coord => Array.isArray(coord) && Number.isFinite(Number(coord[0])) && Number.isFinite(Number(coord[1])))
      .map(coord => [Number(coord[0]), Number(coord[1])]);
    const ring = [];
    for (const coordinate of coordinates) {
      if (!ring.length || !coordinatesNear(ring[ring.length - 1], coordinate)) ring.push(coordinate);
    }
    if (ring.length && !coordinatesNear(ring[0], ring[ring.length - 1])) ring.push(ring[0].slice());
    else if (ring.length > 1) ring[ring.length - 1] = ring[0].slice();
    return ring;
  }

  function normalizeRing(rawRing) {
    const ring = ensureClosedRing(rawRing);
    return ring.length >= 4 ? removeCollinearBacktracks(ring) : ring;
  }

  function ringSignedArea(rawRing) {
    const ring = rawRing || [];
    let sum = 0;
    for (let index = 0; index < ring.length - 1; index += 1) {
      sum += Number(ring[index][0]) * Number(ring[index + 1][1])
        - Number(ring[index + 1][0]) * Number(ring[index][1]);
    }
    return sum / 2;
  }

  function ringDistinctCoordinateCount(rawRing) {
    const ring = rawRing || [];
    const limit = ring.length > 1 && coordinatesNear(ring[0], ring[ring.length - 1]) ? ring.length - 1 : ring.length;
    const keys = new Set();
    for (let index = 0; index < limit; index += 1) {
      const coordinate = ring[index];
      keys.add(`${Math.round(Number(coordinate[0]) / COORDINATE_TOLERANCE)}:${Math.round(Number(coordinate[1]) / COORDINATE_TOLERANCE)}`);
    }
    return keys.size;
  }

  function orientRing(rawRing, wantClockwise) {
    let ring = normalizeRing(rawRing);
    const clockwise = ringSignedArea(ring) < 0;
    if (clockwise !== wantClockwise) ring = normalizeRing(ring.slice(0, -1).reverse());
    return ring;
  }

  function multiPolygonCoordinates(value) {
    if (!value) return [];
    if (value.type === 'Polygon') return [value.coordinates || []];
    if (value.type === 'MultiPolygon') return value.coordinates || [];
    return Array.isArray(value) ? value : [];
  }

  function normalizePolygonGeometry(value) {
    const polygons = multiPolygonCoordinates(value).map(polygon => {
      const outer = orientRing(polygon?.[0], true);
      if (outer.length < 4 || ringDistinctCoordinateCount(outer) < 3 || Math.abs(ringSignedArea(outer)) <= MIN_RING_AREA) return null;
      const holes = (polygon || []).slice(1)
        .map(ring => orientRing(ring, false))
        .filter(ring => ring.length >= 4 && ringDistinctCoordinateCount(ring) >= 3 && Math.abs(ringSignedArea(ring)) > MIN_RING_AREA);
      return [outer, ...holes];
    }).filter(Boolean);
    if (!polygons.length) return null;
    return polygons.length === 1
      ? { type: 'Polygon', coordinates: polygons[0] }
      : { type: 'MultiPolygon', coordinates: polygons };
  }

  function hasCanonicalPolygonWinding(value) {
    const polygons = multiPolygonCoordinates(value);
    if (!polygons.length) return false;
    return polygons.every(polygon => Array.isArray(polygon) && polygon.length
      && polygon.every((rawRing, index) => {
        const ring = ensureClosedRing(rawRing);
        if (ring.length !== rawRing?.length || ring.length < 4 || ringDistinctCoordinateCount(ring) < 3) return false;
        const area = ringSignedArea(ring);
        return index === 0 ? area < -MIN_RING_AREA : area > MIN_RING_AREA;
      }));
  }

  root.PandoLabPolygonGeometry = Object.freeze({
    ensureClosedRing,
    hasCanonicalPolygonWinding,
    normalizePolygonGeometry,
    orientRing,
    ringDistinctCoordinateCount,
    ringSignedArea,
  });
})(globalThis);
