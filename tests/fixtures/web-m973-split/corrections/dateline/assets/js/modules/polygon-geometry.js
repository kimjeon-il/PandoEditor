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

  // Clipping may remove collinear vertices and leave a >180-degree planar
  // edge. Retain its path with intermediate vertices so subsequent geographic
  // wrapping cannot reinterpret that edge as the shorter route across the seam.
  function normalizeClippedPolygonGeometry(value) {
    const normalized = normalizePolygonGeometry(value);
    if (!normalized) return null;
    const polygons = multiPolygonCoordinates(normalized).map(polygon => polygon.map(ring => {
      const segmented = [ring[0]];
      for (let index = 1; index < ring.length; index += 1) {
        const a = ring[index - 1], b = ring[index];
        const count = Math.max(1, Math.ceil(Math.abs(b[0] - a[0]) / 180));
        for (let part = 1; part < count; part += 1) segmented.push([
          a[0] + (b[0] - a[0]) * part / count, a[1] + (b[1] - a[1]) * part / count,
        ]);
        segmented.push(b);
      }
      return segmented;
    }));
    return normalized.type === 'Polygon' ? { type: 'Polygon', coordinates: polygons[0] }
      : { type: 'MultiPolygon', coordinates: polygons };
  }

  // Boolean operations are planar. Put raw dateline rings and already split
  // candidates in the same longitude strips without changing source snapshots.
  function wrapPolygonGeometry(geometry, clipper) {
    if (!geometry) return geometry;
    const polygons = multiPolygonCoordinates(geometry);
    const crossesStrip = polygons.some(polygon => polygon.some(ring => ring.some(([x], index) => {
      const jump = index ? Math.abs(x - ring[index - 1][0]) : 0;
      return x < -180 || x > 180 || (jump > 180 && jump < 360);
    })));
    if (!crossesStrip) return geometry;
    let changed = false;
    const result = [];
    for (const polygon of polygons) {
      const reference = polygon[0]?.[0]?.[0];
      const unwrapped = polygon.map(ring => {
        let previous = reference;
        return ring.map(([longitude, latitude], index) => {
          let x = longitude;
          // Exact full-world edges are already a deliberate seam, not a wrap.
          const fullWorldEdge = index > 0 && Math.abs(longitude - ring[index - 1][0]) === 360;
          if (!fullWorldEdge) {
            while (x - previous > 180) x -= 360;
            while (x - previous < -180) x += 360;
          }
          previous = x;
          return [x, latitude];
        });
      });
      let west = Infinity, east = -Infinity;
      for (const ring of unwrapped) for (const [x] of ring) {
        west = Math.min(west, x); east = Math.max(east, x);
      }
      const unchanged = unwrapped.every((ring, ri) => ring.every((point, i) => point[0] === polygon[ri][i][0]));
      if (unchanged && west >= -180 && east <= 180) { result.push(polygon); continue; }
      changed = true;
      // A full-world seam shell can contain both longitude copies of a hole
      // that crosses the seam. Subtract every overlapping periodic copy before
      // clipping; treating it as a single planar hole would fill its other half.
      let outerWest = Infinity, outerEast = -Infinity;
      for (const [x] of unwrapped[0]) { outerWest = Math.min(outerWest, x); outerEast = Math.max(outerEast, x); }
      const holes = [];
      let periodicHoles = false;
      for (const ring of unwrapped.slice(1)) {
        let holeWest = Infinity, holeEast = -Infinity;
        for (const [x] of ring) { holeWest = Math.min(holeWest, x); holeEast = Math.max(holeEast, x); }
        const firstCopy = Math.ceil((outerWest - holeEast) / 360), lastCopy = Math.floor((outerEast - holeWest) / 360);
        if (firstCopy !== 0 || lastCopy !== 0) periodicHoles = true;
        for (let copy = firstCopy; copy <= lastCopy; copy += 1) holes.push([[ring.map(([x, y]) => [x + copy * 360, y])]]);
      }
      const component = periodicHoles && holes.length ? clipper.difference([[unwrapped[0]]], ...holes) : [unwrapped];
      const first = Math.floor((west + 180) / 360), last = Math.floor((east + 180) / 360);
      for (let strip = first; strip <= last; strip += 1) {
        const left = -180 + strip * 360, right = 180 + strip * 360;
        const clipped = clipper.intersection(component, [[[[left, -90], [right, -90], [right, 90], [left, 90], [left, -90]]]]);
        const normalized = normalizeClippedPolygonGeometry(clipped.map(part => part.map(ring => ring.map(([x, y]) => [x - strip * 360, y]))));
        result.push(...multiPolygonCoordinates(normalized));
      }
    }
    if (!changed) return geometry;
    if (!result.length) return null;
    return result.length === 1 ? { type: 'Polygon', coordinates: result[0] }
      : { type: 'MultiPolygon', coordinates: result };
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
    normalizeClippedPolygonGeometry,
    wrapPolygonGeometry,
    orientRing,
    ringDistinctCoordinateCount,
    ringSignedArea,
  });
})(globalThis);
