export const COLOR_DOMAINS = Object.freeze({
  COUNTRY: 'country',
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
  if (domain === COLOR_DOMAINS.COUNTRY) return target?.override?.color || '';
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
  if (domain === COLOR_DOMAINS.COUNTRY) {
    if (target.override) {
      if (color) target.override.color = color;
      else delete target.override.color;
    }
  } else if (domain === COLOR_DOMAINS.TERRITORIAL && target.feature?.properties) {
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
