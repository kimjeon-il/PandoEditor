import { freezeEditingGeometry } from './editing-render-packet.js';
/** TerritoryComponents: extracted application responsibility.
 * Dependencies are explicitly wired once by the composition modules.
 * Mutable bindings stay local; exported accessors retain live identity.
 */
export function createTerritoryComponents() {
  let dependencies;

  function connect(ports) {
    if (dependencies) throw new Error('territory-components already connected');
    dependencies = ports;
  }

  function geometryMultiCoordinates(geometry) {
    if (!geometry) return [];
    if (geometry.type === 'Polygon') return [geometry.coordinates || []];
    if (geometry.type === 'MultiPolygon') return geometry.coordinates || [];
    return [];
  }

  function multiPolygonPlanarArea(multiPolygon) {
    return (multiPolygon || []).reduce((total, polygon) => {
      if (!polygon?.length) return total;
      const exterior = Math.abs((0, dependencies.geometryModel.ringSignedArea)((0, dependencies.geometryModel.ensureClosedRing)(polygon[0])));
      const holes = polygon.slice(1).reduce((sum, ring) => sum + Math.abs((0, dependencies.geometryModel.ringSignedArea)((0, dependencies.geometryModel.ensureClosedRing)(ring))), 0);
      return total + Math.max(0, exterior - holes);
    }, 0);
  }

  function territoryComponentContext() {
    const session = dependencies.projectState.state.territorySelectionSession;
    if (session?.stage === 'selection' && session.activePhase === 'components') {
      return { selectedKeys: session.selectedComponentKeys, features: session.componentFeatures || [] };
    }
    return { selectedKeys: [], features: [] };
  }

  function installComponentIndex(session, result, key) {
    const names = new Map((session.baseSourceFeatures || []).map(feature => [String(feature.id),
      (0, dependencies.objectPresentation.territorialEntityName)(feature) || feature.properties?.name || '기준 영역']));
    const items = result.items.map(item => (__riverOwnDataMerge(__riverOwnDataMerge({}, item), {geometry: freezeEditingGeometry(item.geometry), countryName: names.get(item.countryId) || '기준 영역', areaKm2: Math.max(0, dependencies.platform.d3.geo.area(item.geometry) * 6371.0088 ** 2)})));
    session.componentIndex = { key, items, byKey: new Map(items.map(item => [item.key, item])), river: null };
    return session.componentIndex;
  }

  function territoryBaseComponentItems() {
    return dependencies.projectState.state.territorySelectionSession?.componentIndex?.items || [];
  }

  function riverBoundaryComposition(baseItems = territoryBaseComponentItems(), { candidates, donorResults } = {}) {
    return (0, dependencies.territorialModel.composeRiverBoundaryTerritoryComponents)({
      components: baseItems,
      candidates: candidates || dependencies.projectState.state.territorySelectionSession?.riverPartitionCandidates || [],
      donorResults: donorResults || dependencies.projectState.state.territorySelectionSession?.riverPartitionDonorResults || [],
    });
  }

  function territoryComponentItems() {
    const context = territoryComponentContext();
    if (!context.features.length) return [];
    const selected = new Set(context.selectedKeys);
    const baseItems = territoryBaseComponentItems(context);
    let items = baseItems;
    const territorialSession = dependencies.projectState.state.territorySelectionSession;
    const usesRiver = territorialSession?.stage === 'selection'
      && territorialSession.activePhase === 'components'
      && territorialSession.useRiverBoundaries;
    if (usesRiver) {
      if (territorialSession.riverPartitionStatus !== 'ready') return [];
      const index = territorialSession.componentIndex;
      if (!index) return [];
      const candidates = territorialSession.riverPartitionCandidates;
      const donorResults = territorialSession.riverPartitionDonorResults;
      if (index.river?.candidates !== candidates || index.river?.donorResults !== donorResults) return [];
      items = index.river.items;
    }
    return items.map(item => (__riverOwnDataMerge(__riverOwnDataMerge({}, item), {selected: selected.has(item.key)})));
  }

  function installRiverComponentIndex(session, composition, candidates, donorResults) {
    const index = session.componentIndex;
    if (!index) return;
    const baseByComponent = new Map(index.items.map(item => [item.componentKey, item]));
    const items = composition.items.map(item => (__riverOwnDataMerge(__riverOwnDataMerge({}, item), {key: String(item.key), geometry: freezeEditingGeometry(item.geometry), countryName: baseByComponent.get(item.componentKey)?.countryName || '기준 영역', areaKm2: Number.isFinite(item.areaKm2) ? item.areaKm2 : Number.isFinite(item.areaM2)
        ? item.areaM2 / 1e6 : dependencies.platform.d3.geo.area(item.geometry) * 6371.0088 ** 2, usesRiverBoundary: item.partitionKind === 'river', riverBoundarySegments: item.riverBoundarySegments || []})));
    index.river = { candidates, donorResults, items, byKey: new Map(items.map(item => [item.key, item])) };
  }

  function selectedTerritoryComponentGeometry() {
    return dependencies.projectState.state.territorySelectionSession?.currentGeometry || null;
  }

  function formatTerritoryArea(areaKm2) {
    const area = Math.max(0, Number(areaKm2) || 0);
    const maximumFractionDigits = area < 10 ? 2 : area < 100 ? 1 : 0;
    return `${area.toLocaleString('ko-KR', { maximumFractionDigits })} km²`;
  }

  return Object.freeze({
    connect,

    get installRiverComponentIndex() { return installRiverComponentIndex; },
    get installComponentIndex() { return installComponentIndex; },
    get riverBoundaryComposition() { return riverBoundaryComposition; },

    get formatTerritoryArea() { return formatTerritoryArea; },
    get geometryMultiCoordinates() { return geometryMultiCoordinates; },
    get multiPolygonPlanarArea() { return multiPolygonPlanarArea; },
    get selectedTerritoryComponentGeometry() { return selectedTerritoryComponentGeometry; },
    get territoryBaseComponentItems() { return territoryBaseComponentItems; },
    get territoryComponentItems() { return territoryComponentItems; },
  });
}
