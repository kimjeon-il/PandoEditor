const text = value => String(value ?? '').trim();

export function countryId(feature) {
  return text(feature?.id);
}

export function countryName(feature, override = {}) {
  return text(override?.name) || text(feature?.properties?.name) || countryId(feature) || '국가';
}

export function countryProperties(properties = {}) {
  const output = { name: text(properties.name) || '국가' };
  const validFrom = text(properties.validFrom);
  const validTo = text(properties.validTo);
  if (validFrom) output.validFrom = validFrom;
  if (validTo) output.validTo = validTo;
  return output;
}

export function normalizeCountryFeature(feature, { id = countryId(feature), name = '', clone = structuredClone } = {}) {
  const normalizedId = text(id);
  if (!normalizedId) throw new Error('국가 ID가 비어 있습니다.');
  const properties = countryProperties({ ...(feature?.properties || {}), ...(name ? { name } : {}) });
  return {
    type: 'Feature',
    id: normalizedId,
    properties,
    geometry: clone(feature?.geometry),
  };
}

export function pruneCountryOverrides(overrides = {}, validIds = null) {
  const allowed = new Set(['name', 'color', 'capital', 'notes', 'flagDataUrl', 'locked']);
  const output = {};
  for (const [rawId, rawOverride] of Object.entries(overrides || {})) {
    const id = text(rawId);
    if (!id || (validIds && !validIds.has(id)) || !rawOverride || typeof rawOverride !== 'object') continue;
    const override = {};
    for (const [key, value] of Object.entries(rawOverride)) {
      if (!allowed.has(key)) continue;
      if (key === 'flagDataUrl' && value === null) {
        override.flagDataUrl = null;
        continue;
      }
      if (key === 'locked') {
        if (value === true) override.locked = true;
        continue;
      }
      const normalized = text(value);
      if (normalized) override[key] = normalized;
    }
    if (Object.keys(override).length) output[id] = override;
  }
  return output;
}
