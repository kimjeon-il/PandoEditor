const RENDER_LOD_LEVELS = Object.freeze({
  COARSE: 'coarse',
  MEDIUM: 'medium',
  HIGH: 'high',
});

function finiteCoordinate(value) {
  return Array.isArray(value) && value.length >= 2
    && Number.isFinite(Number(value[0])) && Number.isFinite(Number(value[1]));
}

function cloneCoordinate(value) {
  return [Number(value[0]), Number(value[1])];
}

function hasDatelineJump(coordinates) {
  for (let index = 1; index < coordinates.length; index += 1) {
    if (Math.abs(Number(coordinates[index][0]) - Number(coordinates[index - 1][0])) > 180) return true;
  }
  return false;
}

function distanceToSegmentSquared(point, start, end) {
  const dx = end[0] - start[0];
  const dy = end[1] - start[1];
  if (dx === 0 && dy === 0) return (point[0] - start[0]) ** 2 + (point[1] - start[1]) ** 2;
  const ratio = Math.max(0, Math.min(1, ((point[0] - start[0]) * dx + (point[1] - start[1]) * dy) / (dx * dx + dy * dy)));
  const x = start[0] + ratio * dx;
  const y = start[1] + ratio * dy;
  return (point[0] - x) ** 2 + (point[1] - y) ** 2;
}

function simplifyOpenLine(coordinates, tolerance) {
  if (coordinates.length <= 2 || tolerance <= 0 || hasDatelineJump(coordinates)) return coordinates.map(cloneCoordinate);
  const threshold = tolerance * tolerance;
  const keep = new Uint8Array(coordinates.length);
  keep[0] = keep[coordinates.length - 1] = 1;
  const stack = [[0, coordinates.length - 1]];
  while (stack.length) {
    const [startIndex, endIndex] = stack.pop();
    let furthest = -1;
    let maximum = threshold;
    for (let index = startIndex + 1; index < endIndex; index += 1) {
      const distance = distanceToSegmentSquared(coordinates[index], coordinates[startIndex], coordinates[endIndex]);
      if (distance > maximum) {
        maximum = distance;
        furthest = index;
      }
    }
    if (furthest < 0) continue;
    keep[furthest] = 1;
    stack.push([startIndex, furthest], [furthest, endIndex]);
  }
  return coordinates.filter((_coordinate, index) => keep[index]).map(cloneCoordinate);
}

function densifyLine(coordinates, maxEdgeDegrees) {
  if (!(maxEdgeDegrees > 0) || coordinates.length < 2) return coordinates;
  const result = [cloneCoordinate(coordinates[0])];
  for (let index = 1; index < coordinates.length; index += 1) {
    const start = coordinates[index - 1];
    const end = coordinates[index];
    let deltaLongitude = Number(end[0]) - Number(start[0]);
    while (deltaLongitude > 180) deltaLongitude -= 360;
    while (deltaLongitude < -180) deltaLongitude += 360;
    const deltaLatitude = Number(end[1]) - Number(start[1]);
    const steps = Math.max(1, Math.ceil(Math.hypot(deltaLongitude, deltaLatitude) / maxEdgeDegrees));
    for (let step = 1; step <= steps; step += 1) {
      const ratio = step / steps;
      let longitude = Number(start[0]) + deltaLongitude * ratio;
      while (longitude > 180) longitude -= 360;
      while (longitude < -180) longitude += 360;
      result.push([longitude, Number(start[1]) + deltaLatitude * ratio]);
    }
  }
  return result;
}

function simplifyLine(coordinates, tolerance, { closed = false, maxEdgeDegrees = 0 } = {}) {
  const source = (coordinates || []).filter(finiteCoordinate).map(cloneCoordinate);
  if (!source.length) return [];
  if (!closed) return densifyLine(simplifyOpenLine(source, tolerance), maxEdgeDegrees);
  const open = source.length > 1 && source[0][0] === source[source.length - 1][0] && source[0][1] === source[source.length - 1][1]
    ? source.slice(0, -1) : source;
  if (open.length < 3) return [];
  const simplified = simplifyOpenLine([...open, open[0]], tolerance);
  const ring = simplified.length >= 4 ? simplified : [...open.map(cloneCoordinate), cloneCoordinate(open[0])];
  const dense = densifyLine(ring, maxEdgeDegrees);
  if (dense.length > 1) dense[dense.length - 1] = cloneCoordinate(dense[0]);
  return dense.length >= 4 ? dense : [];
}

export function resolveRenderLod({ requested = 'high', policy = 'exact', protected: protectedGeometry = false } = {}) {
  if (protectedGeometry || policy !== 'independent') return RENDER_LOD_LEVELS.HIGH;
  return Object.values(RENDER_LOD_LEVELS).includes(requested) ? requested : RENDER_LOD_LEVELS.HIGH;
}

export function simplifyRenderGeometry(geometry, {
  lod = RENDER_LOD_LEVELS.HIGH,
  policy = 'exact',
  projection = 'flat',
} = {}) {
  const level = resolveRenderLod({ requested: lod, policy });
  if (!geometry || level === RENDER_LOD_LEVELS.HIGH || policy !== 'independent') return geometry;
  const tolerance = level === RENDER_LOD_LEVELS.COARSE ? 0.075 : 0.018;
  const maxEdgeDegrees = projection === 'globe' ? (level === RENDER_LOD_LEVELS.COARSE ? 4 : 2.5) : 0;
  const line = coordinates => simplifyLine(coordinates, tolerance, { maxEdgeDegrees });
  const ring = coordinates => simplifyLine(coordinates, tolerance, { closed: true, maxEdgeDegrees });
  if (geometry.type === 'LineString') return { type: 'LineString', coordinates: line(geometry.coordinates) };
  if (geometry.type === 'MultiLineString') return {
    type: 'MultiLineString', coordinates: (geometry.coordinates || []).map(line).filter(part => part.length >= 2),
  };
  if (geometry.type === 'Polygon') return {
    type: 'Polygon', coordinates: (geometry.coordinates || []).map(ring).filter(part => part.length >= 4),
  };
  if (geometry.type === 'MultiPolygon') return {
    type: 'MultiPolygon',
    coordinates: (geometry.coordinates || []).map(polygon => (polygon || []).map(ring).filter(part => part.length >= 4))
      .filter(polygon => polygon.length),
  };
  return geometry;
}
