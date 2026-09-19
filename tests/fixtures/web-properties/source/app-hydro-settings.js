/** HydroSettings: extracted application responsibility.
 * Dependencies are explicitly wired once by the composition modules.
 * Mutable bindings stay local; exported accessors retain live identity.
 */
export function createHydroSettings() {
  let dependencies;

  function connect(ports) {
    if (dependencies) throw new Error('hydro-settings already connected');
    dependencies = ports;
  }

  function currentMapDevicePixelRatio() {
    const devicePixelRatio = dependencies.mapLayout.mapLayoutMetricsSnapshot?.dpr ?? Math.max(1, Number(window.devicePixelRatio || 1));
    return Math.min((0, dependencies.surfaces.isMobile)() ? 2 : 3, devicePixelRatio);
  }

  function activeLayerFolderKeys() {
    return ['polities', 'landforms'];
  }

  function normalizePhysicalSettings(value) {
    const previousLayers = value?.hydroLayers || {};
    const mergeVisibility = ids => {
      const defined = ids.filter(id => typeof previousLayers[id] === 'boolean');
      return defined.length ? defined.some(id => previousLayers[id] !== false) : true;
    };
    const hydroLayers = {
      rivers_hydro: mergeVisibility(['rivers_hydro', 'rivers_base', 'rivers_europe', 'rivers_north_america', 'rivers_australia']),
      lakes_natural_earth: mergeVisibility(['lakes_natural_earth', 'lakes_hydro', 'lakes_base', 'lakes_europe', 'lakes_north_america', 'lakes_australia']),
    };
    const hiddenHydroIds = Object.fromEntries(Object.entries(value?.hiddenHydroIds || {}).filter(([id, hidden]) => (
      hidden === true && !String(id).startsWith('rivers_base:') && !String(id).startsWith('hydro-lake:')
    )));
    return {
      terrainVisible: value?.terrainVisible !== false,
      terrainStyle: value?.terrainStyle === 'physical' ? 'physical' : 'political',
      hydroLayers,
      userFeaturesVisible: value?.userFeaturesVisible !== false,
      hiddenHydroIds,
      dataset: dependencies.physicalConfig.PHYSICAL_DATASET,
    };
  }

  function syncRangeProgress(input) {
    if (!input) return;
    const min = Number(input.min || 0);
    const max = Number(input.max || 100);
    const value = Number(input.value || min);
    const progress = max > min ? (0, dependencies.platform.clamp)(((value - min) / (max - min)) * 100, 0, 100) : 0;
    input.style.setProperty('--ui-range-progress', `${progress}%`);
  }

  function syncPhysicalControls() {
    const terrainVisible = dependencies.projectState.state.physicalSettings.terrainVisible !== false;
    if ((0, dependencies.platform.$)('terrainVisible')) {
      (0, dependencies.platform.$)('terrainVisible').checked = terrainVisible;
    }
    if ((0, dependencies.platform.$)('terrainPoliticalRadio')) (0, dependencies.platform.$)('terrainPoliticalRadio').checked = dependencies.projectState.state.physicalSettings.terrainStyle === 'political';
    if ((0, dependencies.platform.$)('terrainPhysicalRadio')) (0, dependencies.platform.$)('terrainPhysicalRadio').checked = dependencies.projectState.state.physicalSettings.terrainStyle === 'physical';
  }

  function parseHexRgb(value, fallback = dependencies.physicalConfig.TERRAIN_OCEAN_REPRESENTATIVE) {
    const match = /^#([0-9a-f]{6})$/i.exec(String(value || '')) || /^#([0-9a-f]{6})$/i.exec(fallback);
    const packed = Number.parseInt(match[1], 16);
    return [(packed >> 16) & 255, (packed >> 8) & 255, packed & 255];
  }

  function formatHexRgb(rgb) {
    return `#${rgb.map(value => (0, dependencies.platform.clamp)(Math.round(value), 0, 255).toString(16).padStart(2, '0')).join('')}`;
  }

  function automaticWaterColor(gpu = false) {
    if (dependencies.projectState.state.physicalSettings.terrainVisible && dependencies.projectState.state.physicalSettings.terrainStyle === 'physical') {
      const representative = dependencies.projectState.state.terrainManifest?.displayColors?.oceanRepresentative || dependencies.physicalConfig.TERRAIN_OCEAN_REPRESENTATIVE;
      let rgb = parseHexRgb(representative);
      if ((document.documentElement.dataset.theme || dependencies.preferences.systemTheme) === 'dark') rgb = rgb.map((value, index) => value * [0.808, 0.8464, 0.8848][index]);
      return gpu ? rgb.map(value => value / 255) : formatHexRgb(rgb);
    }
    const theme = (0, dependencies.preferences.mapTheme)();
    return gpu ? theme.oceanGpu : theme.ocean;
  }

  function hydroDisplayColor(_category, gpu = false) {
    return automaticWaterColor(gpu);
  }

  function normalizeHydroEdit(feature) {
    if (!feature?.geometry || !Array.isArray(feature.geometry.coordinates) || !feature.geometry.coordinates.length) return null;
    feature.properties ||= {};
    const geometryKind = (0, dependencies.objectPresentation.genericFeatureGeometryKind)(feature);
    const category = feature.properties.category === 'lake' && geometryKind === 'polygon' ? 'lake'
      : feature.properties.category === 'river' && geometryKind === 'line' ? 'river'
        : geometryKind === 'polygon' ? 'lake'
          : geometryKind === 'line' ? 'river' : '';
    if (!category) return null;
    feature.id = String(feature.id || '').trim();
    if (!feature.id) throw new Error('편집 강/호수 ID가 비어 있습니다.');
    feature.properties = {
      ...feature.properties,
      category,
      pandolab_domain: 'hydro',
      pandolab_schema_version: 1,
      pandolab_id: feature.id,
      name: String(feature.properties.name || ''),
      notes: String(feature.properties.notes || ''),
      editorColor: (0, dependencies.colorModel.normalizeEditorColor)(feature.properties.editorColor, dependencies.hydroPresentation.HYDRO_TOOL_CONFIG[category].color),
    };
    delete feature.properties.visible;
    return feature;
  }

  function normalizeHydroEditCollection(value) {
    const output = [];
    const seen = new Set();
    for (const feature of Array.isArray(value) ? value : []) {
      const normalized = normalizeHydroEdit(feature);
      if (!normalized) throw new Error('편집 강/호수 형식이 올바르지 않습니다.');
      if (seen.has(normalized.id)) throw new Error(`편집 강/호수 ID가 중복되었습니다: ${normalized.id}`);
      seen.add(normalized.id);
      output.push(normalized);
    }
    return output;
  }

  function hydroEditById(id) {
    const key = String(id);
    return dependencies.projectState.state.hydroEdits.find(feature => String(feature.id) === key) || null;
  }

  function isHydroEditFeature(feature) {
    return !!feature && (feature.properties?.pandolab_domain === 'hydro' || dependencies.projectState.state.hydroEdits.includes(feature));
  }

  function hydroLayerVisible(layerId) {
    const category = (0, dependencies.hydroPresentation.hydroCategoryKey)(dependencies.hydroPresentation.HYDRO_LAYER_META[layerId]?.category);
    return dependencies.projectState.state.layerVisibility[category === 'lake' ? 'lakes' : 'rivers'] !== false
      && dependencies.projectState.state.physicalSettings.hydroLayers?.[layerId] !== false;
  }

  function isHydroFeatureVisible(feature) {
    const id = String(feature?.properties?.pandolab_id || feature?.id || '');
    if (isHydroEditFeature(feature)) {
      const category = (0, dependencies.hydroPresentation.hydroCategoryKey)(feature.properties?.category);
      return dependencies.projectState.state.layerVisibility[category === 'lake' ? 'lakes' : 'rivers'] !== false && (0, dependencies.layerPresentation.isLayerItemVisible)('hydro', id);
    }
    return hydroLayerVisible(feature?.properties?.layer_id) && dependencies.projectState.state.physicalSettings.hiddenHydroIds?.[id] !== true;
  }

  function allBuiltInHydroFeatures() {
    const tiled = dependencies.projectState.state.hydroFeatureCache instanceof Map ? [...dependencies.projectState.state.hydroFeatureCache.values()] : [];
    const legacy = Object.values(dependencies.projectState.state.hydroCollections || {}).flatMap(collection => collection?.features || []);
    return [...tiled, ...legacy];
  }

  function builtInHydroFeatureById(id) {
    const key = String(id);
    if (dependencies.projectState.state.hydroFeatureCache instanceof Map && dependencies.projectState.state.hydroFeatureCache.has(key)) return dependencies.projectState.state.hydroFeatureCache.get(key);
    for (const feature of allBuiltInHydroFeatures()) {
      if (String(feature.properties?.pandolab_id || feature.id || '') === key) return feature;
    }
    return null;
  }

  function hydroFeatureById(id) {
    return hydroEditById(id) || builtInHydroFeatureById(id);
  }



  return Object.freeze({
    connect,

    get activeLayerFolderKeys() { return activeLayerFolderKeys; },
    get allBuiltInHydroFeatures() { return allBuiltInHydroFeatures; },
    get builtInHydroFeatureById() { return builtInHydroFeatureById; },
    get currentMapDevicePixelRatio() { return currentMapDevicePixelRatio; },
    get hydroDisplayColor() { return hydroDisplayColor; },
    get hydroEditById() { return hydroEditById; },
    get hydroFeatureById() { return hydroFeatureById; },
    get hydroLayerVisible() { return hydroLayerVisible; },
    get isHydroEditFeature() { return isHydroEditFeature; },
    get isHydroFeatureVisible() { return isHydroFeatureVisible; },
    get normalizeHydroEdit() { return normalizeHydroEdit; },
    get normalizeHydroEditCollection() { return normalizeHydroEditCollection; },
    get normalizePhysicalSettings() { return normalizePhysicalSettings; },
    get syncPhysicalControls() { return syncPhysicalControls; },
    get syncRangeProgress() { return syncRangeProgress; },
  });
}
