import { layerStyle } from './layer-presentation.js';

// Resolve only presentation values. Never persist inherited defaults on a child.
export function createTerritorialFillResolver({ state, countryColor, defaultColor, terrainAlpha = 1 }) {
  const units = new Map((state.territorialUnits || []).map(unit => [String(unit.id), unit]));
  const countries = new Map((state.countriesData?.features || []).map(country => [String(country.id), country]));
  const cache = new Map();
  const presentation = state.layerPresentation;
  const countryStyle = layerStyle(presentation, 'countries');
  function resolve(unit, visiting = new Set()) {
    const id = String(unit.id);
    if (cache.has(id)) return cache.get(id);
    const properties = unit.properties || {};
    const country = countries.get(String(properties.sovereignId || properties.parentId || ''));
    let inherited = { color: country ? countryColor(country) : defaultColor,
      opacity: countryStyle.opacity, blendMode: countryStyle.blendMode, depth: 0 };
    const parent = units.get(String(properties.parentId || ''));
    if (parent?.properties?.unitType === 'subunit' && !visiting.has(id)) {
      visiting.add(id);
      if (!visiting.has(String(parent.id))) inherited = resolve(parent, visiting);
      visiting.delete(id);
    }
    const group = properties.unitType === 'region' ? 'regions' : 'subunits';
    const groupStyle = presentation?.styles?.[group] || {};
    const explicit = presentation?.objectStyles?.[`territorial:${properties.unitType}:${id}`] || {};
    // Legacy group records contain materialized defaults. Neutral group values
    // do not turn an otherwise inherited child into an opaque overlay.
    const opacity = explicit.opacity ?? (groupStyle.opacity !== undefined && groupStyle.opacity !== 1
      ? groupStyle.opacity : inherited.opacity);
    const blendMode = explicit.blendMode ?? (groupStyle.blendMode === 'multiply' ? 'multiply' : inherited.blendMode);
    const result = Object.freeze({ color: properties.style?.color || inherited.color,
      opacity: Math.max(0, Math.min(1, Number(opacity))), blendMode,
      fillAlpha: Math.max(0, Math.min(1, Number(opacity))) * terrainAlpha,
      depth: inherited.depth + 1, ownerId: String(properties.sovereignId || ''), parentId: String(properties.parentId || '') });
    cache.set(id, result);
    return result;
  }
  return resolve;
}
