export const LABEL_PRIORITIES = Object.freeze({ country: 100, capital: 90, majorCity: 70, administrative: 60, place: 40 });

const AUTOMATIC_LABEL_POLICIES = Object.freeze({
  country: Object.freeze({ priority: LABEL_PRIORITIES.country, minZoom: 0, maxZoom: Infinity, collisionGroup: 'country' }),
  capital: Object.freeze({ priority: LABEL_PRIORITIES.capital, minZoom: 0, maxZoom: Infinity, collisionGroup: 'place' }),
  city: Object.freeze({ priority: LABEL_PRIORITIES.majorCity, minZoom: 1.25, maxZoom: Infinity, collisionGroup: 'place' }),
  region: Object.freeze({ priority: LABEL_PRIORITIES.administrative, minZoom: 1, maxZoom: Infinity, collisionGroup: 'place' }),
  town: Object.freeze({ priority: LABEL_PRIORITIES.place, minZoom: 2.5, maxZoom: Infinity, collisionGroup: 'place' }),
  mountain: Object.freeze({ priority: LABEL_PRIORITIES.place, minZoom: 2, maxZoom: Infinity, collisionGroup: 'place' }),
  water: Object.freeze({ priority: LABEL_PRIORITIES.place, minZoom: 1.5, maxZoom: Infinity, collisionGroup: 'place' }),
  custom: Object.freeze({ priority: LABEL_PRIORITIES.place, minZoom: 1.5, maxZoom: Infinity, collisionGroup: 'place' }),
});

function normalizedBox(candidate) {
  const width = Math.max(1, Number(candidate.width || 1));
  const height = Math.max(1, Number(candidate.height || 1));
  return {
    left: candidate.point[0] - width / 2,
    right: candidate.point[0] + width / 2,
    top: candidate.point[1] - height / 2,
    bottom: candidate.point[1] + height / 2,
  };
}

function collides(left, right, padding) {
  return !(left.right + padding < right.left || left.left - padding > right.right
    || left.bottom + padding < right.top || left.top - padding > right.bottom);
}

export function normalizeLabelSettings(raw = {}) {
  const manualPosition = Array.isArray(raw.manualPosition) && raw.manualPosition.length >= 2
    ? [Number(raw.manualPosition[0]), Number(raw.manualPosition[1])]
    : null;
  return {
    priority: raw.priority != null && raw.priority !== '' && Number.isFinite(Number(raw.priority)) ? Number(raw.priority) : null,
    minZoom: raw.minZoom != null && raw.minZoom !== '' && Number.isFinite(Number(raw.minZoom)) ? Number(raw.minZoom) : 0,
    maxZoom: raw.maxZoom != null && raw.maxZoom !== '' && Number.isFinite(Number(raw.maxZoom)) ? Number(raw.maxZoom) : Infinity,
    manualPosition,
    pinned: raw.pinned === true,
    collisionGroup: String(raw.collisionGroup || 'map'),
  };
}

export function automaticLabelSettings(kind, raw = {}) {
  const normalized = normalizeLabelSettings(raw);
  const policy = AUTOMATIC_LABEL_POLICIES[String(kind || 'custom')] || AUTOMATIC_LABEL_POLICIES.custom;
  return {
    ...normalized,
    priority: policy.priority,
    minZoom: policy.minZoom,
    maxZoom: policy.maxZoom,
    collisionGroup: policy.collisionGroup,
    pinned: normalized.pinned === true || !!normalized.manualPosition,
  };
}

function boxInsideBounds(box, bounds) {
  if (!bounds) return true;
  return box.left >= Number(bounds.left ?? -Infinity)
    && box.right <= Number(bounds.right ?? Infinity)
    && box.top >= Number(bounds.top ?? -Infinity)
    && box.bottom <= Number(bounds.bottom ?? Infinity);
}

function sortedVisibleCandidates(candidates, zoom, bounds) {
  return candidates
    .filter(candidate => candidate?.point && zoom >= Number(candidate.minZoom ?? 0) && zoom <= Number(candidate.maxZoom ?? Infinity))
    .map(candidate => ({ ...candidate, box: normalizedBox(candidate), collisionGroup: String(candidate.collisionGroup || 'map') }))
    .filter(candidate => candidate.selected || candidate.pinned || boxInsideBounds(candidate.box, bounds))
    .sort((left, right) => Number(!!right.selected) - Number(!!left.selected)
      || Number(!!right.pinned) - Number(!!left.pinned)
      || Number(right.priority || 0) - Number(left.priority || 0)
      || String(left.key).localeCompare(String(right.key)));
}

function screenCellRange(box, cellSize, padding = 0) {
  return {
    minX: Math.floor((box.left - padding) / cellSize),
    maxX: Math.floor((box.right + padding) / cellSize),
    minY: Math.floor((box.top - padding) / cellSize),
    maxY: Math.floor((box.bottom + padding) / cellSize),
  };
}

export function layoutLabels(candidates = [], { zoom = 1, padding = 3, cellSize = 64, bounds = null, metrics = null } = {}) {
  const started = typeof performance !== 'undefined' ? performance.now() : Date.now();
  const visible = sortedVisibleCandidates(candidates, zoom, bounds);
  const resolvedCellSize = Math.max(16, Number(cellSize) || 64);
  const gridsByGroup = new Map();
  let collisionCheckCount = 0;
  let collisionQueryCount = 0;
  let collisionQueryCandidateCount = 0;
  let maxCandidatesPerCollisionQuery = 0;
  let gridCellCount = 0;
  const output = [];

  const groupGrid = group => {
    if (!gridsByGroup.has(group)) gridsByGroup.set(group, new Map());
    return gridsByGroup.get(group);
  };
  const nearby = candidate => {
    const grid = groupGrid(candidate.collisionGroup);
    const range = screenCellRange(candidate.box, resolvedCellSize, padding);
    const indices = new Set();
    for (let x = range.minX; x <= range.maxX; x += 1) for (let y = range.minY; y <= range.maxY; y += 1) {
      for (const index of grid.get(`${x}:${y}`) || []) indices.add(index);
    }
    const ordered = [...indices].sort((left, right) => left - right).map(index => output[index]);
    collisionQueryCount += 1;
    collisionQueryCandidateCount += ordered.length;
    maxCandidatesPerCollisionQuery = Math.max(maxCandidatesPerCollisionQuery, ordered.length);
    return ordered;
  };
  const insert = (candidate, index) => {
    const grid = groupGrid(candidate.collisionGroup);
    const range = screenCellRange(candidate.box, resolvedCellSize);
    for (let x = range.minX; x <= range.maxX; x += 1) for (let y = range.minY; y <= range.maxY; y += 1) {
      const key = `${x}:${y}`;
      if (!grid.has(key)) { grid.set(key, []); gridCellCount += 1; }
      grid.get(key).push(index);
    }
  };

  for (const candidate of visible) {
    const placed = candidate.selected || candidate.pinned ? [] : nearby(candidate);
    const first = !candidate.selected && !candidate.pinned && placed.some(item => {
      collisionCheckCount += 1;
      return !item.pinned || collides(candidate.box, item.box, padding);
    });
    const blocked = first && placed.some(item => {
      collisionCheckCount += 1;
      return collides(candidate.box, item.box, padding);
    });
    if (blocked) continue;
    const outputIndex = output.length;
    output.push(candidate);
    insert(candidate, outputIndex);
  }
  if (metrics) Object.assign(metrics, {
    candidateCount: candidates.length,
    visibleByZoomCount: visible.length,
    placedCount: output.length,
    collisionCheckCount,
    gridCellCount,
    averageCandidatesPerCollisionQuery: collisionQueryCount ? collisionQueryCandidateCount / collisionQueryCount : 0,
    maxCandidatesPerCollisionQuery,
    layoutMs: (typeof performance !== 'undefined' ? performance.now() : Date.now()) - started,
    algorithm: 'screen-grid',
    cellSize: resolvedCellSize,
  });
  return output;
}

export function labelKey(sourceType, sourceId) {
  return `${String(sourceType)}:${String(sourceId)}`;
}
