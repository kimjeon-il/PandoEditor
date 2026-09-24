import {
  HISTORICAL_LIBRARY_SCHEMA_VERSION,
  createCurrentCountryLibraryEntities,
  createHistoricalLibrary,
  instantiateLibraryEntity,
  materializePilotEntities,
} from './historical-library.js';

export function createHistoricalLibraryService({
  dataUrl,
  fetchJson,
  getCountriesData,
  getMaterializationCountriesData = null,
  displayName,
  combineGeometries,
  subtractGeometries = null,
  currentYear = () => new Date().getFullYear(),
}) {
  let library = null;
  let loadPromise = null;

  async function load() {
    if (library) return library;
    if (loadPromise) return loadPromise;
    loadPromise = (async () => {
      const pilot = await fetchJson(dataUrl);
      if (Number(pilot?.schemaVersion) !== HISTORICAL_LIBRARY_SCHEMA_VERSION) {
        throw new Error('역사 라이브러리 schemaVersion이 현재 형식과 일치하지 않습니다.');
      }
      const countriesData = getCountriesData();
      const materializationCountriesData = typeof getMaterializationCountriesData === 'function'
        ? getMaterializationCountriesData()
        : countriesData;
      const currentEntities = createCurrentCountryLibraryEntities(countriesData, { displayName });
      const pilotEntities = materializePilotEntities(
        pilot.entities,
        materializationCountriesData,
        combineGeometries,
        subtractGeometries,
      );
      const currentSnapshot = {
        id: 'current-world',
        name: '현재 세계',
        referenceDate: String(currentYear()),
        entityRefs: currentEntities.map(entity => entity.libraryId),
        metadata: { current: true },
        sourceInfo: { title: 'Natural Earth 5.1.1 Admin 0 Countries', license: 'Public domain' },
      };
      library = createHistoricalLibrary({
        schemaVersion: pilot.schemaVersion,
        entities: [...currentEntities, ...pilotEntities],
        snapshots: [currentSnapshot, ...(pilot.snapshots || [])],
      });
      return library;
    })();
    try {
      return await loadPromise;
    } catch (error) {
      loadPromise = null;
      throw error;
    }
  }

  function entityRefsWithChildren(rootIds, depth = 'none') {
    const selected = new Set((rootIds || []).map(String));
    if (!library || depth === 'none') return [...selected];
    let frontier = [...selected];
    while (frontier.length) {
      const parents = new Set(frontier);
      const next = [];
      for (const entity of library.list()) {
        if (!parents.has(entity.parentLibraryId) || selected.has(entity.libraryId)) continue;
        selected.add(entity.libraryId);
        next.push(entity.libraryId);
      }
      if (depth === 'level1') break;
      frontier = next;
    }
    return [...selected];
  }

  function instantiateDescriptors(rootIds, referenceDate, childDepth = 'none', versionOverrides = {}) {
    return entityRefsWithChildren(rootIds, childDepth)
      .map(id => library?.get(id))
      .filter(Boolean)
      .map(entity => instantiateLibraryEntity(entity, referenceDate, versionOverrides?.[entity.libraryId]));
  }

  return Object.freeze({
    entityRefsWithChildren,
    get: id => library?.get(id) || null,
    getSnapshot: id => library?.getSnapshot(id) || null,
    instantiateDescriptors,
    list: () => library?.list() || [],
    load,
    search: options => library?.search(options) || [],
    snapshots: () => library?.snapshots() || [],
  });
}
