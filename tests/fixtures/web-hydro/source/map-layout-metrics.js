const DEFAULT_SAFE_INSETS = Object.freeze({ left: 0, right: 0, top: 0, bottom: 26 });

function finiteNumber(value, fallback = 0) {
  const number = Number(value);
  return Number.isFinite(number) ? number : fallback;
}

function normalizedInsets(value = DEFAULT_SAFE_INSETS, minimumBottom = 0) {
  return Object.freeze({
    left: Math.max(0, finiteNumber(value.left)),
    right: Math.max(0, finiteNumber(value.right)),
    top: Math.max(0, finiteNumber(value.top)),
    bottom: Math.max(minimumBottom, finiteNumber(value.bottom)),
  });
}

function wrappedLongitude(value) {
  return ((finiteNumber(value) + 540) % 360) - 180;
}

export function createMapLayoutMetricsSnapshot({
  width = 900,
  height = 650,
  dpr = 1,
  safeInsets = DEFAULT_SAFE_INSETS,
  fitInsets = safeInsets,
  mobile = false,
  revision = 0,
  reason = 'initial',
} = {}) {
  const resolvedWidth = Math.max(1, finiteNumber(width, 900));
  const resolvedHeight = Math.max(1, finiteNumber(height, 650));
  const safe = normalizedInsets(safeInsets, 26);
  const fit = normalizedInsets(fitInsets, safe.bottom);
  const contentWidth = Math.max(1, resolvedWidth - safe.left - safe.right);
  const contentHeight = Math.max(1, resolvedHeight - safe.top - safe.bottom);
  const scaleContentHeight = mobile
    ? Math.max(1, resolvedHeight - safe.top - 96)
    : contentHeight;
  const projectionSignature = [
    resolvedWidth.toFixed(3),
    resolvedHeight.toFixed(3),
    Math.max(1, finiteNumber(dpr, 1)).toFixed(3),
    safe.left,
    safe.right,
    safe.top,
    safe.bottom,
    mobile ? 1 : 0,
  ].join(':');

  return Object.freeze({
    revision: Math.max(0, Math.trunc(finiteNumber(revision))),
    reason: String(reason || 'layout'),
    width: resolvedWidth,
    height: resolvedHeight,
    dpr: Math.max(1, finiteNumber(dpr, 1)),
    mobile: mobile === true,
    safe,
    fitInsets: fit,
    contentWidth,
    contentHeight,
    centerX: safe.left + contentWidth / 2,
    centerY: safe.top + contentHeight / 2,
    globeBaseScale: Math.max(60, Math.min(contentWidth, scaleContentHeight) * 0.455),
    flatBaseScale: Math.max(30, contentWidth / (2 * Math.PI)),
    projectionSignature,
    fitSignature: [fit.left, fit.right, fit.top, fit.bottom].join(':'),
  });
}

export function equirectangularCenterForAnchor({
  coordinate,
  screenPoint,
  translate,
  scale,
  latitudeLimit = 89.999,
} = {}) {
  if (!Array.isArray(coordinate) || !Array.isArray(screenPoint) || !Array.isArray(translate)) return null;
  const resolvedScale = finiteNumber(scale);
  if (!(resolvedScale > 0)) return null;
  const longitude = finiteNumber(coordinate[0], Number.NaN);
  const latitude = finiteNumber(coordinate[1], Number.NaN);
  const screenX = finiteNumber(screenPoint[0], Number.NaN);
  const screenY = finiteNumber(screenPoint[1], Number.NaN);
  const translateX = finiteNumber(translate[0], Number.NaN);
  const translateY = finiteNumber(translate[1], Number.NaN);
  if (![longitude, latitude, screenX, screenY, translateX, translateY].every(Number.isFinite)) return null;

  const degreesPerPixel = 180 / (Math.PI * resolvedScale);
  const limit = Math.max(0, Math.min(89.999999, finiteNumber(latitudeLimit, 89.999)));
  return [
    wrappedLongitude(longitude - (screenX - translateX) * degreesPerPixel),
    Math.max(-limit, Math.min(limit, latitude + (screenY - translateY) * degreesPerPixel)),
  ];
}

export { DEFAULT_SAFE_INSETS };
