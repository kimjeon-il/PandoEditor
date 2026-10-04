import { effectiveTerritorialFlagUrl } from './country-flags.js';

/** LayerList: extracted application responsibility.
 * Dependencies are explicitly wired once by the composition modules.
 * Mutable bindings stay local; exported accessors retain live identity.
 */
export function createLayerList() {
  let dependencies;

  function connect(ports) {
    if (dependencies) throw new Error('layer-list already connected');
    dependencies = ports;
  }

  function normalizeLayerItemState(value) {
    const output = {};
    for (const group of dependencies.layerPresentation.LAYER_GROUP_KEYS) {
      const source = value?.[group];
      output[group] = source && typeof source === 'object' && !Array.isArray(source) ? { ...source } : {};
    }
    return output;
  }

  function normalizeLayerFolderState(value) {
    return Object.fromEntries((0, dependencies.hydroPresentation.activeLayerFolderKeys)().map(key => [key, !!value?.[key]]));
  }

  function markLayerTreeDirty() {
    dependencies.projectState.state.layerTreeRevision += 1;
  }

  function isLayerItemVisible(group, id) {
    if (group === 'hydro' && dependencies.hydroPresentation.HYDRO_LAYER_META[String(id)]) {
      return dependencies.projectState.state.physicalSettings.hydroLayers?.[String(id)] !== false;
    }
    return dependencies.projectState.state.itemVisibility?.[group]?.[String(id)] !== false;
  }

  function isCountryVisibleById(id) {
    return !!dependencies.projectState.state.layerVisibility.countries && isLayerItemVisible('countries', id);
  }

  function setLayerItemsVisibility(items, visible) {
    const byGroup = new Map();
    for (const item of items) {
      if (!dependencies.layerPresentation.LAYER_GROUP_KEYS.includes(item.layerGroup)) continue;
      const ids = byGroup.get(item.layerGroup) || [];
      ids.push(String(item.id)); byGroup.set(item.layerGroup, ids);
    }
    for (const [group, ids] of byGroup) {
      if (group === 'hydro') {
        // Preserve the effective visibility of *both* built-in sources and user edits
        // before lifting a legacy river/lake master switch.
        for (const category of ['river', 'lake']) {
          const categoryItems = layerTreeItems('hydro').filter(item => item.hydroCategory === category);
          const targets = categoryItems.filter(item => ids.includes(item.id));
          if (!targets.length) continue;
          const master = category === 'river' ? 'rivers' : 'lakes';
          if (dependencies.projectState.state.layerVisibility[master] === false) {
            dependencies.projectState.state.itemVisibility.hydro ||= {};
            for (const item of categoryItems) {
              if (item.isBuiltin) dependencies.projectState.state.physicalSettings.hydroLayers[item.id] = false;
              else dependencies.projectState.state.itemVisibility.hydro[item.id] = false;
            }
            dependencies.projectState.state.layerVisibility[master] = true;
          }
          for (const item of targets) {
            if (item.isBuiltin) dependencies.projectState.state.physicalSettings.hydroLayers[item.id] = !!visible;
            else {
              dependencies.projectState.state.itemVisibility.hydro ||= {};
              if (visible) delete dependencies.projectState.state.itemVisibility.hydro[item.id];
              else dependencies.projectState.state.itemVisibility.hydro[item.id] = false;
            }
          }
        }
        dependencies.rendering.gpuMapRenderer.invalidateHydroVisibility();
      } else {
        (0, dependencies.layerPresentation.setScopedItemVisibility)({
          layerVisibility: dependencies.projectState.state.layerVisibility, itemVisibility: dependencies.projectState.state.itemVisibility,
          group, allIds: layerTreeItems(group).map(item => item.id), ids, visible,
        });
        if (group === 'distributions') dependencies.distributionPresentation.bumpVisibilityRevision();
        if (group === 'countries') dependencies.rendering.gpuMapRenderer.invalidateCountryPalette({ base: true, emphasis: true }, 'country-item-visibility');
      }
    }
    if (!byGroup.size) return;
    // Existing presentation fields are saved once for the whole bundle operation.
    for (const [group] of byGroup) {
      const masters = group === 'hydro' ? ['rivers', 'lakes'] : [group];
      for (const master of masters) { const input = (0, dependencies.platform.$)(master + 'Visible'); if (input) input.checked = dependencies.projectState.state.layerVisibility[master] !== false; }
    }
    markLayerTreeDirty();
    dependencies.domains.renderingDomain?.invalidateOverlayStyle?.('layer-item-visibility');
    dependencies.domains.projectDomain.queuePresentationAutosave();
  }

  function setLayerItemVisibility(group, id, visible) {
    setLayerItemsVisibility([{ layerGroup: group, id }], visible);
  }

  function isLayerListItemVisible(group, id) {
    const master = group === 'hydro'
      ? ((0, dependencies.hydroPresentation.hydroCategoryKey)(dependencies.hydroPresentation.HYDRO_LAYER_META[id]?.category || (0, dependencies.hydroPresentation.hydroEditById)(id)?.properties?.category) === 'lake' ? 'lakes' : 'rivers')
      : group;
    return dependencies.projectState.state.layerVisibility[master] !== false && isLayerItemVisible(group, id);
  }

  function layerTreeItems(group) {
    if (group === 'countries' || group === 'countryLabels') {
      return dependencies.territorialModel.entityRepository
        .list({ kind: 'general', parentId: '' })
        .map(feature => {
        const id = String(feature.id || '');
        return {
          id,
          name: (0, dependencies.objectPresentation.territorialEntityName)(feature),
          color: (0, dependencies.colorModel.territorialEntityColor)(feature),
          flagUrl: effectiveTerritorialFlagUrl(feature, { assetRevision: dependencies.layerPresentation.ASSET_REVISION }),
          searchText: id,
          meta: group === 'countryLabels' && dependencies.labelPresentation.pendingCountryLabelAnchors.has(id) ? '계산 중' : '',
          selected: (dependencies.projectState.state.selected?.domain === 'territorial' && (dependencies.territorialModel.entityRepository.get(dependencies.projectState.state.selected?.id)?.properties.entityKind === 'general' && !dependencies.territorialModel.entityRepository.get(dependencies.projectState.state.selected?.id)?.properties.parentId)) && dependencies.projectState.state.selected.id === id,
        };
      });
    }
    if (group === 'subunits' || group === 'regions') {
      const kind = group === 'subunits'
        ? 'subunit'
        : 'region';
      return dependencies.territorialModel.entityRepository.list({ kind: kind === 'region' ? 'regional' : 'general' }).filter(feature => kind === 'region' || !!feature.properties.parentId).map(feature => {
        const countryLabel = (0, dependencies.objectPresentation.territorialRootName)(feature);

        return {
          id: String(feature.id),
          name: (0, dependencies.objectPresentation.territorialEntityName)(feature),
          color: (0, dependencies.colorModel.territorialEntityColor)(feature),
          flagUrl: effectiveTerritorialFlagUrl(feature, { assetRevision: dependencies.layerPresentation.ASSET_REVISION }),
          meta: '',
          searchText: countryLabel,
          folderName: kind === 'subunit'
            ? `하위단위 · ${countryLabel}`
            : kind === 'region'
            ? `지방${(0, dependencies.territorialModel.territorialRootId)(feature, id => dependencies.territorialModel.entityRepository.get(id)) ? ` · ${countryLabel}` : ''}`
            : `하위단위 · ${countryLabel}`,
          countryId: String((0, dependencies.territorialModel.territorialRootId)(feature, id => dependencies.territorialModel.entityRepository.get(id)) || ''),
          parentId: String(feature.properties?.parentId || ''),

          selected: (dependencies.projectState.state.selected?.domain === 'territorial' && !(dependencies.territorialModel.entityRepository.get(dependencies.projectState.state.selected?.id)?.properties.entityKind === 'general' && !dependencies.territorialModel.entityRepository.get(dependencies.projectState.state.selected?.id)?.properties.parentId)) && dependencies.projectState.state.selected.id === String(feature.id),
        };
      });
    }
    if (group === 'distributions') {
      return dependencies.projectState.state.distributionLayers.map(layer => ({
        id: layer.id,
        name: layer.name,
        color: (0, dependencies.distributionPresentation.distributionColor)(layer),
        folderName: dependencies.layerPresentation.layerGroupNames[group],
        selected: dependencies.projectState.state.selected?.domain === 'distribution' && dependencies.projectState.state.selected.id === layer.id,
      }));
    }
    if (group === 'hydro') {
      const builtIns = Object.entries(dependencies.hydroPresentation.HYDRO_LAYER_META).map(([id, meta]) => ({
          id,
          name: meta.sourceLabel,
          searchText: `${meta.label} ${meta.sourceLabel}`,
          title: `${meta.sourceLabel} 상태 보기`,
          color: (0, dependencies.hydroPresentation.hydroDisplayColor)(meta.category),
          folderName: meta.label,
          hydroCategory: (0, dependencies.hydroPresentation.hydroCategoryKey)(meta.category),
          layerGroup: 'hydro',
          isBuiltin: true,
          selected: false,
        }));
      const userItems = dependencies.projectState.state.hydroEdits.map(feature => ({
        id: String(feature.id),
        name: (0, dependencies.hydroPresentation.hydroEditorName)(feature.properties?.name, (0, dependencies.hydroPresentation.hydroFallbackName)(feature.properties?.category)),
        color: feature.properties?.editorColor || dependencies.hydroPresentation.HYDRO_TOOL_CONFIG[feature.properties?.category || 'river'].color,
        meta: `${(0, dependencies.hydroPresentation.hydroCategoryLabel)(feature.properties?.category)} · 사용자`,
        folderName: (0, dependencies.hydroPresentation.hydroCategoryLabel)(feature.properties?.category),
        hydroCategory: (0, dependencies.hydroPresentation.hydroCategoryKey)(feature.properties?.category),
        layerGroup: 'hydro',
        isBuiltin: false,
        selected: dependencies.projectState.state.selected?.domain === 'hydro' && dependencies.projectState.state.selected.id === String(feature.id),
      }));
      return [...builtIns, ...userItems];
    }
    if (group === 'genericFeatures') {
      return dependencies.projectState.state.genericFeatures.map(feature => ({
        id: String(feature.id),
        name: (0, dependencies.objectPresentation.genericFeatureName)(feature),
        color: (0, dependencies.colorModel.genericFeatureColor)(feature),
        meta: `${(0, dependencies.objectPresentation.genericFeatureRoleLabel)(feature)} · 사용자`,
        layerGroup: 'genericFeatures',
        selected: dependencies.projectState.state.selected?.domain === 'generic' && dependencies.projectState.state.selected.id === String(feature.id),
      }));
    }
    return dependencies.projectState.state.labels.map(label => ({
      id: String(label.id),
      name: label.name || '이름 없는 지명',
      icon: 'place',
      meta: label.kind || '지명',
      selected: dependencies.projectState.state.selected?.domain === 'label' && dependencies.projectState.state.selected.id === String(label.id),
    }));
  }

  function pruneLayerItemVisibility() {
    const valid = {
      countries: new Set(dependencies.territorialModel.entityRepository
        .list({ kind: 'general', parentId: '' })
        .map(feature => String(feature.id || ''))),
      countryLabels: new Set((0, dependencies.countries.builtinTerritorialScene)().labelById.keys()),
      subunits: new Set(dependencies.territorialModel.entityRepository
        .list({ kind: 'general' }).filter(entity => !!entity.properties.parentId)
        .map(feature => String(feature.id))),
      regions: new Set(dependencies.territorialModel.entityRepository
        .list({ kind: 'regional' })
        .map(feature => String(feature.id))),
      distributions: new Set(dependencies.projectState.state.distributionLayers.map(layer => layer.id)),
      hydro: new Set([...Object.keys(dependencies.hydroPresentation.HYDRO_LAYER_META), ...dependencies.projectState.state.hydroEdits.map(feature => String(feature.id))]),
      genericFeatures: new Set(dependencies.projectState.state.genericFeatures.map(feature => String(feature.id))),
      labels: new Set(dependencies.projectState.state.labels.map(label => String(label.id))),
    };
    for (const group of dependencies.layerPresentation.LAYER_GROUP_KEYS) {
      dependencies.projectState.state.itemVisibility[group] ||= {};
      for (const id of Object.keys(dependencies.projectState.state.itemVisibility[group])) if (!valid[group].has(id)) delete dependencies.projectState.state.itemVisibility[group][id];
    }

    dependencies.domains.selectionDomain?.prune?.(null, { reason: 'prune-invalid-selection' });
  }

  return Object.freeze({
    connect,

    get isCountryVisibleById() { return isCountryVisibleById; },
    get isLayerItemVisible() { return isLayerItemVisible; },
    get isLayerListItemVisible() { return isLayerListItemVisible; },
    get layerTreeItems() { return layerTreeItems; },
    get markLayerTreeDirty() { return markLayerTreeDirty; },
    get normalizeLayerFolderState() { return normalizeLayerFolderState; },
    get normalizeLayerItemState() { return normalizeLayerItemState; },
    get pruneLayerItemVisibility() { return pruneLayerItemVisibility; },
    get setLayerItemVisibility() { return setLayerItemVisibility; },
    get setLayerItemsVisibility() { return setLayerItemsVisibility; },
  });
}
