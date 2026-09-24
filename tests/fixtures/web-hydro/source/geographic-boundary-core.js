'use strict';

((scope) => {
  const EPSILON = 1e-7;
  function normalizeLongitude(value) {
    let longitude = Number(value);
    if (!Number.isFinite(longitude)) return NaN;
    while (longitude > 180) longitude -= 360;
    while (longitude < -180) longitude += 360;
    return longitude;
  }
  function validPoint(point) {
    return Array.isArray(point)
      && Number.isFinite(Number(point[0]))
      && Number.isFinite(Number(point[1]));
  }
  function isArtificialBoundaryEdge(a, b) {
    if (!validPoint(a) || !validPoint(b)) return true;
    if (Math.abs(Number(a[0]) - Number(b[0])) <= EPSILON
      && Math.abs(Number(a[1]) - Number(b[1])) <= EPSILON) return true;
    if (Math.abs(Math.abs(Number(a[1])) - 90) <= EPSILON
      || Math.abs(Math.abs(Number(b[1])) - 90) <= EPSILON) return true;
    const aLongitude = normalizeLongitude(a[0]);
    const bLongitude = normalizeLongitude(b[0]);
    const aAtSeam = Math.abs(Math.abs(aLongitude) - 180) <= EPSILON;
    const bAtSeam = Math.abs(Math.abs(bLongitude) - 180) <= EPSILON;
    return aAtSeam && bAtSeam && Math.abs(Number(a[1]) - Number(b[1])) > EPSILON;
  }
  function appendEdge(lines, a, b, closingEdge) {
    if (!validPoint(a) || !validPoint(b) || isArtificialBoundaryEdge(a, b)) return;
    const start = [normalizeLongitude(a[0]), Number(a[1])];
    let endLongitude = normalizeLongitude(b[0]);
    while (endLongitude - start[0] > 180) endLongitude -= 360;
    while (endLongitude - start[0] < -180) endLongitude += 360;
    if (closingEdge && Math.abs(Math.abs(start[0]) - 180) <= EPSILON
      && Math.abs(Math.abs(endLongitude) - 180) <= EPSILON) return;
    const delta = endLongitude - start[0];
    if (endLongitude > 180 || endLongitude < -180) {
      const seam = delta > 0 ? 180 : -180;
      const ratio = (seam - start[0]) / delta;
      const latitude = start[1] + (Number(b[1]) - start[1]) * ratio;
      lines.push([[start[0], start[1]], [seam, latitude]]);
      lines.push([[delta > 0 ? -180 : 180, latitude], [normalizeLongitude(endLongitude), Number(b[1])]]);
    } else lines.push([start, [endLongitude, Number(b[1])]]);
  }
  function buildRenderableBoundarySegments(value) {
    const lines = [];
    const appendGeometry = geometry => {
      if (!geometry) return;
      if (geometry.type === 'Polygon' || geometry.type === 'MultiPolygon') {
        const polygons = geometry.type === 'Polygon' ? [geometry.coordinates] : geometry.coordinates || [];
        for (const polygon of polygons) for (const ring of polygon || []) {
          if (!Array.isArray(ring) || ring.length < 2) continue;
          const first = ring[0]; const last = ring[ring.length - 1];
          const closed = validPoint(first) && validPoint(last)
            && Math.abs(Number(first[0]) - Number(last[0])) <= EPSILON
            && Math.abs(Number(first[1]) - Number(last[1])) <= EPSILON;
          const count = closed ? ring.length - 1 : ring.length;
          for (let index = 0; index < count; index += 1) appendEdge(lines, ring[index], ring[(index + 1) % ring.length], index + 1 === ring.length);
        }
      } else if (geometry.type === 'LineString' || geometry.type === 'MultiLineString') {
        const parts = geometry.type === 'LineString' ? [geometry.coordinates || []] : geometry.coordinates || [];
        for (const part of parts) for (let index = 0; index < part.length - 1; index += 1) appendEdge(lines, part[index], part[index + 1], false);
      } else if (geometry.type === 'GeometryCollection') for (const child of geometry.geometries || []) appendGeometry(child);
    };
    if (value?.type === 'FeatureCollection') for (const feature of value.features || []) appendGeometry(feature?.geometry);
    else appendGeometry(value?.type === 'Feature' ? value.geometry : value);
    return lines;
  }
  scope.PandoLabGeographicBoundary = { normalizeLongitude, isArtificialBoundaryEdge, buildRenderableBoundarySegments };
})(self);
