export const LAYER_PRESENTATION_SCHEMA_VERSION = 4;

// Presentation policy, not project state. GPU fills claim pixels front-to-back;
// Canvas paints them back-to-front. Both implement the same territorial owner.
const VISUAL_ROLES = Object.freeze([
  { role: 'substrate', scope: 'base' },
  { role: 'terrain', scope: 'base' },
  { role: 'country-fill', scope: 'base', gpuRank: 3 },
  { role: 'territorial-fill', scope: 'base', gpuRank: 2 },
  { role: 'lake', scope: 'base', hydro: true },
  { role: 'lake-boundary', scope: 'base', hydro: true },
  { role: 'river', scope: 'base', hydro: true },
  { role: 'border-river', scope: 'base', hydro: true },
  { role: 'country-boundary', scope: 'base' },
  // Mixed overlays retain group/object order and protect base water/borders.
  { role: 'independent-overlay', scope: 'base' },
  { role: 'candidate', scope: 'selection', priority: 1, aliases: ['reference', 'unchosen-result'] },
  { role: 'hover', scope: 'selection', priority: 2 },
  { role: 'secondary', scope: 'selection', priority: 3, aliases: ['selected-provider', 'selected-component'] },
  { role: 'primary', scope: 'selection', priority: 4 },
  { role: 'edit-preview', scope: 'interaction', priority: 5, aliases: ['edit-target', 'chosen-result'] },
  { role: 'validation', scope: 'interaction' },
  { role: 'edit-handles', scope: 'interaction' },
  { role: 'draft', scope: 'interaction' },
  { role: 'snap', scope: 'interaction' },
  { role: 'territorial-labels', scope: 'interaction' },
  { role: 'labels', scope: 'interaction' },
].map((entry, rank) => Object.freeze({ ...entry, rank, aliases: Object.freeze(entry.aliases || []) })));

export function mapVisualOrder(scope, backend = 'canvas') {
  return Object.freeze(VISUAL_ROLES.filter(entry => scope === 'all' || entry.scope === scope || (scope === 'hydro' && entry.hydro))
    .sort((a, b) => (backend === 'gpu' ? a.gpuRank ?? a.rank : a.rank) - (backend === 'gpu' ? b.gpuRank ?? b.rank : b.rank))
    .map(entry => entry.role));
}

export function mapVisualRank(role) {
  const entry = VISUAL_ROLES.find(entry => entry.role === role);
  if (!entry) throw new Error(`Unknown visual role: ${role}`);
  return entry.rank;
}

export const INTERACTION_ROLE_PRIORITY = Object.freeze(Object.fromEntries(VISUAL_ROLES.filter(entry => entry.priority)
  .flatMap(entry => [entry.role, ...entry.aliases].map(role => [role, entry.priority]))));
export const SELECTION_PAINT_ORDER = mapVisualOrder('selection');

const OVERLAY_DETAIL_ORDER = Object.freeze({ fill: 10, line: 15, boundary: 20, 'territorial-boundary': 30 });
export function overlayVisualOrder(presentation, group, detail = 'fill', objectKey = '', fragment = 0) {
  if (!Object.hasOwn(OVERLAY_DETAIL_ORDER, detail)) throw new Error(`Unknown overlay visual role: ${detail}`);
  const order = presentation?.overlayOrder || OVERLAY_GROUPS;
  const index = order.indexOf(group);
  return (index < 0 ? order.length : index) * 1000 + OVERLAY_DETAIL_ORDER[detail]
    + layerObjectRank(presentation, objectKey) + fragment;
}

export const TERRITORIAL_SYMBOL_KEYS = Object.freeze({
  countries: Object.freeze({ name: 'basemapLabels', flag: 'countryFlags' }),
  subunits: Object.freeze({ name: 'subunitLabels', flag: 'subunitFlags' }),
  regions: Object.freeze({ name: 'regionLabels', flag: 'regionFlags' }),
});

export function territorialSymbolGroup(feature) {
  return (feature?.properties?.entityKind === 'general' && !!feature?.properties?.parentId) ? 'subunits'
    : (feature?.properties?.entityKind === 'regional') ? 'regions' : 'countries';
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
  'distributions',
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
  colorVisible: true,
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
    colorVisible: value.colorVisible !== false,
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

// Keep the base-country palette and territorial replacement fills on one
// display rule. Territorial paint supplies an empty fallback to leave the
// map-mode substrate visible; other domains may supply their own default.
export function resolveLayerDisplayColor(presentation, group, {
  objectKey = '', explicitColor, inheritedColor, fallbackColor,
} = {}) {
  const style = layerStyle(presentation, group, objectKey);
  if (style.colorVisible === false) return fallbackColor;
  return explicitColor || inheritedColor || fallbackColor;
}
