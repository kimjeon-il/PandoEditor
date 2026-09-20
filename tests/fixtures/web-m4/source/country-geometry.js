(function initializeCountryGeometry(root) {
  'use strict';

  const COORDINATE_TOLERANCE = 1e-10;
  const MIN_RING_AREA = 1e-14;

  function coordinatesNear(left, right, tolerance = COORDINATE_TOLERANCE) {
    return Array.isArray(left) && Array.isArray(right)
      && Math.abs(Number(left[0]) - Number(right[0])) <= tolerance
      && Math.abs(Number(left[1]) - Number(right[1])) <= tolerance;
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
    let ring = ensureClosedRing(rawRing);
    const clockwise = ringSignedArea(ring) < 0;
    if (clockwise !== wantClockwise) ring = ensureClosedRing(ring.slice(0, -1).reverse());
    return ring;
  }

  function multiPolygonCoordinates(value) {
    if (!value) return [];
    if (value.type === 'Polygon') return [value.coordinates || []];
    if (value.type === 'MultiPolygon') return value.coordinates || [];
    return Array.isArray(value) ? value : [];
  }

  function normalizeCountryGeometry(value) {
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

  function hasCanonicalCountryWinding(value) {
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

  root.PandoLabCountryGeometry = Object.freeze({
    ensureClosedRing,
    hasCanonicalCountryWinding,
    normalizeCountryGeometry,
    orientRing,
    ringDistinctCoordinateCount,
    ringSignedArea,
  });
})(globalThis);
