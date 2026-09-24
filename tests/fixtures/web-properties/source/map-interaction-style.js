const THEMES = new Set(['light', 'dark']);
const COLOR_PATTERN = /^#[0-9a-f]{6}$/i;

function color(value, fallback) {
  const normalized = String(value || '').trim();
  return COLOR_PATTERN.test(normalized) ? normalized.toLowerCase() : fallback;
}

function unitInterval(value, fallback) {
  const numeric = Number(value);
  return Number.isFinite(numeric) ? Math.max(0, Math.min(1, numeric)) : fallback;
}

export const INTERACTION_ROLE_PRIORITY = Object.freeze({
  'edit-target': 5, 'chosen-result': 5, primary: 4,
  secondary: 3, 'selected-provider': 3, 'selected-component': 3,
  hover: 2, reference: 1, candidate: 1, 'unchosen-result': 1,
});

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

export function interactionRoleStyle(style, role = 'candidate', { directManipulation = false } = {}) {
  const priority = INTERACTION_ROLE_PRIORITY[role] || 1;
  const selection = style.selection;
  if (priority === 1) return Object.freeze({ color: selection.color, width: 1, alpha: 0.45, fillAlpha: 0 });
  if (priority === 2) return style.hover;
  const source = priority >= 4 ? selection.primary : selection.secondary;
  return Object.freeze({ color: selection.color,
    width: directManipulation ? (priority >= 4 ? 2.5 : 1.5) : source.innerWidth,
    alpha: directManipulation ? (priority >= 4 ? 1 : 0.72) : source.innerAlpha,
    fillAlpha: source.fillAlpha, casingColor: selection.casingColor,
    outerWidth: source.outerWidth, casingAlpha: source.casingAlpha });
}

export function interactionCssProperties(style) {
  const properties = { '--map-selection-halo': style.selection.color, '--map-hover-stroke': style.hover.color };
  for (const role of ['primary', 'secondary', 'hover', 'candidate']) {
    const value = interactionRoleStyle(style, role);
    properties[`--map-${role}-fill-alpha`] = String(value.fillAlpha);
    properties[`--map-${role}-stroke-alpha`] = String(value.alpha);
    properties[`--map-${role}-stroke-width`] = `${value.width}px`;
  }
  properties['--map-hover-fill'] = `color-mix(in srgb, ${style.hover.color} ${style.hover.fillAlpha * 100}%, transparent)`;
  return Object.freeze(properties);
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
  const casingColor = color(tokens.textStrong, dark ? '#f2f4f6' : '#1c2229');
  const hoverFillAlpha = (dark ? 0.10 : 0.08) * resolvedFillStrength;
  return Object.freeze({
    theme: resolvedTheme,
    hover: Object.freeze({ color: resolvedSelectionColor, width: 1.5, alpha: 0.85, fillAlpha: hoverFillAlpha }),
    selection: Object.freeze({
      color: resolvedSelectionColor,
      casingColor,
      outlineVisible: resolvedOutlineVisible,
      fillStrength: resolvedFillStrength,
      primary: Object.freeze({
        innerWidth: resolvedOutlineVisible ? 2.5 : 0,
        innerAlpha: resolvedOutlineVisible ? 1 : 0,
        outerWidth: resolvedOutlineVisible ? 4 : 0,
        casingAlpha: resolvedOutlineVisible ? (dark ? 0.72 : 0.64) : 0,
        fillAlpha: fill.primary,
      }),
      secondary: Object.freeze({
        innerWidth: resolvedOutlineVisible ? 1.5 : 0,
        innerAlpha: resolvedOutlineVisible ? 0.72 : 0,
        outerWidth: resolvedOutlineVisible ? 2.8 : 0,
        casingAlpha: resolvedOutlineVisible ? (dark ? 0.48 : 0.42) : 0,
        fillAlpha: fill.secondary,
      }),
    }),
    drawOrder: Object.freeze(['secondary-casing', 'primary-casing', 'candidate', 'hover', 'secondary-inner', 'primary-inner']),
  });
}
