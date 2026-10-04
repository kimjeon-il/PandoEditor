const SNAP_THRESHOLDS = Object.freeze({
  mouse: 10,
  touch: 18,
});

const TYPES = new Set(['vertex', 'edge', 'boundary', 'intersection', 'neighbor']);
const TIE_PRIORITY = Object.freeze({ vertex: 0, intersection: 1, boundary: 2, edge: 3, neighbor: 4 });

export function snapThreshold(pointerType = 'mouse') {
  return SNAP_THRESHOLDS[pointerType === 'touch' ? 'touch' : 'mouse'];
}

function nearestOnProjectedSegment(point, a, b) {
  const dx = b[0] - a[0];
  const dy = b[1] - a[1];
  const length2 = dx * dx + dy * dy;
  const t = length2 ? Math.max(0, Math.min(1, ((point[0] - a[0]) * dx + (point[1] - a[1]) * dy) / length2)) : 0;
  const projected = [a[0] + dx * t, a[1] + dy * t];
  return { projected, t, distancePx: Math.hypot(point[0] - projected[0], point[1] - projected[1]) };
}

function candidateResult(candidate, screenPoint, project) {
  if (!TYPES.has(candidate?.kind)) return null;
  if (candidate.kind === 'vertex' || candidate.kind === 'intersection') {
    const projected = project(candidate.coordinate);
    if (!projected) return null;
    return { ...candidate, coordinate: [...candidate.coordinate], distancePx: Math.hypot(screenPoint[0] - projected[0], screenPoint[1] - projected[1]) };
  }
  const a = project(candidate.a);
  const b = project(candidate.b);
  if (!a || !b) return null;
  const nearest = nearestOnProjectedSegment(screenPoint, a, b);
  return {
    ...candidate,
    coordinate: [
      Number(candidate.a[0]) + (Number(candidate.b[0]) - Number(candidate.a[0])) * nearest.t,
      Number(candidate.a[1]) + (Number(candidate.b[1]) - Number(candidate.a[1])) * nearest.t,
    ],
    segmentEndpoints: [[...candidate.a], [...candidate.b]],
    distancePx: nearest.distancePx,
    segmentT: nearest.t,
  };
}

export function resolveSnap({ coordinate, screenPoint, candidates = [], project, pointerType = 'mouse' }) {
  if (!coordinate || !screenPoint || typeof project !== 'function') return null;
  const threshold = snapThreshold(pointerType);
  const results = candidates
    .map(candidate => candidateResult(candidate, screenPoint, project))
    .filter(result => result && result.distancePx <= threshold)
    .sort((left, right) => left.distancePx - right.distancePx
      || (TIE_PRIORITY[left.kind] ?? 99) - (TIE_PRIORITY[right.kind] ?? 99));
  return results[0] || null;
}

export function snapIndicator(result) {
  if (!result) return null;
  return {
    kind: result.kind,
    coordinate: [...result.coordinate],
    segmentEndpoints: result.segmentEndpoints ? result.segmentEndpoints.map(point => [...point]) : null,
    ownerIds: [...(result.ownerIds || [])].map(String),
    nodeKey: result.nodeKey || null,
    segmentKey: result.segmentKey || null,
  };
}
