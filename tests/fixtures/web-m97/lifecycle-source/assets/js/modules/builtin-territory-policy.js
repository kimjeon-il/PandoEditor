// Default-map display policy only, not a determination of legal sovereignty.
// Spatial records refer to the unchanged canonical source feature IDs.
export const BUILTIN_TERRITORY_MERGES = Object.freeze([
  Object.freeze({ sourceId: 'BRI', controller: 'BRA', claimants: ['BRA', 'URY'], disputed: true }),
  Object.freeze({ sourceId: 'BJN', controller: 'COL', historicalClaimants: ['COL', 'USA', 'JAM', 'NIC'], disputed: true, adjudicatedAgainst: 'NIC', judgmentYear: 2012 }),
  Object.freeze({ sourceId: 'SER', controller: 'COL', historicalClaimants: ['COL', 'USA', 'HND', 'NIC'], disputed: true, adjudicatedAgainst: 'NIC', judgmentYear: 2012 }),
  Object.freeze({ sourceId: 'SCR', controller: 'CHN', claimants: ['CHN', 'TWN', 'PHL'], disputed: true }),
]);

const polygons = geometry => geometry.type === 'Polygon' ? [geometry.coordinates] : geometry.coordinates;

/** These four source islands are disjoint from their destination's existing land. */
export function mergeBuiltinTerritories(collection) {
  const byId = new Map(collection.features.map(feature => [String(feature.id), feature]));
  const removed = new Set();
  for (const policy of BUILTIN_TERRITORY_MERGES) {
    const source = byId.get(policy.sourceId);
    if (!source) continue; // Idempotent and compatible with preview omission of tiny islands.
    const destination = byId.get(policy.controller);
    if (!destination) throw new Error(`기본 영토 대상 누락: ${policy.sourceId} → ${policy.controller}`);
    byId.set(policy.controller, { ...destination, geometry: {
      type: 'MultiPolygon', coordinates: [...polygons(destination.geometry), ...polygons(source.geometry)],
    } });
    removed.add(policy.sourceId);
  }
  return { ...collection, features: collection.features.filter(feature => !removed.has(String(feature.id)))
    .map(feature => byId.get(String(feature.id))) };
}
