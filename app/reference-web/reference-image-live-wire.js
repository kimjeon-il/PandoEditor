const DEFAULT_MAX_DIMENSION = 1024;
const DEFAULT_SNAP_RADIUS = 8;
const DEFAULT_SEARCH_RADIUS = 256;
const DEFAULT_SIMPLIFY_TOLERANCE = 1.5;
const MINIMUM_PEAK_EDGE = 0.02;
const MINIMUM_MEAN_EDGE = 0.035;
const SQRT2 = Math.SQRT2;
const SOBEL_MAX = 4 * SQRT2 * 255;

const liveWireFieldCache = new WeakMap();

const clamp = (value, min, max) => Math.max(min, Math.min(max, value));
const finitePoint = point => Array.isArray(point) && point.length >= 2
  && Number.isFinite(Number(point[0])) && Number.isFinite(Number(point[1]));
const clonePoint = point => [Number(point[0]), Number(point[1])];
const squaredDistance = (left, right) => {
  const dx = Number(left[0]) - Number(right[0]);
  const dy = Number(left[1]) - Number(right[1]);
  return dx * dx + dy * dy;
};

function pointSegmentDistanceSquared(point, start, end) {
  const dx = end[0] - start[0];
  const dy = end[1] - start[1];
  const length2 = dx * dx + dy * dy;
  if (length2 <= 1e-12) return squaredDistance(point, start);
  const t = clamp(((point[0] - start[0]) * dx + (point[1] - start[1]) * dy) / length2, 0, 1);
  const px = start[0] + t * dx;
  const py = start[1] + t * dy;
  return squaredDistance(point, [px, py]);
}

function grayscaleAt(data, offset) {
  const alpha = Number(data[offset + 3] ?? 255) / 255;
  const value = 0.2126 * Number(data[offset] || 0)
    + 0.7152 * Number(data[offset + 1] || 0)
    + 0.0722 * Number(data[offset + 2] || 0);
  return value * alpha + 255 * (1 - alpha);
}

export function buildReferenceImageLiveWireField(imageData, {
  sourceWidth = imageData?.width,
  sourceHeight = imageData?.height,
} = {}) {
  const width = Math.max(1, Math.floor(Number(imageData?.width) || 0));
  const height = Math.max(1, Math.floor(Number(imageData?.height) || 0));
  const data = imageData?.data;
  if (!data || data.length < width * height * 4) throw new TypeError('RGBA image data is required.');
  const naturalWidth = Math.max(1, Number(sourceWidth) || width);
  const naturalHeight = Math.max(1, Number(sourceHeight) || height);
  const gray = new Float32Array(width * height);
  for (let index = 0; index < width * height; index += 1) gray[index] = grayscaleAt(data, index * 4);

  const gradient = new Float32Array(width * height);
  const gradientX = new Float32Array(width * height);
  const gradientY = new Float32Array(width * height);
  let peak = 0;
  let sum = 0;
  if (width >= 3 && height >= 3) {
    for (let y = 1; y < height - 1; y += 1) {
      for (let x = 1; x < width - 1; x += 1) {
        const top = (y - 1) * width;
        const mid = y * width;
        const bottom = (y + 1) * width;
        const gx = -gray[top + x - 1] + gray[top + x + 1]
          - 2 * gray[mid + x - 1] + 2 * gray[mid + x + 1]
          - gray[bottom + x - 1] + gray[bottom + x + 1];
        const gy = -gray[top + x - 1] - 2 * gray[top + x] - gray[top + x + 1]
          + gray[bottom + x - 1] + 2 * gray[bottom + x] + gray[bottom + x + 1];
        const index = mid + x;
        const normalizedX = clamp(gx / SOBEL_MAX, -1, 1);
        const normalizedY = clamp(gy / SOBEL_MAX, -1, 1);
        const magnitude = clamp(Math.hypot(gx, gy) / SOBEL_MAX, 0, 1);
        gradientX[index] = normalizedX;
        gradientY[index] = normalizedY;
        gradient[index] = magnitude;
        peak = Math.max(peak, magnitude);
        sum += magnitude;
      }
    }
  }
  const interiorCount = Math.max(1, Math.max(0, width - 2) * Math.max(0, height - 2));
  return Object.freeze({
    width,
    height,
    sourceWidth: naturalWidth,
    sourceHeight: naturalHeight,
    scaleX: (width - 1) / Math.max(1, naturalWidth - 1),
    scaleY: (height - 1) / Math.max(1, naturalHeight - 1),
    gradient,
    gradientX,
    gradientY,
    peakEdgeStrength: peak,
    meanEdgeStrength: sum / interiorCount,
  });
}

function createAnalysisCanvas(width, height) {
  if (typeof OffscreenCanvas === 'function') return new OffscreenCanvas(width, height);
  if (globalThis.document?.createElement) {
    const canvas = document.createElement('canvas');
    canvas.width = width;
    canvas.height = height;
    return canvas;
  }
  throw new Error('Canvas is unavailable for reference image analysis.');
}

export function getReferenceImageLiveWireField(image, { maxDimension = DEFAULT_MAX_DIMENSION } = {}) {
  if (!image || typeof image !== 'object') throw new TypeError('A loaded reference image is required.');
  const sourceWidth = Math.max(1, Number(image.naturalWidth || image.width) || 0);
  const sourceHeight = Math.max(1, Number(image.naturalHeight || image.height) || 0);
  const limit = Math.max(64, Math.floor(Number(maxDimension) || DEFAULT_MAX_DIMENSION));
  const cacheKey = `${sourceWidth}x${sourceHeight}:${limit}`;
  let cache = liveWireFieldCache.get(image);
  if (!cache) {
    cache = new Map();
    liveWireFieldCache.set(image, cache);
  }
  if (cache.has(cacheKey)) return cache.get(cacheKey);

  const ratio = Math.min(1, limit / Math.max(sourceWidth, sourceHeight));
  const width = Math.max(2, Math.round(sourceWidth * ratio));
  const height = Math.max(2, Math.round(sourceHeight * ratio));
  const canvas = createAnalysisCanvas(width, height);
  canvas.width = width;
  canvas.height = height;
  const context = canvas.getContext('2d', { willReadFrequently: true });
  if (!context) throw new Error('2D canvas is unavailable for reference image analysis.');
  context.clearRect(0, 0, width, height);
  context.drawImage(image, 0, 0, width, height);
  const field = buildReferenceImageLiveWireField(context.getImageData(0, 0, width, height), {
    sourceWidth,
    sourceHeight,
  });
  cache.set(cacheKey, field);
  return field;
}

export function clearReferenceImageLiveWireCache(image = null) {
  if (image && typeof image === 'object') return liveWireFieldCache.delete(image);
  return false;
}

function fieldGradient(field, x, y) {
  if (x < 0 || y < 0 || x >= field.width || y >= field.height) return 0;
  return Number(field.gradient[y * field.width + x] || 0);
}

export function snapLiveWireAnchor(field, point, { radius = DEFAULT_SNAP_RADIUS } = {}) {
  if (!field?.gradient || !finitePoint(point)) return null;
  const centerX = clamp(Math.round(point[0]), 0, field.width - 1);
  const centerY = clamp(Math.round(point[1]), 0, field.height - 1);
  const searchRadius = Math.max(0, Number(radius) || 0);
  const scan = Math.ceil(searchRadius);
  const radius2 = searchRadius * searchRadius;
  let best = [centerX, centerY];
  let bestScore = Number.NEGATIVE_INFINITY;
  let bestDistance2 = Number.POSITIVE_INFINITY;
  for (let y = Math.max(0, centerY - scan); y <= Math.min(field.height - 1, centerY + scan); y += 1) {
    for (let x = Math.max(0, centerX - scan); x <= Math.min(field.width - 1, centerX + scan); x += 1) {
      const distance2 = squaredDistance([x, y], point);
      if (distance2 > radius2 + 1e-9) continue;
      const edge = fieldGradient(field, x, y);
      const score = edge - 0.0045 * Math.sqrt(distance2);
      if (score < bestScore - 1e-9) continue;
      if (Math.abs(score - bestScore) <= 1e-9 && distance2 >= bestDistance2) continue;
      best = [x, y];
      bestScore = score;
      bestDistance2 = distance2;
    }
  }
  return best;
}

class MinHeap {
  constructor() { this.items = []; }
  push(value) {
    const items = this.items;
    items.push(value);
    let index = items.length - 1;
    while (index > 0) {
      const parent = Math.floor((index - 1) / 2);
      if (items[parent].priority <= value.priority) break;
      items[index] = items[parent];
      index = parent;
    }
    items[index] = value;
  }
  pop() {
    const items = this.items;
    if (!items.length) return null;
    const root = items[0];
    const tail = items.pop();
    if (!items.length) return root;
    let index = 0;
    while (true) {
      const left = index * 2 + 1;
      const right = left + 1;
      if (left >= items.length) break;
      let child = left;
      if (right < items.length && items[right].priority < items[left].priority) child = right;
      if (items[child].priority >= tail.priority) break;
      items[index] = items[child];
      index = child;
    }
    items[index] = tail;
    return root;
  }
  get size() { return this.items.length; }
}

const NEIGHBORS = Object.freeze([
  Object.freeze({ dx: -1, dy: -1, step: SQRT2, ux: -1 / SQRT2, uy: -1 / SQRT2 }),
  Object.freeze({ dx: 0, dy: -1, step: 1, ux: 0, uy: -1 }),
  Object.freeze({ dx: 1, dy: -1, step: SQRT2, ux: 1 / SQRT2, uy: -1 / SQRT2 }),
  Object.freeze({ dx: -1, dy: 0, step: 1, ux: -1, uy: 0 }),
  Object.freeze({ dx: 1, dy: 0, step: 1, ux: 1, uy: 0 }),
  Object.freeze({ dx: -1, dy: 1, step: SQRT2, ux: -1 / SQRT2, uy: 1 / SQRT2 }),
  Object.freeze({ dx: 0, dy: 1, step: 1, ux: 0, uy: 1 }),
  Object.freeze({ dx: 1, dy: 1, step: SQRT2, ux: 1 / SQRT2, uy: 1 / SQRT2 }),
]);

function directionCost(field, x, y, move) {
  const index = y * field.width + x;
  const gx = Number(field.gradientX?.[index] || 0);
  const gy = Number(field.gradientY?.[index] || 0);
  const length = Math.hypot(gx, gy);
  if (length < 1e-5) return 1;
  const tangentX = -gy / length;
  const tangentY = gx / length;
  return 1 - Math.abs(move.ux * tangentX + move.uy * tangentY);
}

function turnCost(previousDirection, nextDirection) {
  if (previousDirection < 0 || previousDirection >= NEIGHBORS.length) return 0;
  const previous = NEIGHBORS[previousDirection];
  const next = NEIGHBORS[nextDirection];
  const dot = clamp(previous.ux * next.ux + previous.uy * next.uy, -1, 1);
  return (1 - dot) * 0.5;
}

function invalidTree(reason) {
  return Object.freeze({ ok: false, reason, parents: null });
}

export function buildLiveWireTree(field, anchor, {
  target = null,
  searchRadius = DEFAULT_SEARCH_RADIUS,
  snapRadius = DEFAULT_SNAP_RADIUS,
  gradientWeight = 0.67,
  directionWeight = 0.23,
  turnWeight = 0.10,
  corridorWeight = 0.08,
  minimumPeakEdge = MINIMUM_PEAK_EDGE,
  maxExpansions = 650000,
} = {}) {
  if (!field?.gradient || !field?.gradientX || !field?.gradientY || !finitePoint(anchor)) {
    return invalidTree('invalid-gradient-field');
  }
  if (Number(field.peakEdgeStrength || 0) < Number(minimumPeakEdge)) {
    return invalidTree('insufficient-edge-strength');
  }
  const start = snapLiveWireAnchor(field, anchor, { radius: snapRadius });
  const goal = finitePoint(target) ? snapLiveWireAnchor(field, target, { radius: snapRadius }) : null;
  if (!start) return invalidTree('invalid-anchor');
  const radius = Math.max(24, Math.floor(Number(searchRadius) || DEFAULT_SEARCH_RADIUS));
  const endpoints = goal ? [start, goal] : [start, start];
  const minX = clamp(Math.floor(Math.min(endpoints[0][0], endpoints[1][0]) - radius), 0, field.width - 1);
  const maxX = clamp(Math.ceil(Math.max(endpoints[0][0], endpoints[1][0]) + radius), 0, field.width - 1);
  const minY = clamp(Math.floor(Math.min(endpoints[0][1], endpoints[1][1]) - radius), 0, field.height - 1);
  const maxY = clamp(Math.ceil(Math.max(endpoints[0][1], endpoints[1][1]) + radius), 0, field.height - 1);
  const boxWidth = maxX - minX + 1;
  const boxHeight = maxY - minY + 1;
  const cellCount = boxWidth * boxHeight;
  const distances = new Float64Array(cellCount);
  distances.fill(Number.POSITIVE_INFINITY);
  const parents = new Int32Array(cellCount);
  parents.fill(-1);
  const incomingDirections = new Int8Array(cellCount);
  incomingDirections.fill(-1);
  const closed = new Uint8Array(cellCount);
  const toIndex = (x, y) => (y - minY) * boxWidth + (x - minX);
  const fromIndex = index => [minX + index % boxWidth, minY + Math.floor(index / boxWidth)];
  const startIndex = toIndex(start[0], start[1]);
  const goalIndex = goal ? toIndex(goal[0], goal[1]) : -1;
  const heap = new MinHeap();
  distances[startIndex] = 0;
  heap.push({ index: startIndex, priority: 0 });
  let expansions = 0;
  const expansionLimit = Math.min(cellCount, Math.max(2000, Number(maxExpansions) || 650000));
  const corridorScale = Math.max(16, radius);

  while (heap.size && expansions < expansionLimit) {
    const current = heap.pop();
    if (!current || closed[current.index]) continue;
    closed[current.index] = 1;
    expansions += 1;
    if (current.index === goalIndex) break;
    const [x, y] = fromIndex(current.index);
    const previousDirection = incomingDirections[current.index];
    for (let directionIndex = 0; directionIndex < NEIGHBORS.length; directionIndex += 1) {
      const move = NEIGHBORS[directionIndex];
      const nx = x + move.dx;
      const ny = y + move.dy;
      if (nx < minX || nx > maxX || ny < minY || ny > maxY) continue;
      const nextIndex = toIndex(nx, ny);
      if (closed[nextIndex]) continue;
      const edge = fieldGradient(field, nx, ny);
      const edgeCost = Math.pow(1 - edge, 2);
      const tangentCost = directionCost(field, nx, ny, move);
      const bendCost = turnCost(previousDirection, directionIndex);
      const corridorDistance = goal
        ? Math.sqrt(pointSegmentDistanceSquared([nx, ny], start, goal)) / corridorScale
        : 0;
      const localCost = 0.025
        + Number(gradientWeight) * edgeCost
        + Number(directionWeight) * tangentCost
        + Number(turnWeight) * bendCost
        + Number(corridorWeight) * corridorDistance * corridorDistance;
      const candidate = distances[current.index] + move.step * localCost;
      if (candidate >= distances[nextIndex]) continue;
      distances[nextIndex] = candidate;
      parents[nextIndex] = current.index;
      incomingDirections[nextIndex] = directionIndex;
      heap.push({ index: nextIndex, priority: candidate });
    }
  }

  return Object.freeze({
    ok: true,
    reason: '',
    field,
    anchor: Object.freeze(clonePoint(start)),
    target: goal ? Object.freeze(clonePoint(goal)) : null,
    minX,
    minY,
    maxX,
    maxY,
    boxWidth,
    boxHeight,
    startIndex,
    parents,
    distances,
    expansions,
  });
}

export function traceLiveWirePath(tree, target, {
  snapRadius = DEFAULT_SNAP_RADIUS,
  minimumMeanEdge = MINIMUM_MEAN_EDGE,
} = {}) {
  if (!tree?.ok || !tree.field || !tree.parents || !finitePoint(target)) {
    return Object.freeze({ ok: false, reason: tree?.reason || 'invalid-tree', points: [] });
  }
  const field = tree.field;
  const snapped = snapLiveWireAnchor(field, target, { radius: snapRadius });
  if (!snapped) return Object.freeze({ ok: false, reason: 'invalid-target', points: [] });
  const [x, y] = snapped;
  if (x < tree.minX || x > tree.maxX || y < tree.minY || y > tree.maxY) {
    return Object.freeze({ ok: false, reason: 'target-outside-tree', points: [] });
  }
  const targetIndex = (y - tree.minY) * tree.boxWidth + (x - tree.minX);
  if (!Number.isFinite(tree.distances[targetIndex])) {
    return Object.freeze({ ok: false, reason: 'path-not-found', points: [] });
  }
  const points = [];
  let cursor = targetIndex;
  let guard = tree.parents.length + 1;
  while (cursor >= 0 && guard-- > 0) {
    points.push([tree.minX + cursor % tree.boxWidth, tree.minY + Math.floor(cursor / tree.boxWidth)]);
    if (cursor === tree.startIndex) break;
    cursor = tree.parents[cursor];
  }
  if (!points.length || cursor !== tree.startIndex) {
    return Object.freeze({ ok: false, reason: 'path-not-found', points: [] });
  }
  points.reverse();
  const meanEdgeStrength = points.reduce((sum, point) => sum + fieldGradient(field, point[0], point[1]), 0) / points.length;
  if (meanEdgeStrength < Number(minimumMeanEdge)) {
    return Object.freeze({ ok: false, reason: 'insufficient-edge-strength', points: [], meanEdgeStrength });
  }
  return Object.freeze({
    ok: true,
    reason: '',
    points: Object.freeze(points.map(point => Object.freeze(point))),
    meanEdgeStrength,
  });
}

function rdp(points, tolerance) {
  if (points.length <= 2 || !(tolerance > 0)) return points.map(clonePoint);
  const tolerance2 = tolerance * tolerance;
  const keep = new Uint8Array(points.length);
  keep[0] = 1;
  keep[points.length - 1] = 1;
  const stack = [[0, points.length - 1]];
  while (stack.length) {
    const [startIndex, endIndex] = stack.pop();
    let farthestIndex = -1;
    let farthestDistance2 = tolerance2;
    for (let index = startIndex + 1; index < endIndex; index += 1) {
      const distance2 = pointSegmentDistanceSquared(points[index], points[startIndex], points[endIndex]);
      if (distance2 <= farthestDistance2) continue;
      farthestDistance2 = distance2;
      farthestIndex = index;
    }
    if (farthestIndex < 0) continue;
    keep[farthestIndex] = 1;
    stack.push([startIndex, farthestIndex], [farthestIndex, endIndex]);
  }
  return points.filter((_, index) => keep[index]).map(clonePoint);
}

export function simplifyLiveWireSegments(segments, { tolerance = DEFAULT_SIMPLIFY_TOLERANCE } = {}) {
  const result = [];
  for (const segment of segments || []) {
    const source = (segment || []).filter(finitePoint).map(clonePoint);
    if (source.length < 2) continue;
    const simplified = rdp(source, Math.max(0, Number(tolerance) || 0));
    if (result.length && squaredDistance(result.at(-1), simplified[0]) <= 1e-12) result.push(...simplified.slice(1));
    else result.push(...simplified);
  }
  return result;
}

export function analysisPointFromUv(field, uv) {
  if (!field || !finitePoint(uv)) return null;
  return [
    clamp(Number(uv[0]), 0, 1) * Math.max(1, field.width - 1),
    clamp(Number(uv[1]), 0, 1) * Math.max(1, field.height - 1),
  ];
}

export function sourcePixelsFromAnalysis(field, points) {
  if (!field) return [];
  const scaleX = Number(field.scaleX) || ((field.width - 1) / Math.max(1, field.sourceWidth - 1));
  const scaleY = Number(field.scaleY) || ((field.height - 1) / Math.max(1, field.sourceHeight - 1));
  return (points || []).filter(finitePoint).map(point => [
    scaleX > 0 ? Number(point[0]) / scaleX : 0,
    scaleY > 0 ? Number(point[1]) / scaleY : 0,
  ]);
}

export const REFERENCE_IMAGE_LIVE_WIRE_DEFAULTS = Object.freeze({
  maxDimension: DEFAULT_MAX_DIMENSION,
  snapRadius: DEFAULT_SNAP_RADIUS,
  searchRadius: DEFAULT_SEARCH_RADIUS,
  simplifyTolerance: DEFAULT_SIMPLIFY_TOLERANCE,
});
