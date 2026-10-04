import { resolveTerritorialColor } from './color-adapter.js';
/** ObjectPresentation: extracted application responsibility.
 * Dependencies are explicitly wired once by the composition modules.
 * Mutable bindings stay local; exported accessors retain live identity.
 */
export function createObjectPresentation() {
  let dependencies;
  let territorialScope;
  let distributionVisibilityRevision;
  let distributionRenderRowCache;
  let territorialApplicationService;
  let distributionService;
  let genericFeatureService;
  let projectCommandPipeline;
  let LAYER_GROUP_KEYS;
  let LAYER_SEARCH_GROUP_KEYS;
  let layerGroupNames;
  let layerNameCollator;
  let expandedMapDisplayGroups;
  function connect(ports) {
    if (dependencies) throw new Error('object-presentation already connected');
    dependencies = ports;
  }

  function defaultGenericFeatureColor(feature) {
    return dependencies.colorModel.DEFAULT_GENERIC_FEATURE_COLOR;
  }

  function genericFeatureColor(feature) {
    return (0, dependencies.colorModel.readDomainColor)(dependencies.colorModel.COLOR_DOMAINS.GENERIC, { feature }, { fallback: defaultGenericFeatureColor(feature) }).value;
  }

  function genericFeatureRoleLabel(feature) {
    return dependencies.objectCatalog.GENERIC_FEATURE_ROLE_RULES[feature?.properties?.role]?.label || '기타 객체';
  }

  function genericFeatureRoleHelp(feature) {
    return '기타 객체는 독립된 형상을 가지며 다른 객체 종류로 전환할 수 있습니다.';
  }

  function genericFeatureDisplayFeature(feature) {
    return feature;
  }

  function genericFeatureName(feature) {
    return feature.properties?.name || `이름 없는 ${genericFeatureRoleLabel(feature)} ${String(feature.id || '').slice(0, 8)}`;
  }

  function territorialStyleColor(feature) {
    return (0, dependencies.colorModel.readDomainColor)(dependencies.colorModel.COLOR_DOMAINS.TERRITORIAL, { feature }).explicit;
  }

  function territorialEntityName(feature) {
    const entity = dependencies.territorialModel.entityRepository.get(feature?.id) || feature;
    const properties = entity?.properties || {};
    // Default names come from canonical assets; edited names are literal.
    if (properties.name) return properties.name;
    return '이름 없는 객체';
  }

  function territorialEntityColor(feature) {
    const entity = dependencies.territorialModel.entityRepository.get(feature?.id) || feature;
    const fallback = (entity?.properties?.entityKind === 'general' && !entity?.properties?.parentId)
      ? (0, dependencies.colorModel.defaultCountryColor)(entity) : dependencies.colorModel.DEFAULT_GENERIC_FEATURE_COLOR;
    return (0, dependencies.colorModel.readDomainColor)(dependencies.colorModel.COLOR_DOMAINS.TERRITORIAL, { feature: entity }, {
      inherited: resolveTerritorialColor(entity, { entityRepository: dependencies.territorialModel.entityRepository,
        countryColor: country => (0, dependencies.colorModel.defaultCountryColor)(country), fallback }),
      fallback,
    }).value;
  }

  function territorialRootName(feature) {
    const country = dependencies.territorialModel.entityRepository.root(feature?.id);
    return country?.properties.entityKind === 'general' ? territorialEntityName(country) : '';
  }

  function distributionColor(layer) {
    return (0, dependencies.colorModel.readDomainColor)(dependencies.colorModel.COLOR_DOMAINS.DISTRIBUTION, { layer }, { fallback: dependencies.colorModel.DEFAULT_GENERIC_FEATURE_COLOR }).value;
  }

  function hydroCategoryKey(value) {
    return value === 'lake' ? 'lake' : 'river';
  }

  function hydroCategoryLabel(value) {
    return hydroCategoryKey(value) === 'lake' ? '호수' : '강';
  }

  function hydroFallbackName(value) {
    return `이름 없는 ${hydroCategoryLabel(value)}`;
  }

  function hydroAccusativeLabel(value) {
    return hydroCategoryKey(value) === 'lake' ? '호수를' : '강을';
  }

  function syncMapObjectCategoryLabels() {
    const buildContent = (0, dependencies.platform.$)('createBuildPanel');
    if (buildContent) {
      dependencies.objectCatalog.MAP_OBJECT_CATEGORY_ORDER.forEach(categoryKey => {
        const categoryNode = buildContent.querySelector(`.ui-menu-group[data-map-category="${categoryKey}"]`);
        const category = dependencies.objectCatalog.MAP_OBJECT_CATEGORIES[categoryKey];
        if (!categoryNode || !category) return;
        categoryNode.setAttribute('role', 'group');
        categoryNode.setAttribute('aria-label', category.label);
        category.createItems.forEach(type => {
          const item = categoryNode.querySelector(`[data-map-object-type="${type}"]`);
          if (!item) return;
          const metadata = dependencies.objectCatalog.MAP_OBJECT_TYPES[type];
          const label = item.querySelector('span');
          const icon = item.querySelector('.ui-icon use');
          if (metadata) {
            if (label) label.textContent = metadata.label;
            if (icon) icon.setAttribute('href', `#${metadata.icon}`);
          }
          categoryNode.appendChild(item);
        });
      });
    }
  }

  function initializeTerritorialScope() {
    (territorialScope = (0, dependencies.objectPresentation.createTerritorialScopeResolver)({
      entityRepository: dependencies.territorialModel.entityRepository,
      clipper: () => window.polygonClipping,
      getState: () => dependencies.projectState.state,
    }));

    (distributionVisibilityRevision = 0);

    (distributionRenderRowCache = {
      layers: null,
      entries: null,
      countries: null,
      countryGeometryRevision: -1,
      territorialUnits: null,
      renderMode: '',
      selectedLayerId: '',
      visibilityRevision: -1,
      rows: [],
      rebuildCount: 0,
      buildMs: 0,
    });
  }

  function initializeObjectPresentationModel() {
    (LAYER_GROUP_KEYS = Object.freeze([...new Set([
      ...dependencies.objectCatalog.MAP_OBJECT_CATEGORIES.territorial.layerGroups,
      ...dependencies.objectCatalog.MAP_OBJECT_CATEGORIES.distribution.layerGroups,
      'hydro',
      'genericFeatures',
      ...dependencies.objectCatalog.MAP_OBJECT_CATEGORIES.features.viewGroups,
    ])]));

    (LAYER_SEARCH_GROUP_KEYS = LAYER_GROUP_KEYS.filter(group => group !== 'countryLabels'));

    (layerGroupNames = Object.freeze({
      ...Object.fromEntries(Object.values(dependencies.objectCatalog.MAP_OBJECT_TYPES)
        .filter(type => type.layerGroup)
        .map(type => [type.layerGroup, type.label])),
      distributions: '분포',
      hydro: '강·호수',
      countryLabels: '국가명',
    }));

    (layerNameCollator = new Intl.Collator('ko', { numeric: true, sensitivity: 'base' }));

    (expandedMapDisplayGroups = new Set());
  }

  return Object.freeze({
    connect,
    initializeTerritorialScope,
    initializeObjectPresentationModel,
    get LAYER_GROUP_KEYS() { return LAYER_GROUP_KEYS; },
    get LAYER_SEARCH_GROUP_KEYS() { return LAYER_SEARCH_GROUP_KEYS; },
    get defaultGenericFeatureColor() { return defaultGenericFeatureColor; },
    get distributionColor() { return distributionColor; },
    get distributionRenderRowCache() { return distributionRenderRowCache; },
    get distributionService() { return distributionService; },
    set distributionService(value) { distributionService = value; },
    get distributionVisibilityRevision() { return distributionVisibilityRevision; },
    set distributionVisibilityRevision(value) { distributionVisibilityRevision = value; },
    get expandedMapDisplayGroups() { return expandedMapDisplayGroups; },
    get genericFeatureColor() { return genericFeatureColor; },
    get genericFeatureDisplayFeature() { return genericFeatureDisplayFeature; },
    get genericFeatureName() { return genericFeatureName; },
    get genericFeatureRoleHelp() { return genericFeatureRoleHelp; },
    get genericFeatureRoleLabel() { return genericFeatureRoleLabel; },
    get genericFeatureService() { return genericFeatureService; },
    set genericFeatureService(value) { genericFeatureService = value; },
    get hydroAccusativeLabel() { return hydroAccusativeLabel; },
    get hydroCategoryKey() { return hydroCategoryKey; },
    get hydroCategoryLabel() { return hydroCategoryLabel; },
    get hydroFallbackName() { return hydroFallbackName; },
    get layerGroupNames() { return layerGroupNames; },
    get layerNameCollator() { return layerNameCollator; },
    get projectCommandPipeline() { return projectCommandPipeline; },
    set projectCommandPipeline(value) { projectCommandPipeline = value; },

    get syncMapObjectCategoryLabels() { return syncMapObjectCategoryLabels; },
    get territorialApplicationService() { return territorialApplicationService; },
    set territorialApplicationService(value) { territorialApplicationService = value; },
    get territorialScope() { return territorialScope; },
    get territorialStyleColor() { return territorialStyleColor; },

    get territorialEntityColor() { return territorialEntityColor; },
    get territorialRootName() { return territorialRootName; },
    get territorialEntityName() { return territorialEntityName; },
  });
}
