import { subunitSelectionPolicy, territorialDeletionAllowed, removeTerritorialUnits } from './territorial-interaction-policy.js';
import './territorial-edit-plan.js';
/** ObjectCommands: extracted application responsibility.
 * Dependencies are explicitly wired once by the composition modules.
 * Mutable bindings stay local; exported accessors retain live identity.
 */
export function createObjectCommands() {
  let dependencies;
  let objectActionsMenuTrigger;
  let baseSvg;
  let flatOceanLayer;
  function connect(ports) {
    if (dependencies) throw new Error('object-commands already connected');
    dependencies = ports;
  }

  function countryObjectRef(id) {
    return (0, dependencies.selectionServices.normalizeObjectRef)({ domain: 'territorial', type: dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY, id: String(id) });
  }

  function layerItemObjectRef(group, id) {
    const key = String(id);
    if (group === 'countries' || group === 'countryLabels') return (0, dependencies.selectionServices.normalizeObjectRef)({ domain: 'territorial', type: dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY, id: key });
    if (group === 'subunits' || group === 'regions') {
      const fallback = group === 'subunits' ? dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.SUBUNIT : group === 'regions' ? dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.REGION : dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.SUBUNIT;
      return (0, dependencies.selectionServices.normalizeObjectRef)({ domain: 'territorial', type: (0, dependencies.objectPresentation.territorialUnitById)(key)?.properties?.unitType || fallback, id: key });
    }
    if (dependencies.distributionPresentation.DISTRIBUTION_GROUP_TYPES[group]) return (0, dependencies.selectionServices.normalizeObjectRef)({ domain: 'distribution', type: (0, dependencies.propertyEditingA.distributionLayerById)(key)?.type || dependencies.distributionPresentation.DISTRIBUTION_GROUP_TYPES[group], id: key });
    if (group === 'hydro' && (0, dependencies.hydroPresentation.hydroEditById)(key)) return (0, dependencies.selectionServices.normalizeObjectRef)({ domain: 'hydro', type: (0, dependencies.hydroPresentation.hydroEditById)(key)?.properties?.category || 'river', id: key });
    if (group === 'genericFeatures') return (0, dependencies.selectionServices.normalizeObjectRef)({ domain: 'generic', type: 'feature', id: key });
    if (group === 'labels') return (0, dependencies.selectionServices.normalizeObjectRef)({ domain: 'label', type: dependencies.projectState.state.labels.find(item => String(item.id) === key)?.kind || 'label', id: key });
    return null;
  }

  function objectRefExists(value) {
    const ref = (0, dependencies.selectionServices.normalizeObjectRef)(value);
    if (!ref) return false;
    if (ref.domain === 'territorial') return ref.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY ? !!(0, dependencies.countries.countryFeatureById)(ref.id) : !!(0, dependencies.objectPresentation.territorialUnitById)(ref.id);
    if (ref.domain === 'distribution') return !!(0, dependencies.propertyEditingA.distributionLayerById)(ref.id);
    if (ref.domain === 'generic') return dependencies.projectState.state.genericFeatures.some(item => String(item.id) === ref.id);
    if (ref.domain === 'hydro') return !!(0, dependencies.hydroModel.hydroFeatureById)(ref.id);
    if (ref.domain === 'label') return dependencies.projectState.state.labels.some(item => String(item.id) === ref.id);
    return false;
  }

  function objectDisplayInfo(value) {
    const ref = (0, dependencies.selectionServices.normalizeObjectRef)(value);
    if (!ref) return { name: '알 수 없는 객체', type: '' };
    if (ref.domain === 'territorial' && ref.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY) {
      const feature = (0, dependencies.countries.countryFeatureById)(ref.id);
      return { name: feature ? (0, dependencies.presentation.countryName)(feature) : ref.id, type: '국가', detail: feature?.properties?.name || '' };
    }
    if (ref.domain === 'territorial') {
      const feature = (0, dependencies.objectPresentation.territorialUnitById)(ref.id);
      const type = (0, dependencies.territorialServicesB.territorialTypeLabel)(ref.type);
      const context = ref.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.REGION ? '' : (0, dependencies.objectPresentation.territorialUnitCountryName)(feature);
      return { name: feature ? (0, dependencies.objectPresentation.territorialUnitName)(feature) : ref.id, type, detail: context };
    }
    if (ref.domain === 'distribution') {
      const layer = (0, dependencies.propertyEditingA.distributionLayerById)(ref.id);
      return { name: layer?.name || ref.id, type: dependencies.objectModelA.DISTRIBUTION_TYPE_LABELS[layer?.type || ref.type] || '분포', detail: `${(0, dependencies.distributionServices.distributionEntriesForLayer)(dependencies.projectState.state.distributionEntries, ref.id).length}개 분포` };
    }
    if (ref.domain === 'generic') {
      const feature = dependencies.projectState.state.genericFeatures.find(item => String(item.id) === ref.id);
      return { name: feature ? (0, dependencies.objectPresentation.genericFeatureName)(feature) : ref.id, type: feature ? (0, dependencies.objectPresentation.genericFeatureRoleLabel)(feature) : '기타 객체', detail: '' };
    }
    if (ref.domain === 'hydro') {
      const feature = (0, dependencies.hydroModel.hydroFeatureById)(ref.id);
      const category = (0, dependencies.hydroPresentation.hydroCategoryKey)(feature?.properties?.category || ref.type);
      return { name: (0, dependencies.hydroPresentation.hydroEditorName)(feature?.properties?.name, (0, dependencies.hydroPresentation.hydroFallbackName)(category)), type: (0, dependencies.hydroPresentation.hydroCategoryLabel)(category), detail: '' };
    }
    const label = dependencies.projectState.state.labels.find(item => String(item.id) === ref.id);
    const labelKind = { capital: '수도', city: '도시', town: '마을', region: '지역명', mountain: '산', water: '수역', custom: '기타' };
    return { name: label?.name || ref.id, type: '지명', detail: labelKind[label?.kind] || '지명' };
  }

  function focusObjectRef(value, { announce = true } = {}) {
    const ref = (0, dependencies.selectionServices.normalizeObjectRef)(value);
    if (!ref) return false;
    let feature = null;
    if (ref.domain === 'territorial') feature = ref.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY ? (0, dependencies.countries.countryFeatureById)(ref.id) : (0, dependencies.objectPresentation.territorialUnitById)(ref.id);
    else if (ref.domain === 'generic') feature = dependencies.projectState.state.genericFeatures.find(item => String(item.id) === ref.id);
    else if (ref.domain === 'hydro') feature = (0, dependencies.hydroModel.hydroFeatureById)(ref.id);
    else if (ref.domain === 'distribution') {
      const features = (0, dependencies.distributionServices.distributionEntriesForLayer)(dependencies.projectState.state.distributionEntries, ref.id).map(entry => {
        const geometry = entry.mode === dependencies.territorialModel.DISTRIBUTION_MODES.TERRITORIAL ? dependencies.presentation.territorialRepository.get(entry.territorialUnitId)?.geometry : entry.geometry;
        return geometry ? { type: 'Feature', properties: {}, geometry } : null;
      }).filter(Boolean);
      if (features.length) feature = { type: 'FeatureCollection', features };
    } else if (ref.domain === 'label') {
      const label = dependencies.projectState.state.labels.find(item => String(item.id) === ref.id);
      if (label) {
        (0, dependencies.navigation.focusCoordinate)(label.coordinates);
        if (announce) (0, dependencies.feedback.setActionStatus)('선택 객체로 이동했습니다.', 'success', 2200);
        return true;
      }
    }
    if (!feature?.geometry && feature?.type !== 'FeatureCollection') return false;
    const countryScope = ref.domain === 'territorial' && ref.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY
      ? dependencies.objectModelB.territorialScope.scope(ref.id) : null;
    if (countryScope?.members.length) feature = countryScope.extent;
    const runtimeAnchor = ref.domain === 'territorial' && ref.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY
      ? dependencies.labelPresentation.countryLabelAnchors.get(String(feature?.id || ''))
      : null;
    const preferredAnchor = !countryScope?.members.length && (0, dependencies.countries.validLabelAnchor)(runtimeAnchor)
      ? runtimeAnchor
      : null;
    (0, dependencies.navigation.focusCountry)(feature, { maxZoom: (0, dependencies.surfaces.isMobile)() ? 12 : 10, preferredAnchor });
    if (announce) (0, dependencies.feedback.setActionStatus)('선택 객체로 이동했습니다.', 'success', 2200);
    return true;
  }

  function layerGroupForObjectRef(value) {
    const ref = (0, dependencies.selectionServices.normalizeObjectRef)(value);
    if (!ref) return '';
    if (ref.domain === 'territorial') return ref.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY ? 'countries' : ref.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.SUBUNIT ? 'subunits' : ref.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.REGION ? 'regions' : 'subunits';
    if (ref.domain === 'distribution') return dependencies.distributionPresentation.DISTRIBUTION_TYPE_GROUPS[ref.type] || '';
    if (ref.domain === 'hydro' && (0, dependencies.hydroPresentation.hydroEditById)(ref.id)) return 'hydro';
    if (ref.domain === 'generic') return 'genericFeatures';
    if (ref.domain === 'label') return 'labels';
    return '';
  }

  function objectBatchCapabilities(value) {
    const ref = (0, dependencies.selectionServices.normalizeObjectRef)(value);
    if (!ref) return new Set();
    if (ref.domain === 'hydro' && !(0, dependencies.hydroPresentation.hydroEditById)(ref.id)) return new Set(['visible']);
    const values = new Set(['visible']);
    if (ref.domain === 'territorial') {
      values.add('color');
      values.add('lock');
      if (ref.type !== dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY && territorialDeletionAllowed([(0, dependencies.objectPresentation.territorialUnitById)(ref.id)], dependencies.projectState.state.territorialUnits)) values.add('delete');
    } else if (ref.domain === 'distribution') {
      values.add('color'); values.add('lock'); values.add('delete');
    } else if (ref.domain === 'hydro') {
      values.add('color'); values.add('lock'); values.add('delete');
    } else if (ref.domain === 'generic') {
      values.add('lock'); values.add('delete');
    } else if (ref.domain === 'label') values.add('delete');
    return values;
  }

  function commonBatchCapabilities(refs = dependencies.domains.selectionDomain.snapshot().selection.items) {
    if (!refs.length) return new Set();
    const common = objectBatchCapabilities(refs[0]);
    for (const ref of refs.slice(1)) for (const capability of [...common]) if (!objectBatchCapabilities(ref).has(capability)) common.delete(capability);
    return common;
  }

  function isCountryLocked(id) {
    return dependencies.projectState.state.countryOverrides?.[String(id)]?.locked === true;
  }

  function setCountryLockedState(id, locked) {
    const key = String(id || '');
    if (!key || !(0, dependencies.countries.countryFeatureById)(key)) return false;
    const override = { ...(dependencies.projectState.state.countryOverrides[key] || {}) };
    if (locked) override.locked = true;
    else delete override.locked;
    if (Object.keys(override).length) dependencies.projectState.state.countryOverrides[key] = override;
    else delete dependencies.projectState.state.countryOverrides[key];
    return true;
  }

  function lockedCountryIds(ids = []) {
    return [...new Set(ids.map(String).filter(Boolean))].filter(isCountryLocked);
  }

  function requireCountriesUnlocked(ids, action = '편집') {
    const lockedIds = lockedCountryIds(ids);
    if (!lockedIds.length) return true;
    (0, dependencies.feedback.setActionStatus)(`${lockedIds.length}개국 잠금 해제 후 ${action}하세요`, 'error', 3800);
    return false;
  }

  function objectRefLocked(value) {
    const ref = (0, dependencies.selectionServices.normalizeObjectRef)(value);
    if (!ref) return false;
    if (ref.domain === 'territorial') return ref.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY ? isCountryLocked(ref.id) : (0, dependencies.objectPresentation.territorialUnitById)(ref.id)?.properties?.locked === true;
    if (ref.domain === 'distribution') return (0, dependencies.propertyEditingA.distributionLayerById)(ref.id)?.locked === true;
    if (ref.domain === 'generic') return dependencies.projectState.state.genericFeatures.find(item => String(item.id) === ref.id)?.properties?.locked === true;
    if (ref.domain === 'hydro') return (0, dependencies.hydroPresentation.hydroEditById)(ref.id)?.properties?.locked === true;
    return false;
  }

  function objectRefVisible(value) {
    const ref = (0, dependencies.selectionServices.normalizeObjectRef)(value);
    if (ref?.domain === 'hydro' && !(0, dependencies.hydroPresentation.hydroEditById)(ref.id)) {
      const feature = (0, dependencies.hydroModel.hydroFeatureById)(ref.id);
      const id = String(feature?.properties?.pandolab_id || feature?.id || ref.id);
      return dependencies.projectState.state.physicalSettings.hiddenHydroIds?.[id] !== true;
    }
    const group = layerGroupForObjectRef(ref);
    return !!(ref && group && (0, dependencies.layerPresentation.isLayerItemVisible)(group, ref.id));
  }

  function syncBatchBooleanInput(input, values, enabled) {
    if (!input) return;
    const activeCount = values.filter(Boolean).length;
    input.disabled = !enabled;
    input.checked = enabled && values.length > 0 && activeCount === values.length;
    input.indeterminate = enabled && activeCount > 0 && activeCount < values.length;
    input.setAttribute('aria-checked', input.indeterminate ? 'mixed' : String(input.checked));
    const option = input.closest('.multi-property-option');
    if (option) {
      if (enabled) delete option.dataset.tooltip;
      else option.dataset.tooltip = '선택한 모든 객체에 공통으로 적용할 수 없습니다.';
    }
  }

  function syncBatchActionAvailability(selection = dependencies.domains.selectionDomain.snapshot().selection) {
    const refs = selection.items || [];
    const capabilities = commonBatchCapabilities(refs);
    syncBatchBooleanInput((0, dependencies.platform.$)('multiPropertiesVisibilityInput'), refs.map(objectRefVisible), capabilities.has('visible'));
    if ((0, dependencies.platform.$)('multiPropertiesColorInput')) (0, dependencies.platform.$)('multiPropertiesColorInput').disabled = !capabilities.has('color');
    if ((0, dependencies.platform.$)('multiPropertiesColorTrigger')) (0, dependencies.platform.$)('multiPropertiesColorTrigger').disabled = !capabilities.has('color');
    const countryOnly = refs.length >= 2 && refs.every(ref => ref.domain === 'territorial' && ref.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY);
    const subunitOnly = refs.length >= 2 && refs.every(ref => ref.domain === 'territorial' && ref.type === 'subunit');
    const subunitPolicy = subunitOnly ? subunitSelectionPolicy(refs.map(ref => (0, dependencies.objectPresentation.territorialUnitById)(ref.id)), {
      adjacent: (a, b) => globalThis.PandoLabTerritorialEdit.createKernel(window.polygonClipping).adjacent(a.geometry, b.geometry),
    }) : null;
    (0, dependencies.platform.$)('multiCountryActions')?.classList.toggle('hidden', !countryOnly && !subunitOnly);
    const mergeButton = (0, dependencies.platform.$)('multiSubunitMergeBtn');
    if (mergeButton) { mergeButton.hidden = !subunitOnly; mergeButton.disabled = !subunitPolicy?.valid; }
    const title = (0, dependencies.platform.$)('multiBorderEditBtn')?.querySelector('strong');
    if (title) title.textContent = subunitOnly ? '경계 조정' : '국경 조정';
    const detail = (0, dependencies.platform.$)('multiBorderEditBtn')?.querySelector('small');
    if (detail) detail.textContent = subunitOnly ? '선택 하위단위 사이의 공유 경계 편집' : '선택 국가 사이의 공유국경 편집';
    const borderButton = (0, dependencies.platform.$)('multiBorderEditBtn');
    const borderHelp = (0, dependencies.platform.$)('multiBorderEditHelp');
    if (countryOnly) {
      const analysis = (0, dependencies.geometryOperations.boundaryEditSelectionAnalysis)(refs.map(ref => ref.id));
      const lockedIds = refs.map(ref => ref.id).filter(isCountryLocked);
      if (borderButton) {
        borderButton.disabled = lockedIds.length > 0 || !analysis.valid;
        borderButton.dataset.tooltip = lockedIds.length
          ? '잠긴 국가를 해제한 뒤 국경을 조정하세요.'
          : (analysis.message || '선택 후 공유국경을 확인합니다.');
      }
      if (borderHelp) {
        const message = lockedIds.length
          ? `잠긴 국가 ${lockedIds.length}개를 해제해야 국경을 조정할 수 있습니다.`
          : (analysis.message || '국경 조정 시작 시 공유국경을 확인합니다.');
        borderHelp.textContent = message;
        borderHelp.classList.toggle('hidden', !message);
      }
    } else {
      if (borderButton) {
        borderButton.disabled = true;
        delete borderButton.dataset.tooltip;
      }
      if (borderHelp) {
        borderHelp.textContent = '';
        borderHelp.classList.add('hidden');
      }
    }
    if (subunitOnly) {
      if (borderButton) borderButton.disabled = !subunitPolicy.valid;
      if (borderHelp) { borderHelp.textContent = subunitPolicy.message; borderHelp.classList.remove('hidden'); }
    }
    syncObjectActionsMenu();
  }

  function batchSetVisibility(nextVisible = null) {
    const refs = dependencies.domains.selectionDomain.snapshot().selection.items;
    if (dependencies.projectState.state.projectReplacing || !refs.length || refs.some(ref => !objectRefExists(ref)) || !commonBatchCapabilities(refs).has('visible')) return;
    const allVisible = refs.every(objectRefVisible);
    const visible = typeof nextVisible === 'boolean' ? nextVisible : !allVisible;
    if (refs.every(ref => objectRefVisible(ref) === visible)) return;
    for (const ref of refs) {
      if (ref.domain === 'hydro' && !(0, dependencies.hydroPresentation.hydroEditById)(ref.id)) {
        const feature = (0, dependencies.hydroModel.hydroFeatureById)(ref.id);
        const id = String(feature.properties?.pandolab_id || feature.id);
        const hidden = dependencies.projectState.state.physicalSettings.hiddenHydroIds ||= {};
        if (visible) delete hidden[id];
        else hidden[id] = true;
        continue;
      }
      const group = layerGroupForObjectRef(ref);
      if (!group) continue;
      dependencies.projectState.state.itemVisibility[group] ||= {};
      if (visible) delete dependencies.projectState.state.itemVisibility[group][ref.id];
      else dependencies.projectState.state.itemVisibility[group][ref.id] = false;
    }
    if (refs.some(ref => ref.domain === 'territorial' && ref.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY)) {
      dependencies.rendering.gpuMapRenderer.invalidateCountryPalette({ base: true, emphasis: true }, 'batch-country-visibility');
    }
    if (refs.some(ref => ref.domain === 'hydro')) dependencies.rendering.gpuMapRenderer.invalidateHydroVisibility();
    if (refs.some(ref => ref.domain === 'distribution')) dependencies.distributionPresentation.bumpVisibilityRevision();
    (0, dependencies.layers.markLayerTreeDirty)();
    dependencies.domains.renderingDomain?.invalidateBaseScene?.('object-visibility');
    dependencies.domains.renderingDomain?.invalidateLabels?.('object-visibility');
    dependencies.domains.renderingDomain?.invalidateSelection?.('object-visibility');
    dependencies.domains.projectDomain.queuePresentationAutosave();
    syncBatchActionAvailability();
  }

  function batchSetLocked(nextLocked = null) {
    const refs = dependencies.domains.selectionDomain.snapshot().selection.items;
    if (!refs.length || refs.some(ref => !objectRefExists(ref)) || !commonBatchCapabilities(refs).has('lock')) return;
    const locked = typeof nextLocked === 'boolean' ? nextLocked : !refs.every(objectRefLocked);
    if (refs.every(ref => objectRefLocked(ref) === locked)) return;
    dependencies.domains.projectDomain.recordHistory({ type: 'batch-lock', description: `${refs.length}개 객체 ${locked ? '잠금' : '잠금 해제'}`, affectedIds: refs.map(ref => ref.id) });
    for (const ref of refs) {
      if (ref.domain === 'territorial' && ref.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY) setCountryLockedState(ref.id, locked);
      else if (ref.domain === 'territorial') (0, dependencies.objectPresentation.territorialUnitById)(ref.id).properties.locked = locked;
      else if (ref.domain === 'distribution') (0, dependencies.propertyEditingA.distributionLayerById)(ref.id).locked = locked;
      else if (ref.domain === 'generic') {
        const feature = dependencies.projectState.state.genericFeatures.find(item => String(item.id) === ref.id);
        if (feature) feature.properties.locked = locked;
      } else if (ref.domain === 'hydro') {
        const feature = (0, dependencies.hydroPresentation.hydroEditById)(ref.id);
        if (feature) feature.properties.locked = locked;
      }
    }
    dependencies.projectState.state.stateRevision += 1;
    dependencies.domains.layerTreeController?.syncLocks(refs);
    dependencies.domains.renderingDomain?.invalidateSelection?.('batch-lock');
    dependencies.domains.projectDomain.queueAutosave();
    const primary = dependencies.domains.selectionDomain.primary();
    if (primary) dependencies.domains.selectionUiController.presentPrimary({ refreshOnly: true });
    syncBatchActionAvailability();
  }

  function batchToggleLocked() {
    return batchSetLocked();
  }

  function closeObjectActionsMenu({ restoreFocus = false } = {}) {
    const menu = (0, dependencies.platform.$)('objectActionsMenu');
    if (!menu) return;
    const wasOpen = !menu.classList.contains('hidden');
    const trigger = objectActionsMenuTrigger;
    menu.classList.add('hidden');
    menu.style.removeProperty('left');
    menu.style.removeProperty('top');
    trigger?.setAttribute('aria-expanded', 'false');
    objectActionsMenuTrigger = null;
    if (restoreFocus && wasOpen && trigger?.isConnected) trigger.focus({ preventScroll: true });
  }

  function syncObjectActionsMenu() {
    const refs = dependencies.domains.selectionDomain.snapshot().selection.items;
    const primary = dependencies.domains.selectionDomain.primary();
    const capabilities = commonBatchCapabilities(refs);
    const canSetVisibility = refs.length > 0 && capabilities.has('visible') && refs.every(objectRefExists) && !dependencies.projectState.state.projectReplacing;
    const visibleCount = refs.filter(objectRefVisible).length;
    const visibilityButton = (0, dependencies.platform.$)('objectVisibilityBtn');
    if (visibilityButton) {
      const allVisible = visibleCount === refs.length;
      const label = refs.length > 1 ? allVisible ? '선택한 객체 모두 숨기기' : '선택한 객체 모두 표시' : allVisible ? '객체 숨기기' : '객체 표시';
      visibilityButton.disabled = !canSetVisibility;
      visibilityButton.setAttribute('aria-label', label);
      visibilityButton.setAttribute('aria-pressed', refs.length && visibleCount > 0 && !allVisible ? 'mixed' : String(refs.length > 0 && allVisible));
      visibilityButton.dataset.tooltip = label;
      (0, dependencies.platform.$)('objectVisibilityIcon')?.setAttribute('href', !refs.length || visibleCount ? '#icon-eye' : '#icon-eye-off');
    }
    const locked = refs.length > 0 && refs.every(objectRefLocked);
    const canLock = refs.length > 0 && capabilities.has('lock');
    const canDelete = refs.length > 1
      ? capabilities.has('delete')
      : !!primary && (primary.domain !== 'hydro' || !!(0, dependencies.hydroPresentation.hydroEditById)(primary.id));
    const deleteDisabled = !canDelete || !!(primary && objectRefLocked(primary));
    const lockLabel = locked ? '잠금 해제' : refs.length > 1 ? '모두 잠금' : '잠금';
    const status = (0, dependencies.platform.$)('editorObjectStatus');
    if (status) {
      const lockedCount = refs.filter(objectRefLocked).length;
      const lockStatus = refs.length > 1
        ? (lockedCount === refs.length ? '모두 잠김' : lockedCount ? '일부 잠김' : '')
        : (primary && objectRefLocked(primary) ? '잠김' : '');
      const visibilityStatus = refs.length && !visibleCount ? '숨김' : visibleCount < refs.length ? '일부 숨김' : '';
      const statusText = [visibilityStatus, lockStatus].filter(Boolean).join(' · ');
      status.textContent = statusText;
      status.classList.toggle('hidden', !statusText);
    }
    const focusButton = (0, dependencies.platform.$)('focusSelectedObjectBtn');
    if (focusButton) focusButton.classList.toggle('hidden', refs.length !== 1 || !primary);
    const flagButton = (0, dependencies.platform.$)('flagMenuBtn');
    if (flagButton) {
      const singleTerritorial = refs.length === 1
        && primary?.domain === 'territorial'
        && [dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY, dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.SUBUNIT, dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.REGION].includes(primary.type);
      flagButton.classList.toggle('hidden', !singleTerritorial);
      flagButton.disabled = !singleTerritorial || objectRefLocked(primary);
      if (!singleTerritorial || flagButton.disabled) (0, dependencies.platform.$)('flagMenu')?.hidePopover();
    }
    const lockButton = (0, dependencies.platform.$)('objectLockBtn');
    if (lockButton) {
      lockButton.disabled = !canLock;
      lockButton.setAttribute('aria-pressed', String(locked));
      lockButton.setAttribute('aria-label', lockLabel);
      lockButton.dataset.tooltip = lockLabel;
      (0, dependencies.platform.$)('objectLockIcon')?.setAttribute('href', locked ? '#icon-lock-closed' : '#icon-lock-open');
    }
    const deleteButton = (0, dependencies.platform.$)('objectDeleteBtn');
    if (deleteButton) {
      deleteButton.disabled = deleteDisabled;
      deleteButton.dataset.tooltip = deleteDisabled && canDelete ? '잠금 해제 후 삭제' : '삭제';
      deleteButton.setAttribute('aria-label', deleteButton.dataset.tooltip);
    }
    const menuFocus = (0, dependencies.platform.$)('objectFocusMenuBtn');
    if (menuFocus) menuFocus.disabled = refs.length !== 1 || !primary;
    dependencies.domainControllers.syncSelectionToolbarInteraction?.();
  }

  function positionObjectActionsMenu(trigger) {
    const menu = (0, dependencies.platform.$)('objectActionsMenu');
    if (!menu || !trigger?.isConnected || menu.classList.contains('hidden')) return;
    const rootStyle = getComputedStyle(document.documentElement);
    const edge = Number.parseFloat(rootStyle.getPropertyValue('--ui-popover-screen-edge')) || 8;
    const gap = Number.parseFloat(rootStyle.getPropertyValue('--ui-space-1')) || 4;
    const viewport = window.visualViewport;
    const viewportLeft = viewport?.offsetLeft || 0;
    const viewportTop = viewport?.offsetTop || 0;
    const viewportRight = viewportLeft + (viewport?.width || window.innerWidth);
    const viewportBottom = viewportTop + (viewport?.height || window.innerHeight);
    const triggerRect = trigger.getBoundingClientRect();
    const menuRect = menu.getBoundingClientRect();
    const minLeft = viewportLeft + edge;
    const maxLeft = Math.max(minLeft, viewportRight - menuRect.width - edge);
    const left = (0, dependencies.platform.clamp)(triggerRect.right - menuRect.width, minLeft, maxLeft);
    const belowTop = triggerRect.bottom + gap;
    const aboveTop = triggerRect.top - gap - menuRect.height;
    const preferredTop = belowTop + menuRect.height <= viewportBottom - edge || aboveTop < viewportTop + edge
      ? belowTop
      : aboveTop;
    const minTop = viewportTop + edge;
    const maxTop = Math.max(minTop, viewportBottom - menuRect.height - edge);
    menu.style.left = `${Math.round(left)}px`;
    menu.style.top = `${Math.round((0, dependencies.platform.clamp)(preferredTop, minTop, maxTop))}px`;
  }

  function openObjectActionsMenu(trigger) {
    const menu = (0, dependencies.platform.$)('objectActionsMenu');
    if (!menu || !trigger || !dependencies.domains.selectionDomain.primary()) return;
    const toggleClosed = objectActionsMenuTrigger === trigger && !menu.classList.contains('hidden');
    closeObjectActionsMenu();
    if (toggleClosed) return;
    syncObjectActionsMenu();
    objectActionsMenuTrigger = trigger;
    trigger.setAttribute('aria-expanded', 'true');
    menu.classList.remove('hidden');
    positionObjectActionsMenu(trigger);
    requestAnimationFrame(() => menu.querySelector('[role="menuitem"]:not(.hidden):not(:disabled)')?.focus());
  }

  function deleteSelectedFromObjectMenu() {
    closeObjectActionsMenu();
    const primary = dependencies.domains.selectionDomain.primary();
    if (dependencies.domains.selectionDomain.size() > 1) requestBatchDelete();
    else if (primary?.domain === 'territorial' && primary.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY) (0, dependencies.objectDeletion.requestDeleteCountry)(primary.id);
    else if (primary?.domain === 'territorial') (0, dependencies.objectDeletion.requestTerritorialUnitDivisionRemoval)(primary.id);
    else {
      if (!primary) return;
      const info = objectDisplayInfo(primary);
      (0, dependencies.projectRestore.openConfirmModal)({
        title: `${info.type} 삭제`,
        message: `${info.name} 객체를 지도에서 삭제합니다. 실행취소로 복구할 수 있습니다.`,
        impacts: [`${info.type} 1개 삭제`, '연결된 표시·설정 정리'],
        confirmText: '삭제',
        danger: true,
        onConfirm: dependencies.objectDeletion.deleteSelected,
      });
    }
  }

  function batchSetColor(color) {
    const refs = dependencies.domains.selectionDomain.snapshot().selection.items;
    if (!refs.length || !commonBatchCapabilities(refs).has('color')) return;
    const normalizedColor = (0, dependencies.colorModel.normalizeEditorColor)(color, dependencies.colorModel.DEFAULT_GENERIC_FEATURE_COLOR);
    dependencies.domains.projectDomain.recordHistory({ type: 'batch-color', description: `${refs.length}개 객체 색상 변경`, affectedIds: refs.map(ref => ref.id) });
    for (const ref of refs) {
      if (ref.domain === 'territorial' && ref.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY) {
        dependencies.projectState.state.countryOverrides[ref.id] ||= {};
        (0, dependencies.colorModel.writeDomainColor)(dependencies.colorModel.COLOR_DOMAINS.COUNTRY, {
          feature: (0, dependencies.countries.countryFeatureById)(ref.id), override: dependencies.projectState.state.countryOverrides[ref.id],
        }, normalizedColor, { fallback: (0, dependencies.colorModel.defaultCountryColor)() });
      } else if (ref.domain === 'territorial') {
        const feature = (0, dependencies.objectPresentation.territorialUnitById)(ref.id);
        if (feature) (0, dependencies.colorModel.writeDomainColor)(dependencies.colorModel.COLOR_DOMAINS.TERRITORIAL, { feature }, normalizedColor, { fallback: dependencies.colorModel.DEFAULT_GENERIC_FEATURE_COLOR });
      } else if (ref.domain === 'distribution') {
        const layer = (0, dependencies.propertyEditingA.distributionLayerById)(ref.id);
        if (layer) (0, dependencies.colorModel.writeDomainColor)(dependencies.colorModel.COLOR_DOMAINS.DISTRIBUTION, { layer }, normalizedColor, { fallback: dependencies.colorModel.DEFAULT_GENERIC_FEATURE_COLOR });
      } else if (ref.domain === 'generic') {
        const feature = dependencies.projectState.state.genericFeatures.find(item => String(item.id) === ref.id);
        if (feature) (0, dependencies.colorModel.writeDomainColor)(dependencies.colorModel.COLOR_DOMAINS.GENERIC, { feature }, normalizedColor, { fallback: (0, dependencies.objectModelA.defaultGenericFeatureColor)(feature) });
      } else if (ref.domain === 'hydro') {
        const feature = (0, dependencies.hydroPresentation.hydroEditById)(ref.id);
        if (feature) feature.properties.color = normalizedColor;
      }
    }
    if (refs.some(ref => ref.domain === 'territorial' && ref.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY)) {
      dependencies.rendering.gpuMapRenderer.invalidateCountryPalette({ base: true, emphasis: true }, 'batch-country-color');
    }
    (0, dependencies.layers.markLayerTreeDirty)();
    dependencies.domains.renderingDomain?.invalidateBaseScene?.('batch-country-color');
    dependencies.domains.projectDomain.queueAutosave();
    syncBatchActionAvailability();
  }

  function requestBatchDelete() {
    const refs = dependencies.domains.selectionDomain.snapshot().selection.items;
    if (!refs.length || !commonBatchCapabilities(refs).has('delete')) return;
    const typeCounts = new Map();
    for (const ref of refs) {
      const type = objectDisplayInfo(ref).type;
      typeCounts.set(type, (typeCounts.get(type) || 0) + 1);
    }
    (0, dependencies.projectRestore.openConfirmModal)({
      title: `${refs.length}개 객체 삭제`,
      message: '선택한 객체와 직접 연결된 데이터가 함께 삭제됩니다. 한 번의 실행취소로 복구할 수 있습니다.',
      impacts: [...typeCounts].map(([type, count]) => `${type} ${count}개 삭제`),
      confirmText: '선택 객체 삭제',
      danger: true,
      onConfirm: () => {
        if (refs.some(ref => !objectRefExists(ref) || objectRefLocked(ref)) || !commonBatchCapabilities(refs).has('delete')) {
          (0, dependencies.feedback.setActionStatus)('객체의 잠금 또는 자식 관계가 바뀌어 삭제를 중단했습니다.', 'error', 3600);
          return false;
        }
        const snapshot = (0, dependencies.snapshots.snapshotEditable)();
        try {
          const removedDistributionIds = new Set(refs.filter(ref => ref.domain === 'distribution').map(ref => ref.id));
          const removedHydroEditIds = new Set(refs.filter(ref => ref.domain === 'hydro').map(ref => ref.id));
          const removedGenericFeatureIds = new Set(refs.filter(ref => ref.domain === 'generic').map(ref => ref.id));
          const removedLabelIds = new Set(refs.filter(ref => ref.domain === 'label').map(ref => ref.id));
          const removedUnitIds = new Set(refs.filter(ref => ref.domain === 'territorial' && ref.type !== dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY).map(ref => ref.id));
          dependencies.projectState.state.distributionLayers = dependencies.projectState.state.distributionLayers.filter(layer => !removedDistributionIds.has(String(layer.id)));
          dependencies.projectState.state.distributionEntries = dependencies.projectState.state.distributionEntries.filter(entry => !removedDistributionIds.has(String(entry.layerId))
            && (entry.mode !== dependencies.territorialModel.DISTRIBUTION_MODES.TERRITORIAL || !removedUnitIds.has(String(entry.territorialUnitId))));
          const restoredHydroSourceIds = dependencies.projectState.state.hydroEdits.filter(feature => removedHydroEditIds.has(String(feature.id))).map(feature => String(feature.properties?.sourceFeatureId || '')).filter(Boolean);
          dependencies.projectState.state.hydroEdits = dependencies.projectState.state.hydroEdits.filter(feature => !removedHydroEditIds.has(String(feature.id)));
          for (const sourceId of restoredHydroSourceIds) {
            if (!dependencies.projectState.state.hydroEdits.some(feature => String(feature.properties?.sourceFeatureId || '') === sourceId)) delete dependencies.projectState.state.physicalSettings.hiddenHydroIds[sourceId];
          }
          if (restoredHydroSourceIds.length) dependencies.rendering.gpuMapRenderer.invalidateHydroVisibility();
          for (const layer of dependencies.projectState.state.distributionLayers) if (removedDistributionIds.has(String(layer.parentId))) layer.parentId = '';
          (0, dependencies.landRelations.reassignGenericFeatureParents)([...removedGenericFeatureIds]);
          dependencies.projectState.state.genericFeatures = dependencies.projectState.state.genericFeatures.filter(feature => !removedGenericFeatureIds.has(String(feature.id)));
          dependencies.projectState.state.labels = dependencies.projectState.state.labels.filter(label => !removedLabelIds.has(String(label.id)));
          for (const id of removedLabelIds) delete dependencies.projectState.state.labelSettings[(0, dependencies.labelPresentation.labelKey)('label', id)];
          removeTerritorialUnits(dependencies.projectState.state, removedUnitIds, dependencies.territorialModel.DISTRIBUTION_MODES.TERRITORIAL);
          dependencies.projectState.state.stateRevision += 1;
          dependencies.domains.renderingDomain?.invalidateTerritorialPatch?.('batch-delete');
          dependencies.domains.selectionDomain.clear({ reason: 'batch-delete-clear' });
          dependencies.domainControllers.objectPropertyController?.show(null);
          (0, dependencies.layers.markLayerTreeDirty)();
          dependencies.domains.renderingDomain?.invalidateOverlayGeometry?.('batch', 'batch-delete');
          dependencies.domains.renderingDomain?.invalidateSelection?.('batch-delete');
          dependencies.domains.renderingDomain?.invalidateLabels?.('batch-delete');
          dependencies.domains.projectDomain.commitHistorySnapshot(snapshot);
          dependencies.domains.projectDomain.queueAutosave();
          (0, dependencies.feedback.setActionStatus)(`${refs.length}개 객체 삭제 완료`, 'success', 2800);
        } catch (error) {
          (0, dependencies.projectSnapshots.restoreEditable)(snapshot);
          (0, dependencies.feedback.setActionStatus)(error.message || '삭제를 적용하지 못해 전체 변경을 복구했습니다.', 'error', 4000);
          return false;
        }
      },
    });
  }

  function initializeObjectActionsMenuTrigger() {
    (objectActionsMenuTrigger = null);




  }

  return Object.freeze({
    connect,
    initializeObjectActionsMenuTrigger,
    get baseSvg() { return baseSvg; },
    set baseSvg(value) { baseSvg = value; },
    get batchSetColor() { return batchSetColor; },
    get batchSetVisibility() { return batchSetVisibility; },
    get batchToggleLocked() { return batchToggleLocked; },
    get closeObjectActionsMenu() { return closeObjectActionsMenu; },
    get countryObjectRef() { return countryObjectRef; },
    get deleteSelectedFromObjectMenu() { return deleteSelectedFromObjectMenu; },
    get flatOceanLayer() { return flatOceanLayer; },
    set flatOceanLayer(value) { flatOceanLayer = value; },
    get focusObjectRef() { return focusObjectRef; },
    get isCountryLocked() { return isCountryLocked; },
    get layerItemObjectRef() { return layerItemObjectRef; },
    get objectDisplayInfo() { return objectDisplayInfo; },
    get objectRefExists() { return objectRefExists; },
    get objectRefLocked() { return objectRefLocked; },
    get objectRefVisible() { return objectRefVisible; },
    get openObjectActionsMenu() { return openObjectActionsMenu; },
    get requestBatchDelete() { return requestBatchDelete; },
    get requireCountriesUnlocked() { return requireCountriesUnlocked; },
    get setCountryLockedState() { return setCountryLockedState; },
    get syncBatchActionAvailability() { return syncBatchActionAvailability; },
    get syncObjectActionsMenu() { return syncObjectActionsMenu; },
  });
}
