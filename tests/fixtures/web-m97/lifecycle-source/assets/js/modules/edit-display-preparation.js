import { territorialRootId } from './territorial-units.js';
import { buildTerritorialInternalBoundarySegments } from './boundary-topology.js';
import { excludeAncestorHighlightBoundary } from './territorial-highlight-boundary.js';

export function createEditDisplayPreparation() {
  const groups = new Map();
  const tokens = new WeakMap();
  let sequence = 0;
  const token = geometry => {
    if (!geometry) return 0;
    if (!tokens.has(geometry)) tokens.set(geometry, ++sequence);
    return tokens.get(geometry);
  };
  const signature = features => features.map(feature => [feature.id, token(feature.geometry), feature.properties?.entityKind,
    feature.properties?.parentId, undefined].join(':')).sort().join('|');
  return {
    async prepare(request, countries, units, checkpoint = async () => {}, sourceByKey = () => null) {
      if (request.kind === 'highlight') {
        const feature = request.featureKey ? sourceByKey(request.featureKey) : request.feature;
        const owners = request.occluders ? request.occluders.map(owner => owner.feature || sourceByKey(owner.key)) : request.occluderKeys ? request.occluderKeys.map(sourceByKey)
          : request.occluderFeatures || (request.ancestorIds || []).map(id =>
            countries.find(feature => String(feature.id) === id) || units.find(feature => String(feature.id) === id));
        if (!feature || owners.some(owner => !owner)) throw new Error('강조 원본을 다시 준비하세요.');
        await checkpoint();
        return { feature: excludeAncestorHighlightBoundary(feature, owners) };
      }
      if (request.entities) {
        countries = request.entities.filter(entity => entity.properties.entityKind === 'general' && !entity.properties.parentId);
        units = request.entities.filter(entity => entity.properties.entityKind === 'regional' || !!entity.properties.parentId);
      }
      const countryById = new Map(countries.map(feature => [String(feature.id), feature]));
      const grouped = new Map();
      const entities = new Map([...countries, ...units].map(feature => [String(feature.id), feature]));
      const hasIndependentRegions = units.some(unit => !(unit.properties?.entityKind === 'general' && !!unit.properties?.parentId));
      for (const unit of units) {
        // Independent regions can cross countries; preserve that existing rule.
        const key = hasIndependentRegions ? '*' : territorialRootId(unit, id => entities.get(id));
        if (!grouped.has(key)) grouped.set(key, []);
        grouped.get(key).push(unit);
      }
      const segments = [];
      for (const [key, members] of grouped) {
        await checkpoint();
        const parents = key === '*' ? countries : [countryById.get(key)].filter(Boolean);
        const input = signature([...parents, ...members]);
        let entry = groups.get(key);
        if (entry?.input !== input) {
          entry = { input, segments: buildTerritorialInternalBoundarySegments(parents, members) };
          groups.set(key, entry);
        }
        segments.push(...entry.segments);
      }
      for (const key of groups.keys()) if (!grouped.has(key)) groups.delete(key);
      return { segments };
    },
  };
}
