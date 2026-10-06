const PI = Math.PI;

const freezePair = value => Object.freeze([Number(value?.[0] || 0), Number(value?.[1] || 0)]);
const freezeTriple = value => Object.freeze([
  Number(value?.[0] || 0),
  Number(value?.[1] || 0),
  Number(value?.[2] || 0),
]);

function visibleFlatWorldOffsets({ translateX, scale, flatCenterRadians, viewportWidth }) {
  const candidates = [-2 * PI, 0, 2 * PI];
  const visible = candidates.filter(worldOffset => {
    const left = translateX + scale * (-PI + worldOffset - flatCenterRadians);
    const right = translateX + scale * (PI + worldOffset - flatCenterRadians);
    return Math.min(right, viewportWidth) - Math.max(left, 0) > 0.5;
  });
  if (visible.length) return visible;
  const viewportCenter = viewportWidth / 2;
  return [candidates.reduce((nearest, worldOffset) => {
    const center = translateX + scale * (worldOffset - flatCenterRadians);
    const distance = Math.abs(center - viewportCenter);
    return distance < nearest.distance ? { worldOffset, distance } : nearest;
  }, { worldOffset: 0, distance: Infinity }).worldOffset];
}

function globeRows(projectCoordinate, translate, scale) {
  if (typeof projectCoordinate !== 'function' || !(scale > 0)) {
    return { rowX: [1, 0, 0], rowY: [0, 1, 0], rowZ: [0, 0, 1] };
  }
  const basis = [[0, 0], [90, 0], [0, 90]].map(projectCoordinate);
  if (basis.some(point => !Array.isArray(point) || !Number.isFinite(point[0]) || !Number.isFinite(point[1]))) {
    return { rowX: [1, 0, 0], rowY: [0, 1, 0], rowZ: [0, 0, 1] };
  }
  const rowX = basis.map(point => (point[0] - translate[0]) / scale);
  const rowY = basis.map(point => (point[1] - translate[1]) / scale);
  const cross = [
    rowX[1] * rowY[2] - rowX[2] * rowY[1],
    rowX[2] * rowY[0] - rowX[0] * rowY[2],
    rowX[0] * rowY[1] - rowX[1] * rowY[0],
  ];
  const length = Math.hypot(...cross) || 1;
  return { rowX, rowY, rowZ: cross.map(value => -value / length) };
}

function coordinateVector(coordinate) {
  const longitude = Number(coordinate?.[0]);
  const latitude = Number(coordinate?.[1]);
  if (!Number.isFinite(longitude) || !Number.isFinite(latitude)) return null;
  const lon = longitude * PI / 180;
  const lat = latitude * PI / 180;
  const cosine = Math.cos(lat);
  return { lon, lat, point: [cosine * Math.cos(lon), cosine * Math.sin(lon), Math.sin(lat)] };
}

export function createFrameProjectors({
  mode,
  cssTranslate,
  cssScale,
  cssViewport,
  safeInset,
  flatCenter,
  worldOffsets,
  rows,
}) {
  const dot = (row, point) => row[0] * point[0] + row[1] * point[1] + row[2] * point[2];
  const projectCoordinate = coordinate => {
    const normalized = coordinateVector(coordinate);
    if (!normalized) return null;
    if (mode === 0) {
      return Object.freeze([
        cssTranslate[0] + cssScale * dot(rows.rowX, normalized.point),
        cssTranslate[1] + cssScale * dot(rows.rowY, normalized.point),
      ]);
    }
    const candidates = worldOffsets.map(worldOffset => [
      cssTranslate[0] + cssScale * (normalized.lon + worldOffset - flatCenter[0]),
      cssTranslate[1] - cssScale * (normalized.lat - flatCenter[1]),
    ]);
    const visible = candidates.find(point => point[0] >= safeInset.left - 30
      && point[0] <= cssViewport[0] - safeInset.right + 30
      && point[1] >= safeInset.top - 30
      && point[1] <= cssViewport[1] - safeInset.bottom + 30);
    return Object.freeze(visible || candidates.reduce((nearest, point) => (
      Math.abs(point[0] - cssViewport[0] / 2) < Math.abs(nearest[0] - cssViewport[0] / 2) ? point : nearest
    ), candidates[0]));
  };
  const projectVisibleCoordinate = coordinate => {
    const normalized = coordinateVector(coordinate);
    if (!normalized) return null;
    if (mode === 0 && dot(rows.rowZ, normalized.point) < -0.005) return null;
    const point = projectCoordinate(coordinate);
    if (!point) return null;
    if (mode === 1 && (point[0] < safeInset.left - 30
      || point[0] > cssViewport[0] - safeInset.right + 30
      || point[1] < safeInset.top - 30
      || point[1] > cssViewport[1] - safeInset.bottom + 30)) return null;
    return point;
  };
  return { projectCoordinate, projectVisibleCoordinate };
}

/**
 * Creates the sole immutable projection snapshot consumed by every visible
 * surface in a coordinator frame. CSS and physical-pixel values deliberately
 * live together so GPU, SVG and Canvas fallbacks cannot sample different views.
 */
export function createMapVisualFrame({
  frameId,
  viewRevision,
  projectGeneration = 0,
  projectionRevision = 0,
  viewState,
  layoutSnapshot = null,
  projectCoordinate = null,
  projectPath = null,
} = {}) {
  if (!viewState || typeof viewState !== 'object') throw new TypeError('MapVisualFrame requires viewState.');
  const projectionKind = viewState.projection === 'flat' ? 'flat' : 'globe';
  const mode = projectionKind === 'globe' ? 0 : 1;
  const width = Math.max(1, Number(viewState.size?.width || layoutSnapshot?.width || 1));
  const height = Math.max(1, Number(viewState.size?.height || layoutSnapshot?.height || 1));
  const dpr = Math.max(1, Number(viewState.dpr || layoutSnapshot?.dpr || 1));
  const cssTranslate = freezePair(viewState.translate || [width / 2, height / 2]);
  const cssScale = Math.max(1e-9, Number(viewState.scale || 1));
  const flatCenterDegrees = freezePair(viewState.projectionCenter || viewState.flatCenter || [0, 0]);
  const flatCenter = freezePair([flatCenterDegrees[0] * PI / 180, flatCenterDegrees[1] * PI / 180]);
  const rows = mode === 0
    ? globeRows(projectCoordinate, cssTranslate, cssScale)
    : { rowX: [1, 0, 0], rowY: [0, 1, 0], rowZ: [0, 0, 1] };
  const cssViewport = freezePair([width, height]);
  const gpuViewport = freezePair([width * dpr, height * dpr]);
  const gpuTranslate = freezePair([cssTranslate[0] * dpr, cssTranslate[1] * dpr]);
  const gpuScale = cssScale * dpr;
  const worldOffsets = Object.freeze(mode === 0 ? [0] : visibleFlatWorldOffsets({
    translateX: gpuTranslate[0],
    scale: gpuScale,
    flatCenterRadians: flatCenter[0],
    viewportWidth: gpuViewport[0],
  }));
  const safeSource = viewState.safeInset || layoutSnapshot?.safe || {};
  const safeInset = Object.freeze({
    top: Number(safeSource.top || 0),
    right: Number(safeSource.right || 0),
    bottom: Number(safeSource.bottom || 0),
    left: Number(safeSource.left || 0),
  });
  const projectors = createFrameProjectors({
    mode,
    cssTranslate,
    cssScale,
    cssViewport,
    safeInset,
    flatCenter,
    worldOffsets,
    rows,
  });
  const resolvedFrameId = Math.max(0, Number(frameId || 0));
  const resolvedViewRevision = Math.max(0, Number(viewRevision ?? viewState.revision ?? 0));
  const rotation = projectionKind === 'globe' ? freezeTriple(viewState.rotation || viewState.globeRotation || [0, 0, 0]) : null;
  const projectionCenter = projectionKind === 'flat' ? flatCenterDegrees : null;
  const frozenViewState = Object.freeze({
    ...viewState,
    size: Object.freeze({ width, height }),
    safeInset,
    translate: cssTranslate,
    scale: cssScale,
    rotation,
    projectionCenter,
    flatCenter: flatCenterDegrees,
    globeRotation: rotation,
  });
  const signature = [resolvedFrameId, resolvedViewRevision, projectGeneration, projectionRevision,
    projectionKind, width, height, dpr, ...cssTranslate, cssScale,
    ...(rotation || []), ...flatCenterDegrees].join(':');
  return Object.freeze({
    ...frozenViewState,
    __mapVisualFrame: true,
    frameId: resolvedFrameId,
    revision: resolvedViewRevision,
    viewRevision: resolvedViewRevision,
    projectGeneration: Number(projectGeneration || 0),
    projectionRevision: Number(projectionRevision || 0),
    projection: projectionKind,
    projectionKind,
    mode,
    size: Object.freeze({ width, height }),
    safeInset,
    dpr,
    cssViewport,
    cssTranslate,
    cssScale,
    gpuViewport,
    gpuTranslate,
    gpuScale,
    // Compatibility names used by the existing GPU passes.
    viewport: gpuViewport,
    translate: gpuTranslate,
    scale: gpuScale,
    rowX: freezeTriple(rows.rowX),
    rowY: freezeTriple(rows.rowY),
    rowZ: freezeTriple(rows.rowZ),
    flatCenter,
    worldOffsets,
    projectCoordinate: projectors.projectCoordinate,
    projectVisibleCoordinate: projectors.projectVisibleCoordinate,
    projectPath: typeof projectPath === 'function' ? projectPath : null,
    signature,
    viewState: frozenViewState,
  });
}

export function isMapVisualFrame(value) {
  return value?.__mapVisualFrame === true && Number.isFinite(value.frameId) && typeof value.signature === 'string';
}
