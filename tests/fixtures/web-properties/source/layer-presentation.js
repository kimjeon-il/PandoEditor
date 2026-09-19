export const LAYER_PRESENTATION_SCHEMA_VERSION = 3;

export const TERRITORIAL_SYMBOL_KEYS = Object.freeze({
  countries: Object.freeze({ name: 'basemapLabels', flag: 'countryFlags' }),
  subunits: Object.freeze({ name: 'subunitLabels', flag: 'subunitFlags' }),
  regions: Object.freeze({ name: 'regionLabels', flag: 'regionFlags' }),
});

export function territorialSymbolGroup(feature) {
  return feature?.properties?.unitType === 'subunit' ? 'subunits'
    : feature?.properties?.unitType === 'region' ? 'regions' : 'countries';
}

export function territorialSymbolVisibility(state, group) {
  const keys = TERRITORIAL_SYMBOL_KEYS[group];
  const visible = state.layerVisibility?.[group] !== false;
  return {
    name: visible && state.layerVisibility?.[keys.name] !== false,
    flag: visible && state.layerVisibility?.[keys.flag] !== false,
  };
}

export const OVERLAY_GROUPS = Object.freeze([
  'religions',
  'ethnicities',
  'languages',
  'subunits',
  'regions',
  'genericFeatures',
]);

const PRESENTATION_GROUPS = Object.freeze([
  'labels',
  'countryLabels',
  ...OVERLAY_GROUPS,
  'rivers',
  'lakes',
  'countries',
  'terrain',
]);

const DEFAULT_STYLE = Object.freeze({
  opacity: 1,
  boundaryVisible: true,
  boundaryWidth: 1,
  labelsVisible: true,
  blendMode: 'normal',
});

const clamp = (value, min, max) => Math.max(min, Math.min(max, value));
const orderIndexCache = new WeakMap();

export function layerObjectRank(presentation, objectKey) {
  const order = presentation?.objectOrder;
  if (!objectKey || !Array.isArray(order) || !order.length) return 0;
  let index = orderIndexCache.get(order);
  if (!index) {
    index = new Map(order.map((key, rank) => [key, rank]));
    orderIndexCache.set(order, index);
  }
  return (index.get(objectKey) ?? order.length) / (order.length + 1);
}

function normalizeLayerStyle(value = {}) {
  return {
    opacity: clamp(Number.isFinite(Number(value.opacity)) ? Number(value.opacity) : DEFAULT_STYLE.opacity, 0, 1),
    boundaryVisible: value.boundaryVisible !== false,
    boundaryWidth: DEFAULT_STYLE.boundaryWidth,
    labelsVisible: value.labelsVisible !== false,
    blendMode: value.blendMode === 'multiply' ? 'multiply' : 'normal',
  };
}

export function normalizeLayerPresentation(value = {}) {
  const overlayOrder = [...OVERLAY_GROUPS];
  const sourceStyles = value?.styles && typeof value.styles === 'object' ? value.styles : {};
  const legacyHydroStyle = Object.hasOwn(sourceStyles, 'hydro') ? normalizeLayerStyle(sourceStyles.hydro) : null;
  const styles = {};
  for (const group of PRESENTATION_GROUPS) {
    if (group === 'rivers' && !Object.hasOwn(sourceStyles, group) && legacyHydroStyle) {
      styles[group] = normalizeLayerStyle({
        ...legacyHydroStyle,
        opacity: legacyHydroStyle.boundaryVisible ? legacyHydroStyle.opacity : 0,
        boundaryVisible: true,
      });
      continue;
    }
    if (group === 'lakes' && !Object.hasOwn(sourceStyles, group) && legacyHydroStyle) {
      styles[group] = normalizeLayerStyle(legacyHydroStyle);
      continue;
    }
    styles[group] = normalizeLayerStyle(sourceStyles[group]);
  }
  const objectStyles = {};
  for (const [key, style] of Object.entries(value.objectStyles || {})) {
    const normalized = normalizeLayerStyle(style);
    objectStyles[key] = Object.fromEntries(Object.keys(normalized)
      .filter(property => Object.hasOwn(style, property)).map(property => [property, normalized[property]]));
  }
  const objectOrder = [...new Set((value.objectOrder || []).map(String))];
  return { schemaVersion: LAYER_PRESENTATION_SCHEMA_VERSION, overlayOrder, styles, objectStyles, objectOrder };
}

export function moveOverlayGroup(presentation, group, direction) {
  void group;
  void direction;
  return normalizeLayerPresentation(presentation);
}

export const layerStyle = (presentation, group, objectKey = '') => normalizeLayerStyle(
  { ...presentation?.styles?.[group], ...(objectKey && presentation?.objectStyles?.[objectKey]) },
);
