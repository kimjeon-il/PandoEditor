import { INTERACTION_ROLE_PRIORITY, SELECTION_PAINT_ORDER } from './layer-presentation.js';
import { isMapVisualFrame } from './map-visual-frame.js';
const THEMES = new Set(['light', 'dark']);
const COLOR_PATTERN = /^#[0-9a-f]{6}$/i;
const PI = Math.PI;

function color(value, fallback) {
  const normalized = String(value || '').trim();
  return COLOR_PATTERN.test(normalized) ? normalized.toLowerCase() : fallback;
}

function unitInterval(value, fallback) {
  const numeric = Number(value);
  return Number.isFinite(numeric) ? Math.max(0, Math.min(1, numeric)) : fallback;
}

/** Keep interactive casings from merging nearby islands while zoomed out. */
export function interactionStrokeScale(frameContext = null) {
  const width = Number(frameContext?.size?.width);
  const height = Number(frameContext?.size?.height);
  const scale = Number(isMapVisualFrame(frameContext) ? frameContext.cssScale : frameContext?.scale);
  if (!(width > 0) || !(height > 0) || !(scale > 0)) return 1;
  const safe = frameContext?.safeInset || {};
  const contentWidth = Math.max(1, width - Number(safe.left || 0) - Number(safe.right || 0));
  const contentHeight = Math.max(1, height - Number(safe.top || 0) - Number(safe.bottom || 0));
  const baseScale = frameContext?.projection === 'flat'
    ? Math.max(30, contentWidth / (2 * PI))
    : Math.max(60, Math.min(contentWidth, contentHeight) * 0.455);
  const zoom = scale / baseScale;
  return Math.max(0.45, Math.min(1, 0.4 + ((zoom - 0.72) / 0.28) * 0.6));
}

export function scaleInteractionStroke(style, frameContext = null) {
  if (!style?.scaleWithView) return style || {};
  const factor = interactionStrokeScale(frameContext);
  const casing = style.casing && typeof style.casing === 'object'
    ? Object.freeze({ ...style.casing, width: Number(style.casing.width || 0) * factor })
    : style.casing;
  return Object.freeze({ ...style, width: Number(style.width || 0) * factor,
    innerCutout: Number(style.innerCutout || 0) * factor, casing, interactionScale: factor });
}

/** Collapse roles without copying geometry or changing the selection model. */
export function resolveInteractionEntries(entries = []) {
  const byKey = new Map();
  for (const entry of entries) {
    const key = String(entry?.key || entry?.ref?.key || '');
    if (!key) continue;
    const previous = byKey.get(key);
    const roles = [...new Set([...(previous?.roles || []), ...(entry.roles || [entry.role || 'candidate'])])]
      .filter(role => Object.hasOwn(INTERACTION_ROLE_PRIORITY, role))
      .sort((a, b) => INTERACTION_ROLE_PRIORITY[b] - INTERACTION_ROLE_PRIORITY[a] || a.localeCompare(b));
    const role = roles[0] || 'candidate';
    byKey.set(key, Object.freeze({ ...previous, ...entry, key, roles: Object.freeze(roles), role,
      priority: INTERACTION_ROLE_PRIORITY[role] }));
  }
  const remaining = [...byKey.values()];
  const ordered = [];
  while (remaining.length) {
    const priority = Math.max(...remaining.map(entry => entry.priority));
    const peers = remaining.filter(entry => entry.priority === priority);
    const keys = new Set(peers.map(entry => entry.key));
    const eligible = peers.filter(entry => !(entry.ancestorKeys || []).some(key => keys.has(key)));
    // A malformed hierarchy cannot make presentation hang.
    const next = (eligible.length ? eligible : peers).sort((a, b) => a.key < b.key ? -1 : a.key > b.key ? 1 : 0)[0];
    ordered.push(next); remaining.splice(remaining.indexOf(next), 1);
  }
  return Object.freeze(ordered);
}

const STROKE_SHAPE = Object.freeze({ cap: 'round', join: 'round', dash: Object.freeze([0, 0]), casing: null,
  blendMode: 'normal', scaleWithView: false, antiAlias: true });
const BASE_STROKES = Object.freeze({
  country: Object.freeze({ width: 0.72, cap: 'butt' }),
  'country-internal': Object.freeze({ width: 1.1, cap: 'butt', join: 'miter', dash: Object.freeze([3, 2]) }),
  subunit: Object.freeze({ width: 2 }),
  'subunit-internal': Object.freeze({ width: 1.1, dash: Object.freeze([3, 2]) }),
  region: Object.freeze({ width: 1.5, dash: Object.freeze([7, 3]) }),
  generic: Object.freeze({ width: 1 }),
  distribution: Object.freeze({ width: 1 }),
  river: Object.freeze({ width: 1 }),
  'hydro-boundary': Object.freeze({ width: 1 }),
});

// All widths and dash lengths returned here are CSS pixels. The current layer
// model still fixes its width multiplier to one; this adds no width preference.
export function resolveMapStrokeStyle(style, role = 'candidate', { directManipulation = false, boundaryKind = '' } = {}) {
  if (Object.hasOwn(BASE_STROKES, role)) {
    const theme = style.theme || {};
    const layer = style.layerStyle || {};
    const country = role === 'country' || role === 'country-internal';
    const lake = role === 'hydro-boundary', river = role === 'river';
    const width = lake ? theme.lakeBoundaryWidth : river ? theme.riverWidth : country ? theme.borderWidth : layer.boundaryWidth;
    const alpha = lake ? theme.lakeOpacity : river ? theme.riverOpacity : country ? theme.borderAlpha : layer.opacity;
    return Object.freeze({ ...STROKE_SHAPE, ...BASE_STROKES[role], color: style.color || theme.border,
      width: BASE_STROKES[role].width * Math.max(0.5, Number(width) || 1),
      alpha: layer.boundaryVisible === false || (lake && theme.lakeBoundaryVisible === false) ? 0 : Number(alpha ?? 1),
      blendMode: style.blendMode || layer.blendMode || 'normal', antiAlias: style.antiAlias !== false });
  }
  if (!Object.hasOwn(INTERACTION_ROLE_PRIORITY, role)) throw new Error(`Unknown stroke role: ${role}`);
  const priority = INTERACTION_ROLE_PRIORITY[role];
  const selection = style.selection;
  const antiAlias = style.antiAlias !== false;
  if (priority === 1) return Object.freeze({ ...STROKE_SHAPE, color: selection.color, width: 1, alpha: 0.45, fillAlpha: 0, scaleWithView: true, antiAlias });
  if (priority === 2) return Object.freeze({ ...STROKE_SHAPE, ...style.hover, scaleWithView: true, antiAlias });
  const source = priority >= 4 ? selection.primary : selection.secondary;
  return Object.freeze({ ...STROKE_SHAPE, color: selection.color,
    width: directManipulation ? (priority >= 4 ? 2.5 : 1.5) : source.innerWidth,
    alpha: directManipulation ? (priority >= 4 ? 1 : 0.72) : source.innerAlpha,
    fillAlpha: source.fillAlpha,
    dash: boundaryKind === 'shared' ? Object.freeze([6, 3]) : STROKE_SHAPE.dash,
    scaleWithView: true,
    antiAlias });
}

/** Convert once at the device boundary; never persist or cache this as policy. */
export function gpuStrokeStyle(style, frameContext) {
  const dpr = Math.max(1, Number(frameContext.dpr || 1));
  return Object.freeze({ ...style, width: Number(style.width || 0) * dpr,
    innerCutout: Number(style.innerCutout || 0) * dpr,
    casing: style.casing ? Object.freeze({ ...style.casing, width: Number(style.casing.width || 0) * dpr }) : null,
    dash: Object.freeze((style.dash || STROKE_SHAPE.dash).map(value => Number(value) * dpr)) });
}

export function interactionCssProperties(style) {
  const properties = { '--map-selection-halo': style.selection.color, '--map-hover-stroke': style.hover.color };
  for (const role of ['primary', 'secondary', 'hover', 'candidate']) {
    const value = resolveMapStrokeStyle(style, role);
    properties[`--map-${role}-fill-alpha`] = String(value.fillAlpha);
    properties[`--map-${role}-stroke-alpha`] = String(value.alpha);
    properties[`--map-${role}-stroke-width`] = `${value.width}px`;
  }
  properties['--map-hover-fill'] = `color-mix(in srgb, ${style.hover.color} ${style.hover.fillAlpha * 100}%, transparent)`;
  return Object.freeze(properties);
}

/** The document root owns interaction tokens for both the map and its SVG overlays. */
export function applyInteractionCssProperties(style, root = globalThis.document?.documentElement) {
  if (!root?.style) return false;
  for (const [property, value] of Object.entries(interactionCssProperties(style))) {
    root.style.setProperty(property, value);
  }
  return true;
}

/** Existing packet roles win; class names are only adapters for older tool views. */
export function interactionNodeRole(node, domain = 'draft') {
  const explicit = node.getAttribute?.('data-interaction-role');
  if (Object.hasOwn(INTERACTION_ROLE_PRIORITY, explicit)) return explicit;
  const has = name => node.classList?.contains(name);
  if (has('geometry-preview-old-boundary') || has('geometry-preview-remove')) return 'reference';
  if (has('selected-candidate')) return 'chosen-result';
  if (has('selected-component')) return 'selected-component';
  if (has('hovered-component')) return 'hover';
  if (has('territory-candidate') || has('draft-split-preview')) return 'unchosen-result';
  if (has('territory-component')) return 'candidate';
  return domain === 'preview' ? 'chosen-result' : 'edit-target';
}

export function resolveMapInteractionStyle({
  theme = 'dark',
  selectionColor = null,
  outlineVisible = true,
  fillStrength = 0.35,
  antiAlias = true,
  tokens = {},
} = {}) {
  const resolvedTheme = THEMES.has(theme) ? theme : 'dark';
  const dark = resolvedTheme === 'dark';
  const resolvedFillStrength = unitInterval(fillStrength, 0.35);
  const resolvedOutlineVisible = outlineVisible !== false;
  const maxFill = dark ? { primary: 0.30, secondary: 0.18 } : { primary: 0.24, secondary: 0.14 };
  const fill = {
    primary: maxFill.primary * resolvedFillStrength,
    secondary: maxFill.secondary * resolvedFillStrength,
  };
  const themeAccentFallback = dark ? '#cda95d' : '#315e9d';
  const resolvedSelectionColor = color(selectionColor, color(tokens.accent, themeAccentFallback));
  const hoverFillAlpha = (dark ? 0.10 : 0.08) * resolvedFillStrength;
  return Object.freeze({
    theme: resolvedTheme,
    antiAlias: antiAlias !== false,
    hover: Object.freeze({ color: resolvedSelectionColor, width: 1.5, alpha: 0.85, fillAlpha: hoverFillAlpha }),
    selection: Object.freeze({
      color: resolvedSelectionColor,
      outlineVisible: resolvedOutlineVisible,
      fillStrength: resolvedFillStrength,
      primary: Object.freeze({
        innerWidth: resolvedOutlineVisible ? 2.5 : 0,
        innerAlpha: resolvedOutlineVisible ? 1 : 0,
        fillAlpha: fill.primary,
      }),
      secondary: Object.freeze({
        innerWidth: resolvedOutlineVisible ? 1.5 : 0,
        innerAlpha: resolvedOutlineVisible ? 0.72 : 0,
        fillAlpha: fill.secondary,
      }),
    }),
    drawOrder: Object.freeze(SELECTION_PAINT_ORDER.map(role => ['primary', 'secondary'].includes(role) ? `${role}-inner` : role)),
  });
}
