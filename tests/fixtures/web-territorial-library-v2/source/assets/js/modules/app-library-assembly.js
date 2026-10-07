/** LibraryAssembly: extracted application responsibility.
 * Dependencies are explicitly wired once by the composition modules.
 * Mutable bindings stay local; exported accessors retain live identity.
 */
export function createLibraryAssembly() {
  let dependencies;
  let territorialLibraryService;
  let territorialLibraryController;
  let controllerPromise = null;
  let batchPreparation = null;
  function connect(ports) {
    if (dependencies) throw new Error('library-assembly already connected');
    dependencies = ports;
  }

  function territorialLibraryPreviewSvg(entity, version) {
    const wrapper = document.createElement('div');
    wrapper.className = 'territorial-library-preview-map';
    const svgNode = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svgNode.setAttribute('viewBox', '0 0 420 190');
    svgNode.setAttribute('aria-label', `${entity.names.ko || entity.names.en || Object.values(entity.names)[0]} 경계 미리보기`);
    const projection = dependencies.platform.d3.geo.equirectangular().scale(1).translate([0, 0]);
    const previewPath = dependencies.platform.d3.geo.path().projection(projection);
    // D3 spherical paths need canonical winding; normalize only a display copy.
    const feature = { type: 'Feature', properties: {}, geometry: (0, dependencies.geometryModel.normalizePolygonGeometry)(version.geometry) };
    const bounds = previewPath.bounds(feature);
    const width = Math.max(Number.EPSILON, bounds[1][0] - bounds[0][0]);
    const height = Math.max(Number.EPSILON, bounds[1][1] - bounds[0][1]);
    const scale = 0.86 / Math.max(width / 420, height / 190);
    const center = [(bounds[0][0] + bounds[1][0]) / 2, (bounds[0][1] + bounds[1][1]) / 2];
    projection.scale(scale).translate([210 - scale * center[0], 95 - scale * center[1]]);
    const pathNode = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    pathNode.setAttribute('d', previewPath(feature) || '');
    pathNode.setAttribute('fill', 'var(--accent-surface)');
    pathNode.setAttribute('stroke', 'var(--accent-border)');
    pathNode.setAttribute('stroke-width', '1.5');
    pathNode.setAttribute('vector-effect', 'non-scaling-stroke');
    svgNode.appendChild(pathNode);
    wrapper.appendChild(svgNode);
    return wrapper;
  }

  async function instantiateTerritorialLibraryEntities(rootIds, referenceDate, childDepth = 'none', options = {}) {
    const revision = dependencies.projectState.state.stateRevision;
    const landRevision = dependencies.countries.countryLandRevision;
    const currentEntities = dependencies.projectState.state.territorialEntities;
    const currentCountries = { type: 'FeatureCollection', features: dependencies.territorialModel.entityRepository.list({ kind: 'general', parentId: '' }) };
    const assertCurrent = () => {
      if (options.isCurrent?.() === false || dependencies.projectState.state.stateRevision !== revision || dependencies.countries.countryLandRevision !== landRevision || dependencies.projectState.state.territorialEntities !== currentEntities) {
        throw new Error('프로젝트 또는 선택이 변경되었습니다. 항목과 소속을 다시 확인하세요.');
      }
    };
    const preparationKey = JSON.stringify([revision, landRevision, rootIds, referenceDate, childDepth, options.ownership || {}]);
    if (batchPreparation?.key !== preparationKey || batchPreparation.project !== currentEntities) {
      const entry = { key: preparationKey, project: currentEntities, promise: null };
      batchPreparation = entry;
      entry.promise = (async () => {
        const descriptors = await territorialLibraryService.instantiateDescriptors(rootIds, referenceDate, childDepth);
        const countries = dependencies.territorialModel.entityRepository.list({ kind: 'general', parentId: '' });
        const existingUnits = dependencies.territorialModel.entityRepository.list()
          .filter(feature => !(feature.properties?.entityKind === 'general' && !feature.properties?.parentId));
        const prepared = (0, dependencies.libraryServices.prepareLibraryOwnership)({
          descriptors, countries,
          units: existingUnits, choices: options.ownership || {},
          allocateId: type => (0, dependencies.surfaces.uid)(`library_${type}`),
          // Exact containment is checked in the batch Worker before applying anything.
          contains: null,
        });
        if (!prepared.length) return { prepared };
        const countryFeatures = prepared.filter(item => item.entityKind === 'general' && !item.parentId).map(item =>
          (0, dependencies.territorialServicesA.createTerritorialFeature)({id:item.id,entityKind:'general',name:item.name,
            geometry:item.geometry,sourceEntityId:item.entityId,sourceGeometryVersion:item.geometryVersionId,
            color:item.metadata?.defaultColor || '',metadata:{...item.metadata,
              ...(item.metadata?.defaultFlagDataUrl ? {flagDataUrl:item.metadata.defaultFlagDataUrl} : {})}}));
        const units = prepared.filter(item => item.entityKind === 'regional' || !!item.parentId).map(item => (0, dependencies.territorialServicesA.createTerritorialFeature)({
          id: item.id, entityKind: item.entityKind, name: item.name, geometry: item.geometry,
          parentId: item.entityKind === 'regional' ? '' : item.parentId,
          coverageMode: item.entityKind === 'regional' ? dependencies.territorialModel.TERRITORIAL_COVERAGE_MODES.EXPLICIT : dependencies.territorialModel.TERRITORIAL_COVERAGE_MODES.PARTITION,
          validFrom: item.validFrom, validTo: item.validTo,
          color: item.metadata?.defaultColor || '',
          metadata: item.metadata, sourceEntityId: item.entityId, sourceGeometryVersion: item.geometryVersionId,
        }));
        const response = await dependencies.spatialQuery.mapEditClient.execute('territorial-library-batch', { payload: { countries: countryFeatures, units } });
        return { descriptors, prepared, countryFeatures, units, batch: response.result, sourceRevision: response.sourceRevision };

      })().catch(error => { if (batchPreparation === entry) batchPreparation = null; throw error; });
    }
    const { descriptors, prepared, countryFeatures, units, batch, sourceRevision } = await batchPreparation.promise;
    assertCurrent();
    if (!prepared.length) return { added: 0, subtracted: 0, deleted: 0, affectedIds: [] };
    if (!dependencies.spatialQuery.mapEditClient.sourcesCurrent(sourceRevision)) {
      batchPreparation = null;
      throw new Error('프로젝트가 변경되었습니다. 추가할 항목을 다시 준비하세요.');
    }
    const patches = new Map(batch.features.map(feature => [String(feature.id), feature]));
    const removed = new Set(batch.removedIds);
    const existingIds = new Set(currentCountries.features.map(feature => String(feature.id)));
    const draft = { type: 'FeatureCollection', features: currentCountries.features.filter(feature => !removed.has(String(feature.id)))
      .map(feature => patches.get(String(feature.id)) || feature).concat(batch.features.filter(feature => !existingIds.has(String(feature.id)))) };
    const affectedIds = new Set(batch.affectedIds), donorIds = new Set(batch.donorIds);
    const transfers = batch.transfers, deleted = batch.deleted;
    const impacts = batch.impacts.map(impact => impact.expansion ? `${impact.name}: 소속 하위단위의 경계까지 국가 영토 확장`
      : `${impact.name}: ${impact.area.toLocaleString('ko', { maximumFractionDigits: 3 })} km² 이전${impact.deleted ? ' · 전체 영토 이전' : ''}`);
    const impactKey = JSON.stringify([revision, landRevision, rootIds, referenceDate, childDepth, options.ownership || {}, impacts]);
    if (impacts.length && options.confirmedImpact !== impactKey) return { confirmationRequired: true, impactKey, impacts };
    assertCurrent();
    // Library entries usually merge through the GIS transaction, which also
    // handles replacements and territorial units.  A country-add can update
    // its donor boundaries, but it must not remove a country or add another
    // object kind.  Those cases keep the normal invalidation path so stale
    // boundaries cannot be retained.
    const preserveExistingScene = countryFeatures.length > 0
      && countryFeatures.length === prepared.length
      && !units.length
      && !deleted;
    const committer = await (0, dependencies.gisRuntime.getGisImportCommitter)();
    assertCurrent();
    const result = await committer.commitGisMerge({
      countriesData: { type: 'FeatureCollection', features: countryFeatures },
      preparedTerritorialUnits: units, landTransfers: transfers, assertCurrent,
      countryUpdates: Object.assign({}, ...prepared.map(item => item.instantiation?.countryUpdates || {})),
      sourceInfo: { imports: prepared.map(item => ({
        ...(item.metadata?.sourceInfo || {}), kind: 'library', sourceId: item.entityId,
        objectId: item.id, sourceType: descriptors.find(original => original.entityId === item.entityId)?.type,
        geometryVersionId: item.geometryVersionId, referenceDate,
        originalParentEntityId: item.parentEntityId,
      })) },
      commitStatus: '라이브러리 항목과 소속 관계를 한 번의 작업으로 추가했습니다.',
    }, {
      countriesData: draft, affectedIds: [...affectedIds],
      counts: { added: prepared.length, subtracted: donorIds.size, deleted },
      countryPatchPresentation: preserveExistingScene ? 'preserve-existing-scene' : 'replace-scene',
    });
    (0, dependencies.layers.markLayerTreeDirty)();
    (0, dependencies.spatialRecords.scheduleMapObjectSpatialIndexRebuild)();
    if (!preserveExistingScene) dependencies.domains.renderingDomain?.invalidateProject?.('territorial-library-import');
    dependencies.projectSession.saveState.markNewProject('content:0');
    batchPreparation = null;
    return result;
  }

  async function getTerritorialLibraryController() {
    if (territorialLibraryController) return territorialLibraryController;
    if (controllerPromise) return controllerPromise;
    controllerPromise = (async () => {
    await Promise.all([(0, dependencies.libraryServices.ensureTerritorialLibraryRuntime)(), dependencies.gisRuntime.gisWorkflow.ensure(), (0, dependencies.applicationServicesA.ensureModalRuntime)()]);
    const { createTerritorialLibraryService } = dependencies.libraryServices.territorialLibraryServiceModule;
    const { createTerritorialLibraryController } = dependencies.libraryServices.territorialLibraryControllerModule;
    const {createTerritorialEntityLoader} = dependencies.libraryServices.territorialEntityLoaderModule;
    const meta = window.PANDOLAB_BUILD_META;
    territorialLibraryService = createTerritorialLibraryService({loader:createTerritorialEntityLoader({
      indexUrl: dependencies.platformConfigurationA.TERRITORIAL_LIBRARY_INDEX_URL,
      indexSpec: meta.territorialIndex, dataRevision: meta.dataRevision,
    })});
    territorialLibraryController = createTerritorialLibraryController({
      document,
      elements: {
        open: null,
        modal: (0, dependencies.platform.$)('territorialLibraryModal'),
        card: document.querySelector('.territorial-library-card'),
        close: (0, dependencies.platform.$)('territorialLibraryCloseBtn'),
        backdrop: (0, dependencies.platform.$)('territorialLibraryModal').querySelector('.ui-dialog-backdrop'),
        search: (0, dependencies.platform.$)('territorialLibrarySearchInput'),
        clearSearch: (0, dependencies.platform.$)('territorialLibrarySearchClearBtn'),
        referenceDate: (0, dependencies.platform.$)('territorialLibraryReferenceDateInput'),
        results: (0, dependencies.platform.$)('territorialLibraryResults'),
        preview: (0, dependencies.platform.$)('territorialLibraryPreview'),
        childDepth: (0, dependencies.platform.$)('territorialLibraryChildDepthInput'),
        add: (0, dependencies.platform.$)('territorialLibraryAddBtn'),
        addOptions: (0, dependencies.platform.$)('territorialLibraryAddOptions'),
        optionsBack: (0, dependencies.platform.$)('territorialLibraryOptionsBackBtn'),
        ownership: (0, dependencies.platform.$)('territorialLibraryOwnership'),
      },
      service: territorialLibraryService,
      selectGeometryVersion: dependencies.applicationServicesB.selectGeometryVersion,
      renderMapPreview: territorialLibraryPreviewSvg,
      createEmptyState: dependencies.platformConfigurationB.createEmptyState,
      replaceSelectOptions: dependencies.propertyEditingB.replaceSelectOptions,
      shouldShowTerritorialParentChoice: dependencies.territorialServicesA.shouldShowTerritorialParentChoice,
      isMobile: dependencies.surfaces.isMobile,
      closeSurface: dependencies.workspaceUiA.closeSurface,
      focusSurfaceTrigger: dependencies.workspaceUiB.focusSurfaceTrigger,
      instantiate: instantiateTerritorialLibraryEntities,
      getProjectGeneration: () => dependencies.domains.projectDomain.getGeneration(),
      ownershipContext: async (ids, referenceDate, depth) => {
        return {
          missing: (0, dependencies.libraryServices.missingLibraryOwnership)(
            await territorialLibraryService.instantiateDescriptors(ids, referenceDate, depth),
          ),
          countries: (0, dependencies.propertyEditingB.territorialRootOptions)().filter(option => option.value),
          parents: id => (0, dependencies.territorialServicesA.territorialParentChoices)(
            id,
            dependencies.territorialModel.entityRepository,
            {
              name: feature => (0, dependencies.objectPresentation.territorialEntityName)(feature),
            },
          ),
        };
      },
      setStatus: dependencies.feedback.setActionStatus,
      reportError: dependencies.feedback.reportOperationError,
    });
    territorialLibraryController.connect();
    return territorialLibraryController;
    })().catch(error => { controllerPromise = null; throw error; });
    return controllerPromise;
  }

  function initializeTerritorialLibraryService() {
    (territorialLibraryService = null);

    (territorialLibraryController = null);
    controllerPromise = null;


    window.PANDOLAB_TERRITORIAL_LIBRARY = Object.freeze({
      load: async () => { await getTerritorialLibraryController(); return territorialLibraryService.load(); },
      get: async id => { await getTerritorialLibraryController(); await territorialLibraryService.load(); return territorialLibraryService.loadEntity(id); },
      list: async () => { await getTerritorialLibraryController(); return territorialLibraryService.list(); },
      search: async options => { await getTerritorialLibraryController(); return territorialLibraryService.search(options); },
      snapshots: async () => { await getTerritorialLibraryController(); return territorialLibraryService.snapshots(); },
      instantiate: async (id, referenceDate, childDepth = 'none') => {
        await getTerritorialLibraryController();
        await territorialLibraryService.load();
        return instantiateTerritorialLibraryEntities([id], referenceDate, childDepth);
      },
    });
  }

  return Object.freeze({
    connect,
    initializeTerritorialLibraryService,
    get getTerritorialLibraryController() { return getTerritorialLibraryController; },
    get territorialLibraryController() { return territorialLibraryController; },
  });
}
