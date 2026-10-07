const DEFAULT_MAX_DIMENSION = 1024;
const DEFAULT_CORRIDOR_RADIUS = 12;
const DEFAULT_SIMPLIFY_TOLERANCE = 1.5;
const MINIMUM_PEAK_EDGE = 0.02;
const MINIMUM_MEAN_EDGE = 0.06;
const SQRT2 = Math.SQRT2;
const SOBEL_MAX = 4 * SQRT2 * 255;

const gradientCache = new WeakMap();

const clamp = (value, min, max) => Math.max(min, Math.min(max, value));
const finitePoint = value => Array.isArray(value) && value.length >= 2
  && Number.isFinite(Number(value[0])) && Number.isFinite(Number(value[1]));
const clonePoint = value => [Number(value[0]), Number(value[1])];
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
  return squaredDistance(point, [start[0] + dx * t, start[1] + dy * t]);
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

function removeDuplicatePoints(points) {
  const result = [];
  for (const point of points) {
    if (!finitePoint(point)) continue;
    const next = clonePoint(point);
    if (result.length && squaredDistance(result.at(-1), next) <= 1e-12) continue;
    result.push(next);
  }
  return result;
}

function grayscaleAt(data, offset) {
  const alpha = Number(data[offset + 3] ?? 255) / 255;
  const value = 0.2126 * Number(data[offset] || 0)
    + 0.7152 * Number(data[offset + 1] || 0)
    + 0.0722 * Number(data[offset + 2] || 0);
  return value * alpha + 255 * (1 - alpha);
}

export function buildReferenceImageGradientField(imageData, {
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
        const magnitude = clamp(Math.hypot(gx, gy) / SOBEL_MAX, 0, 1);
        gradient[mid + x] = magnitude;
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

export function getReferenceImageGradientField(image, { maxDimension = DEFAULT_MAX_DIMENSION } = {}) {
  if (!image || typeof image !== 'object') throw new TypeError('A loaded reference image is required.');
  const sourceWidth = Math.max(1, Number(image.naturalWidth || image.width) || 0);
  const sourceHeight = Math.max(1, Number(image.naturalHeight || image.height) || 0);
  if (!sourceWidth || !sourceHeight) throw new Error('Reference image dimensions are unavailable.');
  const limit = Math.max(64, Math.floor(Number(maxDimension) || DEFAULT_MAX_DIMENSION));
  const cacheKey = `${sourceWidth}x${sourceHeight}:${limit}`;
  let entries = gradientCache.get(image);
  if (!entries) {
    entries = new Map();
    gradientCache.set(image, entries);
  }
  if (entries.has(cacheKey)) return entries.get(cacheKey);

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
  const field = buildReferenceImageGradientField(context.getImageData(0, 0, width, height), {
    sourceWidth,
    sourceHeight,
  });
  entries.set(cacheKey, field);
  return field;
}

export function clearReferenceImageGradientCache(image = null) {
  if (image && typeof image === 'object') return gradientCache.delete(image);
  return false;
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

function fieldGradient(field, x, y) {
  if (x < 0 || y < 0 || x >= field.width || y >= field.height) return 0;
  return Number(field.gradient[y * field.width + x] || 0);
}

function resamplePolyline(points, spacing) {
  const source = removeDuplicatePoints(points);
  if (source.length <= 1 || !(spacing > 0)) return source;
  const result = [clonePoint(source[0])];
  let previous = clonePoint(source[0]);
  let remaining = spacing;
  for (let index = 1; index < source.length; index += 1) {
    const target = source[index];
    let dx = target[0] - previous[0];
    let dy = target[1] - previous[1];
    let length = Math.hypot(dx, dy);
    while (length >= remaining && length > 1e-9) {
      const ratio = remaining / length;
      previous = [previous[0] + dx * ratio, previous[1] + dy * ratio];
      result.push(clonePoint(previous));
      dx = target[0] - previous[0];
      dy = target[1] - previous[1];
      length = Math.hypot(dx, dy);
      remaining = spacing;
    }
    remaining -= length;
    previous = clonePoint(target);
    if (remaining <= 1e-9) remaining = spacing;
  }
  const last = source.at(-1);
  if (squaredDistance(result.at(-1), last) > 1e-8) result.push(clonePoint(last));
  return result;
}

function snapToEdge(field, point, radius) {
  const centerX = clamp(Math.round(point[0]), 0, field.width - 1);
  const centerY = clamp(Math.round(point[1]), 0, field.height - 1);
  const search = Math.max(1, Math.ceil(radius));
  const radius2 = radius * radius;
  let best = [centerX, centerY];
  let bestScore = fieldGradient(field, centerX, centerY);
  let bestDistance2 = 0;
  for (let y = Math.max(0, centerY - search); y <= Math.min(field.height - 1, centerY + search); y += 1) {
    for (let x = Math.max(0, centerX - search); x <= Math.min(field.width - 1, centerX + search); x += 1) {
      const distance2 = squaredDistance([x, y], point);
      if (distance2 > radius2) continue;
      const edge = fieldGradient(field, x, y);
      const score = edge - 0.006 * Math.sqrt(distance2);
      if (score < bestScore - 1e-9 || (Math.abs(score - bestScore) <= 1e-9 && distance2 >= bestDistance2)) continue;
      best = [x, y];
      bestScore = score;
      bestDistance2 = distance2;
    }
  }
  return best;
}

const NEIGHBORS = Object.freeze([
  [-1, -1, SQRT2], [0, -1, 1], [1, -1, SQRT2],
  [-1, 0, 1], [1, 0, 1],
  [-1, 1, SQRT2], [0, 1, 1], [1, 1, SQRT2],
]);

function traceCorridorSegment(field, roughStart, roughEnd, start, end, radius) {
  const margin = Math.ceil(radius) + 2;
  const minX = clamp(Math.floor(Math.min(roughStart[0], roughEnd[0], start[0], end[0]) - margin), 0, field.width - 1);
  const maxX = clamp(Math.ceil(Math.max(roughStart[0], roughEnd[0], start[0], end[0]) + margin), 0, field.width - 1);
  const minY = clamp(Math.floor(Math.min(roughStart[1], roughEnd[1], start[1], end[1]) - margin), 0, field.height - 1);
  const maxY = clamp(Math.ceil(Math.max(roughStart[1], roughEnd[1], start[1], end[1]) + margin), 0, field.height - 1);
  const boxWidth = maxX - minX + 1;
  const boxHeight = maxY - minY + 1;
  const cellCount = boxWidth * boxHeight;
  const distances = new Float64Array(cellCount);
  distances.fill(Number.POSITIVE_INFINITY);
  const parents = new Int32Array(cellCount);
  parents.fill(-1);
  const closed = new Uint8Array(cellCount);
  const toLocalIndex = (x, y) => (y - minY) * boxWidth + (x - minX);
  const fromLocalIndex = index => [minX + index % boxWidth, minY + Math.floor(index / boxWidth)];
  const startPoint = [clamp(Math.round(start[0]), minX, maxX), clamp(Math.round(start[1]), minY, maxY)];
  const endPoint = [clamp(Math.round(end[0]), minX, maxX), clamp(Math.round(end[1]), minY, maxY)];
  const startIndex = toLocalIndex(startPoint[0], startPoint[1]);
  const endIndex = toLocalIndex(endPoint[0], endPoint[1]);
  const radius2 = radius * radius;
  const allowed = (x, y) => {
    if (x === startPoint[0] && y === startPoint[1]) return true;
    if (x === endPoint[0] && y === endPoint[1]) return true;
    return pointSegmentDistanceSquared([x, y], roughStart, roughEnd) <= radius2 + 1e-9;
  };
  const heap = new MinHeap();
  distances[startIndex] = 0;
  heap.push({ index: startIndex, priority: 0 });
  let expansions = 0;
  const maxExpansions = Math.min(150000, Math.max(2000, cellCount * 2));
  while (heap.size && expansions < maxExpansions) {
    const current = heap.pop();
    if (!current || closed[current.index]) continue;
    closed[current.index] = 1;
    expansions += 1;
    if (current.index === endIndex) break;
    const [x, y] = fromLocalIndex(current.index);
    for (const [dx, dy, stepDistance] of NEIGHBORS) {
      const nx = x + dx;
      const ny = y + dy;
      if (nx < minX || nx > maxX || ny < minY || ny > maxY || !allowed(nx, ny)) continue;
      const nextIndex = toLocalIndex(nx, ny);
      if (closed[nextIndex]) continue;
      const edge = fieldGradient(field, nx, ny);
      const corridorDistance = Math.sqrt(pointSegmentDistanceSquared([nx, ny], roughStart, roughEnd));
      const edgeCost = 0.08 + 3.4 * (1 - edge) * (1 - edge);
      const corridorCost = 0.18 * Math.pow(corridorDistance / Math.max(1, radius), 2);
      const candidate = distances[current.index] + stepDistance * (edgeCost + corridorCost);
      if (candidate >= distances[nextIndex]) continue;
      distances[nextIndex] = candidate;
      parents[nextIndex] = current.index;
      const heuristic = Math.hypot(nx - endPoint[0], ny - endPoint[1]) * 0.08;
      heap.push({ index: nextIndex, priority: candidate + heuristic });
    }
  }
  if (!Number.isFinite(distances[endIndex])) return null;
  const path = [];
  let cursor = endIndex;
  let guard = cellCount + 1;
  while (cursor >= 0 && guard-- > 0) {
    path.push(fromLocalIndex(cursor));
    if (cursor === startIndex) break;
    cursor = parents[cursor];
  }
  if (path.at(-1)?.[0] !== startPoint[0] || path.at(-1)?.[1] !== startPoint[1]) return null;
  return path.reverse();
}

function toAnalysisPoint(field, point) {
  return [
    clamp(Number(point[0]) * field.scaleX, 0, field.width - 1),
    clamp(Number(point[1]) * field.scaleY, 0, field.height - 1),
  ];
}

function toSourcePoint(field, point) {
  return [
    field.scaleX > 0 ? Number(point[0]) / field.scaleX : 0,
    field.scaleY > 0 ? Number(point[1]) / field.scaleY : 0,
  ];
}

export function refineReferenceImageLine({
  field,
  roughPoints,
  corridorRadius = DEFAULT_CORRIDOR_RADIUS,
  simplifyTolerance = DEFAULT_SIMPLIFY_TOLERANCE,
  minimumPeakEdge = MINIMUM_PEAK_EDGE,
  minimumMeanEdge = MINIMUM_MEAN_EDGE,
} = {}) {
  if (!field?.gradient || !Number.isFinite(field.width) || !Number.isFinite(field.height)) {
    return Object.freeze({ ok: false, reason: 'invalid-gradient-field', points: [] });
  }
  const source = removeDuplicatePoints(roughPoints || []);
  if (source.length < 2) return Object.freeze({ ok: false, reason: 'too-few-points', points: [] });
  if (Number(field.peakEdgeStrength || 0) < minimumPeakEdge) {
    return Object.freeze({ ok: false, reason: 'insufficient-edge-strength', points: [] });
  }

  const analysisRough = source.map(point => toAnalysisPoint(field, point));
  const analysisScale = Math.max(1e-6, Math.min(field.scaleX || 1, field.scaleY || 1));
  const radius = Math.max(2, Number(corridorRadius) * analysisScale);
  const spacing = Math.max(4, Math.min(18, radius * 1.15));
  const anchors = resamplePolyline(analysisRough, spacing);
  if (anchors.length < 2) return Object.freeze({ ok: false, reason: 'too-few-points', points: [] });
  const snapRadius = Math.max(1.5, Math.min(radius * 0.9, 8));
  const snapped = anchors.map(point => snapToEdge(field, point, snapRadius));
  const traced = [];
  for (let index = 1; index < anchors.length; index += 1) {
    const segment = traceCorridorSegment(
      field,
      anchors[index - 1],
      anchors[index],
      snapped[index - 1],
      snapped[index],
      radius,
    );
    if (!segment?.length) return Object.freeze({ ok: false, reason: 'path-not-found', points: [] });
    if (traced.length && squaredDistance(traced.at(-1), segment[0]) <= 1e-12) traced.push(...segment.slice(1));
    else traced.push(...segment);
  }
  const uniqueTrace = removeDuplicatePoints(traced);
  if (uniqueTrace.length < 2) return Object.freeze({ ok: false, reason: 'path-not-found', points: [] });
  const meanEdgeStrength = uniqueTrace.reduce((sum, point) => sum + fieldGradient(field, Math.round(point[0]), Math.round(point[1])), 0) / uniqueTrace.length;
  if (meanEdgeStrength < minimumMeanEdge) {
    return Object.freeze({ ok: false, reason: 'insufficient-edge-strength', points: [], meanEdgeStrength });
  }
  const sourceTrace = uniqueTrace.map(point => toSourcePoint(field, point));
  const points = rdp(sourceTrace, Math.max(0, Number(simplifyTolerance) || 0));
  return Object.freeze({
    ok: true,
    reason: '',
    points: Object.freeze(points.map(point => Object.freeze(point))),
    rawPointCount: uniqueTrace.length,
    simplifiedPointCount: points.length,
    meanEdgeStrength,
    corridorRadius: Number(corridorRadius),
  });
}

export function referenceImagePixelsToCoordinates(points, { sourceWidth, sourceHeight, warp } = {}) {
  if (!warp?.ok || typeof warp.project !== 'function') throw new Error('A ready reference image warp is required.');
  const width = Math.max(2, Number(sourceWidth) || 0);
  const height = Math.max(2, Number(sourceHeight) || 0);
  const xDenominator = width - 1;
  const yDenominator = height - 1;
  return (points || []).filter(finitePoint).map(point => {
    const uv = [clamp(Number(point[0]) / xDenominator, 0, 1), clamp(Number(point[1]) / yDenominator, 0, 1)];
    const projected = warp.project(uv);
    if (!finitePoint(projected)) throw new Error('Reference image warp returned an invalid coordinate.');
    return clonePoint(projected);
  });
}

export const REFERENCE_IMAGE_LINE_REFINER_DEFAULTS = Object.freeze({
  maxDimension: DEFAULT_MAX_DIMENSION,
  corridorRadius: DEFAULT_CORRIDOR_RADIUS,
  simplifyTolerance: DEFAULT_SIMPLIFY_TOLERANCE,
});
