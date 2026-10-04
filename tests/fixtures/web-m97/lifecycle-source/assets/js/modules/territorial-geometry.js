
function multiCoordinates(geometry) {
  if (geometry?.type === 'Polygon') return [geometry.coordinates || []];
  if (geometry?.type === 'MultiPolygon') return geometry.coordinates || [];
  return [];
}

function coordinateKey(coord, precision = 6) {
  return `${Number(coord?.[0]).toFixed(precision)},${Number(coord?.[1]).toFixed(precision)}`;
}

function finitePoint(point) {
  return Array.isArray(point) && Number.isFinite(point[0]) && Number.isFinite(point[1]);
}

function projectedPoint(project, coordinate) {
  try {
    const point = project(coordinate);
    return finitePoint(point) ? point : null;
  } catch (_) {
    return null;
  }
}

export function snapLineEndpointsToBoundary(rawLine, geometry, {
  project,
  maxDistance = 10,
  isVisible = () => true,
  maxSegmentLength = Number.POSITIVE_INFINITY,
} = {}) {
  const line = (rawLine || []).map(coordinate => [Number(coordinate?.[0]), Number(coordinate?.[1])]);
  const snaps = { start: null, end: null };
  if (!line.length || typeof project !== 'function' || !Number.isFinite(maxDistance) || maxDistance < 0) return { line, snaps };

  const endpointEntries = line.length === 1
    ? [['start', 0]]
    : [['start', 0], ['end', line.length - 1]];
  const polygons = multiCoordinates(geometry);

  for (const [name, lineIndex] of endpointEntries) {
    const rawCoordinate = line[lineIndex];
    const rawPoint = projectedPoint(project, rawCoordinate);
    if (!rawPoint) continue;
    let nearest = null;

    for (let polygonIndex = 0; polygonIndex < polygons.length; polygonIndex += 1) {
      const outerRing = polygons[polygonIndex]?.[0] || [];
      if (outerRing.length < 2) continue;
      const closed = coordinateKey(outerRing[0], 10) === coordinateKey(outerRing[outerRing.length - 1], 10);
      const segmentCount = closed ? outerRing.length - 1 : outerRing.length;
      for (let segmentIndex = 0; segmentIndex < segmentCount; segmentIndex += 1) {
        const a = outerRing[segmentIndex];
        const b = outerRing[(segmentIndex + 1) % outerRing.length];
        if (!finitePoint(a) || !finitePoint(b) || (!isVisible(a) && !isVisible(b))) continue;
        const projectedA = projectedPoint(project, a);
        const projectedB = projectedPoint(project, b);
        if (!projectedA || !projectedB) continue;
        const vx = projectedB[0] - projectedA[0];
        const vy = projectedB[1] - projectedA[1];
        const lengthSquared = vx * vx + vy * vy;
        if (!lengthSquared || Math.sqrt(lengthSquared) > maxSegmentLength) continue;
        const t = Math.max(0, Math.min(1,
          ((rawPoint[0] - projectedA[0]) * vx + (rawPoint[1] - projectedA[1]) * vy) / lengthSquared,
        ));
        const screenCoordinate = [projectedA[0] + vx * t, projectedA[1] + vy * t];
        const distance = Math.hypot(rawPoint[0] - screenCoordinate[0], rawPoint[1] - screenCoordinate[1]);
        if (distance > maxDistance || (nearest && distance >= nearest.distance)) continue;
        nearest = {
          coordinate: [Number(a[0]) + (Number(b[0]) - Number(a[0])) * t, Number(a[1]) + (Number(b[1]) - Number(a[1])) * t],
          distance,
          polygonIndex,
          segmentIndex,
        };
      }
    }

    if (!nearest) continue;
    line[lineIndex] = nearest.coordinate.slice();
    snaps[name] = nearest;
  }

  return { line, snaps };
}
