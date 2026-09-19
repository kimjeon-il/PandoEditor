const polygons = geometry => geometry?.type === 'Polygon' ? [geometry.coordinates]
  : geometry?.type === 'MultiPolygon' ? geometry.coordinates : [];
const featureFor = coordinates => coordinates?.length ? { type: 'Feature', properties: {}, geometry: { type: 'MultiPolygon', coordinates } } : null;

/** Read model only. Its geometry never replaces countriesData or a subunit. */
export function createTerritorialScopeResolver({ read, countryById, countryColor, clipper }) {
  let signature = null, sourceUnits = null;
  let byId = new Map(), children = new Map(), scopes = new Map();
  function refresh() {
    const snapshot = read();
    if (signature === snapshot.revision && sourceUnits === snapshot.units) return;
    signature = snapshot.revision;
    sourceUnits = snapshot.units;
    byId = new Map((snapshot.units || []).map(unit => [String(unit.id), unit]));
    children = new Map();
    scopes = new Map();
    for (const unit of snapshot.units || []) {
      if (unit.properties?.unitType !== 'subunit') continue;
      const storedParent = String(unit.properties.parentId || '');
      const parentId = byId.get(storedParent)?.properties?.unitType === 'region'
        ? String(unit.properties.sovereignId || '') : storedParent || String(unit.properties.sovereignId || '');
      const siblings = children.get(parentId) || [];
      siblings.push(unit);
      children.set(parentId, siblings);
    }
  }
  function members(countryId) {
    refresh();
    const result = [], seen = new Set([String(countryId)]), pending = [String(countryId)];
    while (pending.length) {
      for (const unit of children.get(pending.pop()) || []) {
        const id = String(unit.id);
        if (seen.has(id)) continue;
        seen.add(id); result.push(unit); pending.push(id);
      }
    }
    return result;
  }
  function scope(countryId) {
    refresh();
    const id = String(countryId);
    if (scopes.has(id)) return scopes.get(id);
    const country = countryById(id);
    const descendants = members(id);
    const base = polygons(country?.geometry);
    let extent = country, extra = null;
    if (descendants.length && base.length) {
      const engine = clipper();
      if (!engine?.union || !engine?.difference) return { country, members: descendants, extent, extra };
      const combined = engine.union(base, ...descendants.map(unit => polygons(unit.geometry)).filter(value => value.length));
      extent = featureFor(combined) || country;
      extra = featureFor(engine.difference(combined, base));
    }
    const result = { country, members: descendants, extent, extra };
    scopes.set(id, result);
    return result;
  }
  function color(unit, fallback = '') {
    refresh();
    const seen = new Set();
    let current = unit;
    while (current && !seen.has(String(current.id))) {
      seen.add(String(current.id));
      if (current.properties?.style?.color) return current.properties.style.color;
      const parentId = String(current.properties?.parentId || '');
      const parent = byId.get(parentId);
      if (parent?.properties?.unitType === 'subunit') { current = parent; continue; }
      const country = countryById(parentId) || countryById(current.properties?.sovereignId);
      return country ? countryColor(country) : fallback;
    }
    return fallback;
  }
  return Object.freeze({ members, scope, color });
}

export function validateSubunitParentChanges(previous, next, countryExists) {
  const old = new Map((previous || []).map(unit => [String(unit.id), unit]));
  const units = new Map((next || []).map(unit => [String(unit.id), unit]));
  const issues = [];
  const parentRelationSignature = (parentId, source) => {
    const id = String(parentId || '');
    if (countryExists(id)) return `country:${id}`;
    const parent = source.get(id);
    if (!parent) return 'missing';
    return `unit:${String(parent.properties?.unitType || '')}:${String(parent.properties?.sovereignId || '')}`;
  };
  for (const unit of next || []) {
    if (unit.properties?.unitType !== 'subunit') continue;
    const before = old.get(String(unit.id));
    const parentId = String(unit.properties.parentId || '');
    const sovereignId = String(unit.properties.sovereignId || '');
    let cursor = parentId;
    const seen = new Set([String(unit.id)]);
    while (cursor && units.has(cursor)) {
      if (seen.has(cursor)) { issues.push(`${unit.id}: 하위단위 소속 관계가 순환합니다.`); break; }
      seen.add(cursor);
      cursor = String(units.get(cursor).properties?.parentId || '');
    }
    const beforeParentId = String(before?.properties?.parentId || '');
    const relationChanged = before?.properties?.unitType !== 'subunit'
      || beforeParentId !== parentId
      || String(before.properties?.sovereignId || '') !== sovereignId
      || parentRelationSignature(beforeParentId, old) !== parentRelationSignature(parentId, units);
    if (!relationChanged) continue;
    const parent = units.get(parentId);
    if (!countryExists(sovereignId) || (parentId !== sovereignId
      && String(parent?.properties?.sovereignId || '') !== sovereignId)) {
      issues.push(unit.id + ': 상위 단위와 소속 국가가 일치해야 합니다.');
    }
    if (!parentId || (!countryExists(parentId) && units.get(parentId)?.properties?.unitType !== 'subunit')) {
      issues.push(`${unit.id}: 하위단위의 상위 단위는 국가 또는 하위단위여야 합니다.`);
    }
  }
  return { ok: !issues.length, issues };
}
