import { COUNTRY_DEFAULT_COLORS } from './country-default-colors.js';

export const COLOR_DOMAINS = Object.freeze({
  TERRITORIAL: 'territorial',
  GENERIC: 'generic',
  DISTRIBUTION: 'distribution',
});

const HEX_COLOR = /^#[0-9a-f]{6}$/i;

export function normalizeColorValue(value, fallback = '#8c68d8') {
  const candidate = String(value || '').trim();
  if (HEX_COLOR.test(candidate)) return candidate.toLowerCase();
  const safeFallback = String(fallback || '').trim();
  return HEX_COLOR.test(safeFallback) ? safeFallback.toLowerCase() : '#8c68d8';
}

function explicitColor(domain, target) {
  if (domain === COLOR_DOMAINS.TERRITORIAL) return target?.feature?.properties?.style?.color || '';
  if (domain === COLOR_DOMAINS.GENERIC) return target?.feature?.properties?.color || '';
  if (domain === COLOR_DOMAINS.DISTRIBUTION) return target?.layer?.color || '';
  return '';
}

export function readDomainColor(domain, target = {}, { fallback = '#8c68d8', inherited = '' } = {}) {
  const rawExplicit = String(explicitColor(domain, target) || '').trim();
  const explicit = HEX_COLOR.test(rawExplicit) ? rawExplicit.toLowerCase() : '';
  const base = inherited || fallback;
  return {
    explicit,
    value: normalizeColorValue(explicit || base, fallback),
    isDefault: !explicit,
  };
}

export function writeDomainColor(domain, target = {}, value, { clear = false, fallback = '#8c68d8' } = {}) {
  const color = clear ? '' : normalizeColorValue(value, fallback);
  if (domain === COLOR_DOMAINS.TERRITORIAL && target.feature?.properties) {
    target.feature.properties.style = { ...(target.feature.properties.style || {}) };
    if (color) target.feature.properties.style.color = color;
    else delete target.feature.properties.style.color;
  } else if (domain === COLOR_DOMAINS.GENERIC && target.feature?.properties) {
    if (color) target.feature.properties.color = color;
    else delete target.feature.properties.color;
  } else if (domain === COLOR_DOMAINS.DISTRIBUTION && target.layer) {
    if (color) target.layer.color = color;
    else delete target.layer.color;
  }
  return color;
}
/** Stable source identity, never an edited display name or a saved color override. */
export function countryDefaultColor(feature) {
  if (feature?.properties?.entityKind !== 'general' || feature.properties.parentId) return '';
  const libraryId = feature.properties.sourceLibraryId;
  // Current-country library instances use a library ID, not the canonical country ID.
  const id = String(libraryId?.startsWith('current-country:')
    ? feature.properties.metadata?.currentCountryId || '' : libraryId || feature.id || '');
  return Object.hasOwn(COUNTRY_DEFAULT_COLORS, id) ? COUNTRY_DEFAULT_COLORS[id] || '' : '';
}

/** Intrinsic territorial color inheritance; layer opacity/blending stays in rendering. */
export function resolveTerritorialColor(feature, { entityRepository, countryColor = countryDefaultColor, fallback = '', colorVisible = () => true }) {
  let current = feature;
  const seen = new Set();
  while (current) {
    const id = String(current.id);
    if (seen.has(id)) throw new Error(`영역 색상 상위 관계가 순환합니다: ${id}`);
    seen.add(id);
    if (!colorVisible(current)) return fallback;
    if (current.properties?.style?.color) return current.properties.style.color;
    if (current.properties.entityKind === 'regional') return fallback;
    if (!current.properties.parentId) return countryColor(current);
    current = entityRepository.parent(id);
    if (!current) throw new Error(`색상을 상속할 상위 객체가 없습니다: ${id}`);
  }
  return fallback;
}
