import {
  buildReferenceImageMesh,
  buildReferenceImageProjectiveWarpFromQuad,
} from './reference-image-georef.js';

export const REFERENCE_IMAGE_TRANSFORM = Object.freeze({
  handleRadius: 8,
  hitRadius: 12,
  rotateHandleOffset: 30,
  minimumWidth: 48,
  minimumHeight: 36,
});

const radians = value => Number(value) * Math.PI / 180;
const degrees = value => Number(value) * 180 / Math.PI;
const HANDLE_ORDER = Object.freeze(['nw', 'n', 'ne', 'e', 'se', 's', 'sw', 'w']);

function finitePair(value) {
  if (!Array.isArray(value) || value.length < 2) return null;
  const x = Number(value[0]);
  const y = Number(value[1]);
  return Number.isFinite(x) && Number.isFinite(y) ? [x, y] : null;
}

function normalizeCoordinate(value) {
  const pair = finitePair(value);
  if (!pair) return null;
  let lon = pair[0];
  while (lon > 180) lon -= 360;
  while (lon < -180) lon += 360;
  return [lon, Math.max(-90, Math.min(90, pair[1]))];
}

function add(a, b) {
  return [a[0] + b[0], a[1] + b[1]];
}

function subtract(a, b) {
  return [a[0] - b[0], a[1] - b[1]];
}

function multiply(vector, scalar) {
  return [vector[0] * scalar, vector[1] * scalar];
}

function midpoint(a, b) {
  return [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];
}

function average(points) {
  return [
    points.reduce((sum, point) => sum + point[0], 0) / points.length,
    points.reduce((sum, point) => sum + point[1], 0) / points.length,
  ];
}

function length(vector) {
  return Math.hypot(vector[0], vector[1]);
}

function normalizeVector(vector) {
  const magnitude = length(vector);
  return magnitude > 1e-9 ? [vector[0] / magnitude, vector[1] / magnitude] : null;
}

function rotatePoint(point, center, angleDegrees) {
  const angle = radians(angleDegrees);
  const cos = Math.cos(angle);
  const sin = Math.sin(angle);
  const delta = subtract(point, center);
  return [
    center[0] + delta[0] * cos - delta[1] * sin,
    center[1] + delta[0] * sin + delta[1] * cos,
  ];
}

function solveBasis(delta, u, v) {
  const determinant = u[0] * v[1] - u[1] * v[0];
  if (!Number.isFinite(determinant) || Math.abs(determinant) < 1e-8) return null;
  return [
    (delta[0] * v[1] - delta[1] * v[0]) / determinant,
    (u[0] * delta[1] - u[1] * delta[0]) / determinant,
  ];
}

function barycentric(point, triangle) {
  const [a, b, c] = triangle;
  const denominator = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1]);
  if (!Number.isFinite(denominator) || Math.abs(denominator) < 1e-8) return null;
  const w0 = ((b[1] - c[1]) * (point[0] - c[0]) + (c[0] - b[0]) * (point[1] - c[1])) / denominator;
  const w1 = ((c[1] - a[1]) * (point[0] - c[0]) + (a[0] - c[0]) * (point[1] - c[1])) / denominator;
  const w2 = 1 - w0 - w1;
  return w0 >= -0.002 && w1 >= -0.002 && w2 >= -0.002 ? [w0, w1, w2] : null;
}

function angularDistanceDegrees(a, b) {
  if (!a || !b) return Number.POSITIVE_INFINITY;
  const factor = Math.PI / 180;
  const lonA = a[0] * factor;
  const latA = a[1] * factor;
  const lonB = b[0] * factor;
  const latB = b[1] * factor;
  const dot = Math.sin(latA) * Math.sin(latB) + Math.cos(latA) * Math.cos(latB) * Math.cos(lonA - lonB);
  return Math.acos(Math.max(-1, Math.min(1, dot))) / factor;
}

function projectVisible(host, coordinate) {
  const projected = finitePair(host?.project?.(coordinate));
  if (!projected) return null;
  if (host.getProjectionKind?.() !== 'globe') return projected;
  const roundTrip = normalizeCoordinate(host.unproject?.(projected));
  return angularDistanceDegrees(coordinate, roundTrip) <= 0.25 ? projected : null;
}

function unprojectCorners(host, corners) {
  if (!host?.unproject || !Array.isArray(corners) || corners.length !== 4) return null;
  const quad = [];
  for (const corner of corners) {
    const coordinate = normalizeCoordinate(host.unproject(corner));
    if (!coordinate) return null;
    quad.push(coordinate);
  }
  return quad;
}

function applyScreenCorners(record, corners, host) {
  const quad = unprojectCorners(host, corners);
  if (!record || !quad) return false;
  record.mapQuad = quad;
  return true;
}

export function normalizeReferenceImageRotation(value) {
  let angle = Number(value);
  if (!Number.isFinite(angle)) angle = 0;
  angle %= 360;
  if (angle > 180) angle -= 360;
  if (angle <= -180) angle += 360;
  return angle;
}

export function normalizeReferenceImageScreenRect(value) {
  if (!value || typeof value !== 'object') return null;
  const x = Number(value.x);
  const y = Number(value.y);
  const width = Number(value.width);
  const height = Number(value.height);
  if (![x, y, width, height].every(Number.isFinite)) return null;
  return {
    x,
    y,
    width: Math.max(REFERENCE_IMAGE_TRANSFORM.minimumWidth, width),
    height: Math.max(REFERENCE_IMAGE_TRANSFORM.minimumHeight, height),
  };
}

export function defaultReferenceImageScreenRect(image, mapElement) {
  const bounds = mapElement.getBoundingClientRect();
  const maxWidth = Math.max(180, bounds.width * 0.62);
  const maxHeight = Math.max(140, bounds.height * 0.62);
  const naturalWidth = Math.max(1, Number(image?.naturalWidth) || 1);
  const naturalHeight = Math.max(1, Number(image?.naturalHeight) || 1);
  const scale = Math.min(maxWidth / naturalWidth, maxHeight / naturalHeight);
  const width = Math.max(80, naturalWidth * scale);
  const height = Math.max(60, naturalHeight * scale);
  return {
    x: (bounds.width - width) / 2,
    y: (bounds.height - height) / 2,
    width,
    height,
  };
}

export function referenceImageScreenRectToMapQuad(screenRect, rotation, host) {
  const rect = normalizeReferenceImageScreenRect(screenRect);
  if (!rect || !host?.unproject) return null;
  const center = [rect.x + rect.width / 2, rect.y + rect.height / 2];
  const corners = [
    [rect.x, rect.y],
    [rect.x + rect.width, rect.y],
    [rect.x + rect.width, rect.y + rect.height],
    [rect.x, rect.y + rect.height],
  ].map(point => rotatePoint(point, center, normalizeReferenceImageRotation(rotation)));
  return unprojectCorners(host, corners);
}

export function defaultReferenceImageMapQuad(image, mapElement, host) {
  let rect = defaultReferenceImageScreenRect(image, mapElement);
  for (let attempt = 0; attempt < 8; attempt += 1) {
    const quad = referenceImageScreenRectToMapQuad(rect, 0, host);
    if (quad) return quad;
    const center = [rect.x + rect.width / 2, rect.y + rect.height / 2];
    const width = Math.max(REFERENCE_IMAGE_TRANSFORM.minimumWidth, rect.width * 0.82);
    const height = Math.max(REFERENCE_IMAGE_TRANSFORM.minimumHeight, rect.height * 0.82);
    rect = {
      x: center[0] - width / 2,
      y: center[1] - height / 2,
      width,
      height,
    };
  }
  return null;
}

export function projectReferenceImageMapQuad(record, host) {
  if (!record?.mapQuad || record.mapQuad.length !== 4 || !host?.project) return null;
  const corners = record.mapQuad.map(coordinate => projectVisible(host, coordinate));
  return corners.every(Boolean) ? corners : null;
}

export function referenceImageAnchorScreenPoint(record, host) {
  const coordinate = record?.anchor?.coordinate;
  return coordinate ? projectVisible(host, coordinate) : null;
}

export function buildReferenceImagePlacementWarp(record) {
  if (!record?.mapQuad || record.mapQuad.length !== 4) {
    return buildReferenceImageProjectiveWarpFromQuad(null);
  }
  return buildReferenceImageProjectiveWarpFromQuad(record.mapQuad);
}

export function referenceImagePlacementCoordinateAtUv(record, imageUv) {
  const pair = finitePair(imageUv);
  if (!pair || pair.some(component => component < 0 || component > 1)) return null;
  const warp = buildReferenceImagePlacementWarp(record);
  if (!warp.ok) return null;
  const u = record.flipX ? 1 - pair[0] : pair[0];
  const v = record.flipY ? 1 - pair[1] : pair[1];
  return warp.project([u, v]);
}

export function referenceImagePlacementPointAtUv(record, imageUv, host) {
  const coordinate = referenceImagePlacementCoordinateAtUv(record, imageUv);
  return coordinate ? projectVisible(host, coordinate) : null;
}

export function alignReferenceImageAnchor(record) {
  if (!record?.anchor || !record.mapQuad) return false;
  const current = referenceImagePlacementCoordinateAtUv(record, record.anchor.image);
  const target = normalizeCoordinate(record.anchor.coordinate);
  if (!current || !target) return false;

  const targetLon = unwrapLongitude(target[0], current[0]);
  const deltaLon = targetLon - current[0];
  const deltaLat = target[1] - current[1];
  const original = record.mapQuad.map(coordinate => [...coordinate]);
  const translated = [];
  for (const coordinate of original) {
    const lon = unwrapLongitude(coordinate[0], current[0]) + deltaLon;
    const lat = coordinate[1] + deltaLat;
    if (!Number.isFinite(lon) || !Number.isFinite(lat) || lat < -90 || lat > 90) return false;
    translated.push(normalizeCoordinate([lon, lat]));
  }
  if (translated.some(coordinate => !coordinate)) return false;
  record.mapQuad = translated;

  const aligned = referenceImagePlacementCoordinateAtUv(record, record.anchor.image);
  if (!aligned) {
    record.mapQuad = original;
    return false;
  }
  const lonError = Math.abs(unwrapLongitude(aligned[0], target[0]) - target[0]);
  const latError = Math.abs(aligned[1] - target[1]);
  if (lonError > 1e-8 || latError > 1e-8) {
    record.mapQuad = original;
    return false;
  }
  return true;
}

export function referenceImagePlacementGeometry(record, host) {
  const corners = projectReferenceImageMapQuad(record, host);
  if (!corners) return null;
  const [nw, ne, se, sw] = corners;
  const center = average(corners);
  const topMid = midpoint(nw, ne);
  const rightMid = midpoint(ne, se);
  const bottomMid = midpoint(sw, se);
  const leftMid = midpoint(nw, sw);
  const topVector = subtract(ne, nw);
  const bottomVector = subtract(se, sw);
  const leftVector = subtract(sw, nw);
  const rightVector = subtract(se, ne);
  const widthVector = multiply(add(topVector, bottomVector), 0.5);
  const heightVector = multiply(add(leftVector, rightVector), 0.5);
  const u = normalizeVector(widthVector);
  const v = normalizeVector(heightVector);
  const topUnit = normalizeVector(topVector);
  if (!u || !v || !topUnit) return null;
  const outward = [topUnit[1], -topUnit[0]];
  const rotateHandle = add(topMid, multiply(outward, REFERENCE_IMAGE_TRANSFORM.rotateHandleOffset));
  const handles = Object.freeze({
    nw,
    n: topMid,
    ne,
    e: rightMid,
    se,
    s: bottomMid,
    sw,
    w: leftMid,
  });
  return Object.freeze({
    corners,
    center,
    handles,
    rotateHandle,
    width: Math.max(1e-9, length(widthVector)),
    height: Math.max(1e-9, length(heightVector)),
    u,
    v,
    rotation: normalizeReferenceImageRotation(degrees(Math.atan2(topVector[1], topVector[0]))),
  });
}

export function referenceImagePlacementRotation(record, host) {
  return referenceImagePlacementGeometry(record, host)?.rotation ?? 0;
}

export function referenceImagePlacementHit(record, point, host) {
  const candidate = finitePair(point);
  const geometry = referenceImagePlacementGeometry(record, host);
  if (!candidate || !geometry) return null;
  const radius = REFERENCE_IMAGE_TRANSFORM.hitRadius;
  if (Math.hypot(candidate[0] - geometry.rotateHandle[0], candidate[1] - geometry.rotateHandle[1]) <= radius) {
    return Object.freeze({ type: 'rotate' });
  }
  for (const handle of HANDLE_ORDER) {
    const location = geometry.handles[handle];
    if (Math.hypot(candidate[0] - location[0], candidate[1] - location[1]) <= radius) {
      return Object.freeze({ type: 'resize', handle });
    }
  }
  if (
    barycentric(candidate, [geometry.corners[0], geometry.corners[1], geometry.corners[2]])
    || barycentric(candidate, [geometry.corners[0], geometry.corners[2], geometry.corners[3]])
  ) {
    return record.anchor ? null : Object.freeze({ type: 'move' });
  }
  return null;
}

export function createReferenceImagePlacementDrag(record, hit, point, host, pointerId = null) {
  const startPoint = finitePair(point);
  const geometry = referenceImagePlacementGeometry(record, host);
  if (!geometry || !hit || !startPoint) return null;
  const anchorPoint = referenceImageAnchorScreenPoint(record, host);
  if (record.anchor && !anchorPoint) return null;
  const pivot = anchorPoint || geometry.center;
  return {
    pointerId,
    recordId: record.id,
    hit,
    startPoint,
    startCorners: geometry.corners.map(corner => [...corner]),
    startMapQuad: record.mapQuad.map(coordinate => [...coordinate]),
    center: [...geometry.center],
    pivot: [...pivot],
    anchored: !!record.anchor,
    startRotation: geometry.rotation,
    startAngle: degrees(Math.atan2(startPoint[1] - pivot[1], startPoint[0] - pivot[0])),
    width: geometry.width,
    height: geometry.height,
    aspect: geometry.width / Math.max(1e-9, geometry.height),
    u: [...geometry.u],
    v: [...geometry.v],
  };
}

function scaleCornersInBasis(corners, pivot, basisU, basisV, scaleU, scaleV) {
  return corners.map(corner => {
    const components = solveBasis(subtract(corner, pivot), basisU, basisV);
    if (!components) return null;
    return add(
      pivot,
      add(multiply(basisU, components[0] * scaleU), multiply(basisV, components[1] * scaleV)),
    );
  });
}

function resizeCorner(drag, point, shiftKey) {
  const cornerIndex = { nw: 0, ne: 1, se: 2, sw: 3 }[drag.hit.handle];
  const oppositeIndex = { nw: 2, ne: 3, se: 0, sw: 1 }[drag.hit.handle];
  if (!Number.isInteger(cornerIndex) || !Number.isInteger(oppositeIndex)) return null;
  const pivot = drag.startCorners[oppositeIndex];
  const startHandle = drag.startCorners[cornerIndex];
  const start = solveBasis(subtract(startHandle, pivot), drag.u, drag.v);
  const next = solveBasis(subtract(point, pivot), drag.u, drag.v);
  if (!start || !next || Math.abs(start[0]) < 1e-8 || Math.abs(start[1]) < 1e-8) return null;

  const minScaleX = REFERENCE_IMAGE_TRANSFORM.minimumWidth / Math.max(1e-9, drag.width);
  const minScaleY = REFERENCE_IMAGE_TRANSFORM.minimumHeight / Math.max(1e-9, drag.height);
  let scaleX = Math.max(minScaleX, next[0] / start[0]);
  let scaleY = Math.max(minScaleY, next[1] / start[1]);

  if (shiftKey) {
    const startDelta = subtract(startHandle, pivot);
    const nextDelta = subtract(point, pivot);
    const denominator = startDelta[0] * startDelta[0] + startDelta[1] * startDelta[1];
    if (denominator < 1e-8) return null;
    const uniform = Math.max(
      Math.max(minScaleX, minScaleY),
      (nextDelta[0] * startDelta[0] + nextDelta[1] * startDelta[1]) / denominator,
    );
    scaleX = uniform;
    scaleY = uniform;
  }
  return scaleCornersInBasis(drag.startCorners, pivot, drag.u, drag.v, scaleX, scaleY);
}

function resizeEdge(drag, point) {
  const [nw, ne, se, sw] = drag.startCorners;
  const definitions = {
    e: { pivot: midpoint(nw, sw), handle: midpoint(ne, se), fixedAxis: normalizeVector(subtract(sw, nw)), axis: 'u' },
    w: { pivot: midpoint(ne, se), handle: midpoint(nw, sw), fixedAxis: normalizeVector(subtract(se, ne)), axis: 'u' },
    s: { pivot: midpoint(nw, ne), handle: midpoint(sw, se), fixedAxis: normalizeVector(subtract(ne, nw)), axis: 'v' },
    n: { pivot: midpoint(sw, se), handle: midpoint(nw, ne), fixedAxis: normalizeVector(subtract(se, sw)), axis: 'v' },
  };
  const definition = definitions[drag.hit.handle];
  if (!definition?.fixedAxis) return null;
  const scaleAxis = normalizeVector(subtract(definition.handle, definition.pivot));
  if (!scaleAxis) return null;

  const startDistance = length(subtract(definition.handle, definition.pivot));
  const nextComponent = solveBasis(
    subtract(point, definition.pivot),
    scaleAxis,
    definition.fixedAxis,
  );
  if (!nextComponent || startDistance < 1e-8) return null;

  const minimum = definition.axis === 'u'
    ? REFERENCE_IMAGE_TRANSFORM.minimumWidth / Math.max(1e-9, drag.width)
    : REFERENCE_IMAGE_TRANSFORM.minimumHeight / Math.max(1e-9, drag.height);
  const scale = Math.max(minimum, nextComponent[0] / startDistance);

  if (definition.axis === 'u') {
    return scaleCornersInBasis(drag.startCorners, definition.pivot, scaleAxis, definition.fixedAxis, scale, 1);
  }
  return scaleCornersInBasis(drag.startCorners, definition.pivot, definition.fixedAxis, scaleAxis, 1, scale);
}

function resizeAroundAnchor(drag, point, shiftKey) {
  const handle = drag.hit.handle;
  const startHandle = {
    nw: drag.startCorners[0],
    n: midpoint(drag.startCorners[0], drag.startCorners[1]),
    ne: drag.startCorners[1],
    e: midpoint(drag.startCorners[1], drag.startCorners[2]),
    se: drag.startCorners[2],
    s: midpoint(drag.startCorners[3], drag.startCorners[2]),
    sw: drag.startCorners[3],
    w: midpoint(drag.startCorners[0], drag.startCorners[3]),
  }[handle];
  if (!startHandle) return null;

  const startComponents = solveBasis(subtract(startHandle, drag.pivot), drag.u, drag.v);
  const nextComponents = solveBasis(subtract(point, drag.pivot), drag.u, drag.v);
  if (!startComponents || !nextComponents) return null;

  const horizontal = ['nw', 'ne', 'e', 'se', 'sw', 'w'].includes(handle);
  const vertical = ['nw', 'n', 'ne', 'se', 's', 'sw'].includes(handle);
  const minScaleX = REFERENCE_IMAGE_TRANSFORM.minimumWidth / Math.max(1e-9, drag.width);
  const minScaleY = REFERENCE_IMAGE_TRANSFORM.minimumHeight / Math.max(1e-9, drag.height);
  let scaleX = 1;
  let scaleY = 1;

  if (horizontal) {
    if (Math.abs(startComponents[0]) < 1e-8) return null;
    scaleX = Math.max(minScaleX, nextComponents[0] / startComponents[0]);
  }
  if (vertical) {
    if (Math.abs(startComponents[1]) < 1e-8) return null;
    scaleY = Math.max(minScaleY, nextComponents[1] / startComponents[1]);
  }

  if (shiftKey && horizontal && vertical) {
    const startDelta = subtract(startHandle, drag.pivot);
    const nextDelta = subtract(point, drag.pivot);
    const denominator = startDelta[0] * startDelta[0] + startDelta[1] * startDelta[1];
    if (denominator < 1e-8) return null;
    const uniform = Math.max(
      Math.max(minScaleX, minScaleY),
      (nextDelta[0] * startDelta[0] + nextDelta[1] * startDelta[1]) / denominator,
    );
    scaleX = uniform;
    scaleY = uniform;
  }

  return drag.startCorners.map(corner => {
    const components = solveBasis(subtract(corner, drag.pivot), drag.u, drag.v);
    if (!components) return null;
    return add(
      drag.pivot,
      add(multiply(drag.u, components[0] * scaleX), multiply(drag.v, components[1] * scaleY)),
    );
  });
}

export function applyReferenceImagePlacementDrag(record, drag, point, host, { shiftKey = false } = {}) {
  const candidate = finitePair(point);
  if (!record || !drag || !candidate || !host) return false;
  if (drag.hit.type === 'move') {
    if (drag.anchored) return false;
    const delta = subtract(candidate, drag.startPoint);
    return applyScreenCorners(record, drag.startCorners.map(corner => add(corner, delta)), host);
  }
  if (drag.hit.type === 'rotate') {
    const angle = degrees(Math.atan2(candidate[1] - drag.pivot[1], candidate[0] - drag.pivot[0]));
    let target = normalizeReferenceImageRotation(drag.startRotation + (angle - drag.startAngle));
    if (shiftKey) target = normalizeReferenceImageRotation(Math.round(target / 15) * 15);
    const delta = normalizeReferenceImageRotation(target - drag.startRotation);
    const changed = applyScreenCorners(
      record,
      drag.startCorners.map(corner => rotatePoint(corner, drag.pivot, delta)),
      host,
    );
    return changed && (!drag.anchored || alignReferenceImageAnchor(record));
  }
  if (drag.hit.type !== 'resize') return false;
  const corners = drag.anchored
    ? resizeAroundAnchor(drag, candidate, shiftKey)
    : ['nw', 'ne', 'se', 'sw'].includes(drag.hit.handle)
      ? resizeCorner(drag, candidate, shiftKey)
      : resizeEdge(drag, candidate);
  if (!corners || corners.some(candidateCorner => !candidateCorner)) return false;
  const changed = applyScreenCorners(record, corners, host);
  return changed && (!drag.anchored || alignReferenceImageAnchor(record));
}

export function setReferenceImagePlacementRotation(record, host, value) {
  const geometry = referenceImagePlacementGeometry(record, host);
  if (!geometry) return false;
  const target = normalizeReferenceImageRotation(value);
  const delta = normalizeReferenceImageRotation(target - geometry.rotation);
  const anchorPoint = referenceImageAnchorScreenPoint(record, host);
  if (record.anchor && !anchorPoint) return false;
  const pivot = anchorPoint || geometry.center;
  const changed = applyScreenCorners(record, geometry.corners.map(corner => rotatePoint(corner, pivot, delta)), host);
  return changed && (!record.anchor || alignReferenceImageAnchor(record));
}

function unwrapLongitude(value, reference) {
  let result = value;
  while (result - reference > 180) result -= 360;
  while (result - reference < -180) result += 360;
  return result;
}

export function buildReferenceImagePlacementMesh(record, { columns = 12, rows = 8 } = {}) {
  const warp = buildReferenceImagePlacementWarp(record);
  return warp.ok ? buildReferenceImageMesh(warp, { columns, rows }) : null;
}

function signedArea(points) {
  let area = 0;
  for (let index = 0; index < points.length; index += 1) {
    const current = points[index];
    const next = points[(index + 1) % points.length];
    area += current[0] * next[1] - next[0] * current[1];
  }
  return area / 2;
}

function orientation(a, b, c) {
  return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}

function properSegmentsIntersect(a, b, c, d) {
  const abC = orientation(a, b, c);
  const abD = orientation(a, b, d);
  const cdA = orientation(c, d, a);
  const cdB = orientation(c, d, b);
  return abC * abD < 0 && cdA * cdB < 0;
}

function strictlyConvexQuad(points) {
  if (!Array.isArray(points) || points.length !== 4) return false;
  const turns = [];
  for (let index = 0; index < 4; index += 1) {
    const a = points[index];
    const b = points[(index + 1) % 4];
    const c = points[(index + 2) % 4];
    const turn = orientation(a, b, c);
    if (Math.abs(turn) < 1e-6) return false;
    turns.push(Math.sign(turn));
  }
  return turns.every(sign => sign === turns[0]);
}

function usableFreeTransformQuad(record, host) {
  const points = projectReferenceImageMapQuad(record, host);
  if (!points || Math.abs(signedArea(points)) < 64 || !strictlyConvexQuad(points)) return false;
  if (properSegmentsIntersect(points[0], points[1], points[2], points[3])) return false;
  if (properSegmentsIntersect(points[1], points[2], points[3], points[0])) return false;
  return buildReferenceImagePlacementWarp(record).ok;
}

export function referenceImageFreeTransformHit(record, point, host) {
  if (!record) return null;
  const candidate = finitePair(point);
  const corners = projectReferenceImageMapQuad(record, host);
  if (!candidate || !corners) return null;
  const names = ['nw', 'ne', 'se', 'sw'];
  for (let index = 0; index < corners.length; index += 1) {
    if (Math.hypot(candidate[0] - corners[index][0], candidate[1] - corners[index][1]) <= REFERENCE_IMAGE_TRANSFORM.hitRadius + 2) {
      return Object.freeze({ type: 'corner-pin', corner: names[index], index });
    }
  }
  return null;
}

export function createReferenceImageFreeTransformDrag(record, hit, pointerId = null) {
  if (!record || hit?.type !== 'corner-pin') return null;
  if (!Number.isInteger(hit.index) || hit.index < 0 || hit.index > 3) return null;
  return {
    pointerId,
    recordId: record.id,
    index: hit.index,
    corner: hit.corner,
    startMapQuad: record.mapQuad.map(coordinate => [...coordinate]),
  };
}

export function applyReferenceImageFreeTransformDrag(record, drag, point, host) {
  if (!record || !drag || !host) return false;
  const coordinate = normalizeCoordinate(host.unproject?.(point));
  if (!coordinate) return false;
  const previous = record.mapQuad;
  const candidate = drag.startMapQuad.map(value => [...value]);
  candidate[drag.index] = coordinate;
  record.mapQuad = candidate;
  if (!usableFreeTransformQuad(record, host)) {
    record.mapQuad = previous;
    return false;
  }
  return true;
}

export function referenceImagePlacementUvAtPoint(record, point, host) {
  const candidate = finitePair(point);
  const mesh = buildReferenceImagePlacementMesh(record);
  if (!candidate || !mesh) return null;
  const projected = mesh.vertices.map(vertex => projectVisible(host, vertex.coordinate));
  for (const triangle of mesh.triangles) {
    const destination = triangle.map(index => projected[index]);
    if (destination.some(vertex => !vertex)) continue;
    const weights = barycentric(candidate, destination);
    if (!weights) continue;
    const uv = triangle.map(index => mesh.vertices[index].uv);
    let u = weights[0] * uv[0][0] + weights[1] * uv[1][0] + weights[2] * uv[2][0];
    let v = weights[0] * uv[0][1] + weights[1] * uv[1][1] + weights[2] * uv[2][1];
    u = Math.max(0, Math.min(1, u));
    v = Math.max(0, Math.min(1, v));
    if (record.flipX) u = 1 - u;
    if (record.flipY) v = 1 - v;
    return [u, v];
  }
  return null;
}
