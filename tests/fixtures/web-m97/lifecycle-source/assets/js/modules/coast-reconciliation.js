const EARTH_RADIUS_METERS = 6371008.8;

const COAST_RECONCILIATION_DEFAULTS = Object.freeze({
  searchDistanceMeters: 2500,
  alignedToleranceMeters: 250,
  minMatchLengthMeters: 500,
  sampleCount: 5,
});

const clone = value => structuredClone(value);
const text = value => String(value ?? '').trim();

export function normalizeCoastDecision(choice) {
  const direction = String(choice?.direction || 'cancel');
  if (!['cancel', 'country-to-admin', 'admin-to-country', 'independent'].includes(direction)) {
    throw new Error('해안선 정합 선택이 올바르지 않습니다.');
  }
  return direction;
}

function polygonsFor(geometry) {
  if (geometry?.type === 'Polygon') return [geometry.coordinates || []];
  if (geometry?.type === 'MultiPolygon') return geometry.coordinates || [];
  return [];
}

function openRing(ring) {
  if (!Array.isArray(ring)) return [];
  const points = ring
    .filter(point => Array.isArray(point) && Number.isFinite(Number(point[0])) && Number.isFinite(Number(point[1])))
    .map(point => [Number(point[0]), Number(point[1])]);
  if (points.length > 1 && points[0][0] === points.at(-1)[0] && points[0][1] === points.at(-1)[1]) points.pop();
  return points;
}

function longitudeDelta(a, b) {
  let delta = Number(a) - Number(b);
  while (delta > 180) delta -= 360;
  while (delta < -180) delta += 360;
  return delta;
}

function normalizeLongitude(value) {
  let longitude = Number(value);
  while (longitude > 180) longitude -= 360;
  while (longitude < -180) longitude += 360;
  return longitude;
}

function localMetricDistance(a, b, referenceLatitude = ((Number(a?.[1]) || 0) + (Number(b?.[1]) || 0)) / 2) {
  if (!Array.isArray(a) || !Array.isArray(b)) return Number.POSITIVE_INFINITY;
  const latitude = Number(referenceLatitude) * Math.PI / 180;
  const dx = longitudeDelta(a[0], b[0]) * Math.PI / 180 * Math.cos(latitude) * EARTH_RADIUS_METERS;
  const dy = (Number(a[1]) - Number(b[1])) * Math.PI / 180 * EARTH_RADIUS_METERS;
  return Math.hypot(dx, dy);
}

function segmentLength(segment) {
  return localMetricDistance(segment.a, segment.b);
}

function midpoint(a, b) {
  const delta = longitudeDelta(b[0], a[0]);
  return [Number(a[0]) + delta / 2, (Number(a[1]) + Number(b[1])) / 2];
}

function segmentDistance(segment, point) {
  const latitude = ((Number(segment.a?.[1]) || 0) + (Number(segment.b?.[1]) || 0) + (Number(point?.[1]) || 0)) / 3;
  const scaleX = Math.cos(latitude * Math.PI / 180) * Math.PI / 180 * EARTH_RADIUS_METERS;
  const scaleY = Math.PI / 180 * EARTH_RADIUS_METERS;
  const bx = longitudeDelta(segment.b[0], segment.a[0]) * scaleX;
  const by = (Number(segment.b[1]) - Number(segment.a[1])) * scaleY;
  const px = longitudeDelta(point[0], segment.a[0]) * scaleX;
  const py = (Number(point[1]) - Number(segment.a[1])) * scaleY;
  const length2 = bx * bx + by * by;
  const t = length2 > 0 ? Math.max(0, Math.min(1, (px * bx + py * by) / length2)) : 0;
  return Math.hypot(px - bx * t, py - by * t);
}

function extractExteriorSegments(feature) {
  const output = [];
  for (let polygonIndex = 0; polygonIndex < polygonsFor(feature?.geometry).length; polygonIndex += 1) {
    const polygon = polygonsFor(feature.geometry)[polygonIndex];
    const ring = openRing(polygon?.[0]);
    for (let index = 0; index < ring.length; index += 1) {
      const nextIndex = (index + 1) % ring.length;
      if (ring.length < 3 || !ring[nextIndex]) continue;
      output.push({
        id: `${text(feature?.id)}:${polygonIndex}:0:${index}`,
        featureId: text(feature?.id),
        polygonIndex,
        ringIndex: 0,
        index,
        a: ring[index],
        b: ring[nextIndex],
        lengthMeters: localMetricDistance(ring[index], ring[nextIndex]),
      });
    }
  }
  return output;
}

function countryCoastSegments(countryFeature, topology) {
  const countryId = text(countryFeature?.id);
  const segments = [...(topology?.segments?.values?.() || [])]
    .filter(segment => segment.kind === 'coast'
      && segment.ownerIds?.size === 1
      && [...segment.ownerIds][0] === countryId
      && (segment.refs || []).some(ref => Number(ref.ringIndex) === 0))
    .map((segment, index) => {
      const ref = (segment.refs || []).find(candidate => candidate.featureId === countryId && Number(candidate.ringIndex) === 0) || segment.refs?.[0];
      const interpolate = t => [
        normalizeLongitude(Number(ref?.a?.[0]) + longitudeDelta(ref?.b?.[0], ref?.a?.[0]) * Number(t)),
        Number(ref?.a?.[1]) + (Number(ref?.b?.[1]) - Number(ref?.a?.[1])) * Number(t),
      ];
      const a = ref ? interpolate(ref.startT ?? 0) : [...segment.a];
      const b = ref ? interpolate(ref.endT ?? 1) : [...segment.b];
      return {
      id: segment.key || `${countryId}:coast:${index}`,
      featureId: countryId,
      polygonIndex: ref?.polygonIndex ?? 0,
      ringIndex: ref?.ringIndex ?? 0,
      index: ref?.segmentIndex ?? index,
      order: Number(ref?.segmentIndex ?? index) + Number(ref?.startT ?? 0),
      a,
      b,
      lengthMeters: localMetricDistance(a, b),
    };
    });
  return segments;
}

function orientationScore(admin, country) {
  const adminDx = longitudeDelta(admin.b[0], admin.a[0]);
  const adminDy = Number(admin.b[1]) - Number(admin.a[1]);
  const countryDx = longitudeDelta(country.b[0], country.a[0]);
  const countryDy = Number(country.b[1]) - Number(country.a[1]);
  const left = Math.hypot(adminDx, adminDy) || 1;
  const right = Math.hypot(countryDx, countryDy) || 1;
  return Math.abs((adminDx * countryDx + adminDy * countryDy) / left / right);
}

function chainLength(chain) {
  return (chain || []).reduce((sum, segment) => sum + segmentLength(segment), 0);
}

function chainCoordinates(chain) {
  if (!chain?.length) return [];
  const coordinates = [chain[0].a];
  for (const segment of chain) coordinates.push(segment.b);
  return coordinates.map(point => [...point]);
}

function chooseCountrySegment(adminSegment, candidates, searchDistanceMeters) {
  const center = midpoint(adminSegment.a, adminSegment.b);
  const matches = [];
  for (const candidate of candidates) {
    const distance = segmentDistance(candidate, center);
    if (distance > Number(searchDistanceMeters)) continue;
    const orientation = orientationScore(adminSegment, candidate);
    if (orientation < 0.25) continue;
    const score = distance - orientation * 100;
    matches.push({ candidate, distance, orientation, score });
  }
  matches.sort((left, right) => left.score - right.score);
  const best = matches[0] || null;
  if (!best) return null;
  const ambiguous = matches.slice(1).some(match => (
    (match.candidate.polygonIndex !== best.candidate.polygonIndex || match.candidate.ringIndex !== best.candidate.ringIndex)
    && match.score <= best.score + Math.max(100, best.distance * 0.1)
  ));
  return ambiguous ? null : best;
}

function segmentPairDistance(left, right) {
  return Math.min(
    segmentDistance(left, right.a),
    segmentDistance(left, right.b),
    segmentDistance(right, left.a),
    segmentDistance(right, left.b),
    segmentDistance(left, midpoint(right.a, right.b)),
    segmentDistance(right, midpoint(left.a, left.b)),
  );
}

function resampleCoordinates(coordinates, spacingMeters = 100) {
  if (!Array.isArray(coordinates) || coordinates.length < 2) return [];
  const lengths = [];
  let total = 0;
  for (let index = 1; index < coordinates.length; index += 1) {
    total += localMetricDistance(coordinates[index - 1], coordinates[index]);
    lengths.push(total);
  }
  const sampleCount = Math.max(2, Math.min(512, Math.ceil(total / Math.max(25, Number(spacingMeters) || 100)) + 1));
  const output = [];
  let segmentIndex = 0;
  for (let index = 0; index < sampleCount; index += 1) {
    const wanted = total * index / (sampleCount - 1);
    while (segmentIndex < lengths.length - 1 && lengths[segmentIndex] < wanted) segmentIndex += 1;
    const previousLength = segmentIndex ? lengths[segmentIndex - 1] : 0;
    const segmentLengthMeters = Math.max(1e-9, lengths[segmentIndex] - previousLength);
    const t = Math.max(0, Math.min(1, (wanted - previousLength) / segmentLengthMeters));
    const a = coordinates[segmentIndex];
    const b = coordinates[segmentIndex + 1];
    output.push([
      Number(a[0]) + longitudeDelta(b[0], a[0]) * t,
      Number(a[1]) + (Number(b[1]) - Number(a[1])) * t,
    ]);
  }
  return output;
}

function orientedCountryChain(adminChain, countryChain) {
  const adminCoordinates = chainCoordinates(adminChain);
  const forward = chainCoordinates(countryChain);
  const reversed = [...forward].reverse();
  const endpointCost = coordinates => localMetricDistance(adminCoordinates[0], coordinates[0])
    + localMetricDistance(adminCoordinates.at(-1), coordinates.at(-1));
  return endpointCost(reversed) < endpointCost(forward)
    ? [...countryChain].reverse().map(segment => ({ ...segment, a: [...segment.b], b: [...segment.a] }))
    : countryChain;
}

function deviationMetrics(adminChain, rawCountryChain, sampleSpacingMeters = 100) {
  const countryChain = orientedCountryChain(adminChain, rawCountryChain);
  const adminSamples = resampleCoordinates(chainCoordinates(adminChain), sampleSpacingMeters);
  const countrySamples = resampleCoordinates(chainCoordinates(countryChain), sampleSpacingMeters);
  const count = Math.max(2, Math.min(adminSamples.length, countrySamples.length));
  const deviations = [];
  for (let index = 0; index < count; index += 1) {
    const left = adminSamples[Math.round(index * (adminSamples.length - 1) / (count - 1))];
    const right = countrySamples[Math.round(index * (countrySamples.length - 1) / (count - 1))];
    deviations.push(localMetricDistance(left, right));
  }
  const matchedLength = Math.min(chainLength(adminChain), chainLength(countryChain));
  const sorted = [...deviations].sort((left, right) => left - right);
  return {
    matchedLength,
    meanDeviation: deviations.reduce((sum, value) => sum + value, 0) / deviations.length,
    maxDeviation: Math.max(...deviations),
    p95Deviation: sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * 0.95))],
    countryChain,
  };
}

function chainCanContinue(previous, next, direction) {
  if (previous.adminSegment.polygonIndex !== next.adminSegment.polygonIndex
      || previous.adminSegment.ringIndex !== next.adminSegment.ringIndex
      || next.adminSegment.index !== previous.adminSegment.index + 1) return false;
  if (previous.countrySegment.polygonIndex !== next.countrySegment.polygonIndex
      || previous.countrySegment.ringIndex !== next.countrySegment.ringIndex) return false;
  const delta = Number(next.countrySegment.order ?? next.countrySegment.index) - Number(previous.countrySegment.order ?? previous.countrySegment.index);
  if (Math.abs(delta) > 2 || Math.abs(delta) < 1e-8) return false;
  return !direction || Math.sign(delta) === direction;
}

export function analyzeAdminCountryCoast({
  adminFeature,
  countryFeature,
  countryTopology,
  tolerance = {},
} = {}) {
  const options = { ...COAST_RECONCILIATION_DEFAULTS, ...tolerance };
  const adminSegments = extractExteriorSegments(adminFeature);
  const countrySegments = countryCoastSegments(countryFeature, countryTopology);
  if (!countrySegments.length) {
    const exterior = extractExteriorSegments(countryFeature);
    const touchesUnclassifiedBoundary = adminSegments.some(adminSegment => exterior.some(countrySegment => (
      segmentPairDistance(adminSegment, countrySegment) <= Number(options.searchDistanceMeters)
    )));
    return touchesUnclassifiedBoundary
      ? { status: 'unavailable', unavailableReason: 'country-coast-not-found', conflicts: [] }
      : { status: 'ready', conflicts: [] };
  }
  const matches = [];
  for (const adminSegment of adminSegments) {
    const match = chooseCountrySegment(adminSegment, countrySegments, options.searchDistanceMeters);
    if (!match) continue;
    matches.push({ adminSegment, countrySegment: match.candidate, distance: match.distance, orientation: match.orientation });
  }

  const orderedMatches = matches.sort((left, right) => left.adminSegment.polygonIndex - right.adminSegment.polygonIndex
    || left.adminSegment.ringIndex - right.adminSegment.ringIndex
    || left.adminSegment.index - right.adminSegment.index);
  const chains = [];
  for (const match of orderedMatches) {
    const current = chains.at(-1);
    const previous = current?.matches?.at(-1);
    const delta = previous ? Number(match.countrySegment.order ?? match.countrySegment.index) - Number(previous.countrySegment.order ?? previous.countrySegment.index) : 0;
    const direction = current?.direction || Math.sign(delta);
    if (!previous || !chainCanContinue(previous, match, direction)) chains.push({ matches: [match], direction: 0 });
    else {
      current.matches.push(match);
      if (!current.direction) current.direction = Math.sign(delta);
    }
  }

  const conflicts = [];
  for (const row of chains) {
    const adminChain = row.matches.map(match => match.adminSegment);
    const rawCountryChain = row.matches.map(match => match.countrySegment);
    const metrics = deviationMetrics(adminChain, rawCountryChain, options.sampleSpacingMeters || 100);
    const countryChain = metrics.countryChain;
    if (metrics.matchedLength < Number(options.minMatchLengthMeters)) continue;
    if (metrics.maxDeviation <= Number(options.alignedToleranceMeters)) continue;
    const firstAdmin = adminChain[0];
    const lastAdmin = adminChain.at(-1);
    const firstCountry = countryChain[0];
    const lastCountry = countryChain.at(-1);
    const points = [...adminChain, ...countryChain].flatMap(segment => [segment.a, segment.b]);
    conflicts.push({
      id: `${text(adminFeature?.id)}:${firstAdmin.polygonIndex}:${firstAdmin.index}`,
      adminChain,
      countryChain,
      adminRingRef: { polygonIndex: firstAdmin.polygonIndex, ringIndex: firstAdmin.ringIndex, startIndex: firstAdmin.index, endIndex: lastAdmin.index },
      countryRingRef: { polygonIndex: firstCountry.polygonIndex, ringIndex: firstCountry.ringIndex, startIndex: Math.min(firstCountry.index, lastCountry.index), endIndex: Math.max(firstCountry.index, lastCountry.index) },
      countryGeometry: clone(countryFeature.geometry),
      adminGeometry: clone(adminFeature.geometry),
      startAnchor: [...firstAdmin.a],
      endAnchor: [...lastAdmin.b],
      bounds: [
        Math.min(...points.map(point => point[0])),
        Math.min(...points.map(point => point[1])),
        Math.max(...points.map(point => point[0])),
        Math.max(...points.map(point => point[1])),
      ],
      matchedLength: metrics.matchedLength,
      meanDeviation: metrics.meanDeviation,
      maxDeviation: metrics.maxDeviation,
      p95Deviation: metrics.p95Deviation,
    });
  }
  return { status: 'ready', conflicts };
}

function sameCoordinate(left, right, epsilon = 1e-10) {
  return Array.isArray(left) && Array.isArray(right)
    && Math.abs(longitudeDelta(left[0], right[0])) <= epsilon
    && Math.abs(Number(left[1]) - Number(right[1])) <= epsilon;
}

function dedupeCoordinates(coordinates) {
  const output = [];
  for (const point of coordinates || []) {
    if (!Array.isArray(point) || !Number.isFinite(Number(point[0])) || !Number.isFinite(Number(point[1]))) continue;
    const next = [Number(point[0]), Number(point[1])];
    if (!sameCoordinate(output.at(-1), next)) output.push(next);
  }
  return output;
}

function orientedReplacement(sourceChain, startPoint, endPoint) {
  const forward = dedupeCoordinates(chainCoordinates(sourceChain));
  if (forward.length < 2) throw new Error('교체할 해안선 구간이 비어 있습니다.');
  const reversed = [...forward].reverse();
  const cost = coordinates => localMetricDistance(coordinates[0], startPoint)
    + localMetricDistance(coordinates.at(-1), endPoint);
  const selected = cost(reversed) < cost(forward) ? reversed : forward;
  return dedupeCoordinates(selected);
}

function ringEdgeSet(startIndex, endIndex, size) {
  const output = [];
  let cursor = startIndex;
  for (let guard = 0; guard < size; guard += 1) {
    output.push(cursor);
    if (cursor === endIndex) return output;
    cursor = (cursor + 1) % size;
  }
  throw new Error('해안선 교체 구간이 ring을 한 바퀴 이상 순환합니다.');
}

function applyRingReplacements(ring, replacements) {
  const source = openRing(ring);
  if (source.length < 3) throw new Error('해안선 연결에 필요한 ring을 찾을 수 없습니다.');
  const prepared = replacements.map(replacement => {
    const startIndex = Math.max(0, Math.min(source.length - 1, Number(replacement.ref?.startIndex) || 0));
    const endIndex = Math.max(0, Math.min(source.length - 1, Number(replacement.ref?.endIndex) || 0));
    return {
      ...replacement,
      startIndex,
      endIndex,
      edges: ringEdgeSet(startIndex, endIndex, source.length),
    };
  });
  const occupied = new Set();
  for (const replacement of prepared) {
    for (const edge of replacement.edges) {
      if (occupied.has(edge)) throw new Error('서로 겹치는 해안선 교체 구간이 있습니다.');
      occupied.add(edge);
    }
  }

  const wrap = prepared.find(replacement => replacement.startIndex > replacement.endIndex
    || replacement.endIndex === source.length - 1);
  const origin = wrap?.startIndex || 0;
  const rotated = [...source.slice(origin), ...source.slice(0, origin)];
  const normalized = prepared.map(replacement => {
    const start = (replacement.startIndex - origin + source.length) % source.length;
    const edgeCount = replacement.edges.length;
    const end = start + edgeCount - 1;
    if (end >= source.length) throw new Error('해안선 교체 구간을 하나의 ring에 정규화할 수 없습니다.');
    return { ...replacement, start, end };
  }).sort((left, right) => right.start - left.start);

  let next = rotated;
  for (const replacement of normalized) {
    const startPoint = rotated[replacement.start];
    const endPoint = rotated[(replacement.end + 1) % rotated.length];
    const coordinates = orientedReplacement(replacement.sourceChain, startPoint, endPoint);
    next = [
      ...next.slice(0, replacement.start),
      ...coordinates,
      ...next.slice(replacement.end + 2),
    ];
  }
  next = dedupeCoordinates(next);
  if (next.length < 3) throw new Error('해안선 교체 후 ring에 유효한 점이 부족합니다.');
  next.push([...next[0]]);
  return next;
}

function geometryWithReplacements(geometry, replacements) {
  const polygons = polygonsFor(geometry).map(polygon => polygon.map(ring => ring.map(point => [...point])));
  const grouped = new Map();
  for (const replacement of replacements) {
    const ref = replacement.ref || {};
    const key = `${Number(ref.polygonIndex) || 0}:${Number(ref.ringIndex) || 0}`;
    if (!grouped.has(key)) grouped.set(key, []);
    grouped.get(key).push(replacement);
  }
  for (const [key, rows] of grouped) {
    const [polygonIndex, ringIndex] = key.split(':').map(Number);
    const ring = polygons[polygonIndex]?.[ringIndex];
    if (!ring) throw new Error('해안선 연결에 필요한 ring을 찾을 수 없습니다.');
    polygons[polygonIndex][ringIndex] = applyRingReplacements(ring, rows);
  }
  return polygons.length === 1 ? { type: 'Polygon', coordinates: polygons[0] } : { type: 'MultiPolygon', coordinates: polygons };
}

export function planCoastReconciliations({ conflicts = [], direction } = {}) {
  if (!Array.isArray(conflicts) || !conflicts.length || !['country-to-admin', 'admin-to-country'].includes(direction)) {
    throw new Error('해안선 정합 방향이 올바르지 않습니다.');
  }
  const source = conflicts[0];
  const countryGeometry = direction === 'admin-to-country'
    ? geometryWithReplacements(source.countryGeometry, conflicts.map(conflict => ({ ref: conflict.countryRingRef, sourceChain: conflict.adminChain })))
    : clone(source.countryGeometry);
  const adminGeometry = direction === 'country-to-admin'
    ? geometryWithReplacements(source.adminGeometry, conflicts.map(conflict => ({ ref: conflict.adminRingRef, sourceChain: conflict.countryChain })))
    : clone(source.adminGeometry);
  const bounds = conflicts.flatMap(conflict => Array.isArray(conflict.bounds) ? [conflict.bounds] : []);
  return {
    countryGeometry,
    adminGeometry,
    affectedBounds: bounds.length ? [
      Math.min(...bounds.map(value => value[0])),
      Math.min(...bounds.map(value => value[1])),
      Math.max(...bounds.map(value => value[2])),
      Math.max(...bounds.map(value => value[3])),
    ] : null,
    changedCountry: direction === 'admin-to-country',
    changedAdmin: direction === 'country-to-admin',
  };
}

export function planCoastReconciliation({ conflict, direction } = {}) {
  if (!conflict) throw new Error('해안선 정합 대상이 없습니다.');
  return planCoastReconciliations({ conflicts: [conflict], direction });
}

function planarPoint(point, latitude) {
  return [
    Number(point[0]) * Math.cos(latitude * Math.PI / 180),
    Number(point[1]),
  ];
}

function unwrappedRing(ring) {
  const points = openRing(ring);
  const output = [];
  for (const point of points) {
    let longitude = Number(point[0]);
    if (output.length) {
      const previous = output.at(-1)[0];
      while (longitude - previous > 180) longitude -= 360;
      while (longitude - previous < -180) longitude += 360;
    }
    output.push([longitude, Number(point[1])]);
  }
  return output;
}

function orientation(a, b, c) {
  return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}

function segmentsIntersect(a, b, c, d, epsilon = 1e-12) {
  const abC = orientation(a, b, c);
  const abD = orientation(a, b, d);
  const cdA = orientation(c, d, a);
  const cdB = orientation(c, d, b);
  if (((abC > epsilon && abD < -epsilon) || (abC < -epsilon && abD > epsilon))
      && ((cdA > epsilon && cdB < -epsilon) || (cdA < -epsilon && cdB > epsilon))) return true;
  const onSegment = (point, left, right) => point[0] >= Math.min(left[0], right[0]) - epsilon
    && point[0] <= Math.max(left[0], right[0]) + epsilon
    && point[1] >= Math.min(left[1], right[1]) - epsilon
    && point[1] <= Math.max(left[1], right[1]) + epsilon;
  return (Math.abs(abC) <= epsilon && onSegment(c, a, b))
    || (Math.abs(abD) <= epsilon && onSegment(d, a, b))
    || (Math.abs(cdA) <= epsilon && onSegment(a, c, d))
    || (Math.abs(cdB) <= epsilon && onSegment(b, c, d));
}

function ringArea(ring) {
  const points = unwrappedRing(ring);
  if (points.length < 3) return 0;
  const latitude = points.reduce((sum, point) => sum + Number(point[1]), 0) / points.length;
  const projected = points.map(point => planarPoint(point, latitude));
  return projected.reduce((sum, point, index) => {
    const next = projected[(index + 1) % projected.length];
    return sum + point[0] * next[1] - next[0] * point[1];
  }, 0) / 2;
}

function pointInRing(point, ring) {
  const points = unwrappedRing(ring);
  const wanted = [Number(point[0]), Number(point[1])];
  if (points.length) {
    while (wanted[0] - points[0][0] > 180) wanted[0] -= 360;
    while (wanted[0] - points[0][0] < -180) wanted[0] += 360;
  }
  let inside = false;
  for (let left = 0, right = points.length - 1; left < points.length; right = left, left += 1) {
    const a = points[left];
    const b = points[right];
    const intersects = ((a[1] > wanted[1]) !== (b[1] > wanted[1]))
      && wanted[0] < (b[0] - a[0]) * (wanted[1] - a[1]) / ((b[1] - a[1]) || 1e-15) + a[0];
    if (intersects) inside = !inside;
  }
  return inside;
}

function ringSelfIntersects(ring) {
  const points = unwrappedRing(ring);
  const latitude = points.reduce((sum, point) => sum + Number(point[1]), 0) / Math.max(1, points.length);
  const projected = points.map(point => planarPoint(point, latitude));
  for (let left = 0; left < projected.length; left += 1) {
    const leftNext = (left + 1) % projected.length;
    for (let right = left + 1; right < projected.length; right += 1) {
      const rightNext = (right + 1) % projected.length;
      if (left === right || leftNext === right || rightNext === left) continue;
      if (left === 0 && rightNext === 0) continue;
      if (segmentsIntersect(projected[left], projected[leftNext], projected[right], projected[rightNext])) return true;
    }
  }
  return false;
}

export function validateCoastReplacement(geometry, { clipper = null } = {}) {
  const issues = [];
  const polygons = polygonsFor(geometry);
  if (!polygons.length) issues.push('Polygon 또는 MultiPolygon geometry가 아닙니다.');
  polygons.forEach((polygon, polygonIndex) => {
    polygon.forEach((ring, ringIndex) => {
      const prefix = `${polygonIndex + 1}번째 polygon의 ${ringIndex + 1}번째 ring`;
      if (!Array.isArray(ring) || ring.some(point => !Array.isArray(point)
          || !Number.isFinite(Number(point[0])) || !Number.isFinite(Number(point[1]))
          || Number(point[0]) < -180 || Number(point[0]) > 180 || Number(point[1]) < -90 || Number(point[1]) > 90)) {
        issues.push(`${prefix}에 유효하지 않은 좌표가 있습니다.`);
        return;
      }
      const open = openRing(ring);
      if (open.length < 3) issues.push(`${prefix}에 유효한 점이 3개 미만입니다.`);
      if (ring.length < 4 || !sameCoordinate(ring[0], ring.at(-1))) issues.push(`${prefix}이 닫혀 있지 않습니다.`);
      if (open.some((point, index) => sameCoordinate(point, open[(index + 1) % open.length]))) issues.push(`${prefix}에 연속 중복점이 있습니다.`);
      else if (open.some((point, index) => localMetricDistance(point, open[(index + 1) % open.length]) < 0.01)) issues.push(`${prefix}에 지나치게 짧은 구간이 있습니다.`);
      if (Math.abs(ringArea(ring)) <= 1e-14) issues.push(`${prefix}의 면적이 0입니다.`);
      if (ringSelfIntersects(ring)) issues.push(`${prefix}이 자기 자신과 교차합니다.`);
      if (ringIndex > 0 && open.length && !pointInRing(open[0], polygon[0])) issues.push(`${prefix}이 exterior ring 밖에 있습니다.`);
    });
  });
  if (!issues.length && clipper && polygons.length > 1) {
    for (let left = 0; left < polygons.length; left += 1) {
      for (let right = left + 1; right < polygons.length; right += 1) {
        try {
          const overlap = clipper.intersection([polygons[left]], [polygons[right]]);
          if (Array.isArray(overlap) && overlap.length) issues.push(`${left + 1}번째와 ${right + 1}번째 polygon component가 겹칩니다.`);
        } catch {
          issues.push('MultiPolygon component 교차 검사를 완료하지 못했습니다.');
        }
      }
    }
  }
  return { ok: issues.length === 0, issues };
}
