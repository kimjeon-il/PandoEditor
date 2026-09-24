import { territorialDeletionAllowed, removeTerritorialUnits } from './territorial-interaction-policy.js';
/** ObjectDeletion: extracted application responsibility.
 * Dependencies are explicitly wired once by the composition modules.
 * Mutable bindings stay local; exported accessors retain live identity.
 */
export function createObjectDeletion() {
  let dependencies;

  function connect(ports) {
    if (dependencies) throw new Error('object-deletion already connected');
    dependencies = ports;
  }

  function requestDeleteCountry(id) {
    const key = String(id);
    if (!(0, dependencies.objectOperationsB.requireCountriesUnlocked)([key], '삭제')) return;
    const feature = (0, dependencies.countries.countryFeatureById)(key);
    if (!feature) return;
    const children = dependencies.presentation.territorialRepository.children(key);
    if (children.length) {
      (0, dependencies.feedback.setActionStatus)(`하위 영역 ${children.length}개를 먼저 옮기거나 삭제하세요.`, 'error', 4400);
      return;
    }
    const name = (0, dependencies.presentation.countryName)(feature);
    (0, dependencies.projectRestore.openConfirmModal)({
      title: '국가 삭제',
      message: `${name} 국가 객체를 지도에서 삭제합니다. 실행취소로 복구할 수 있습니다.`,
      impacts: ['국가 1개 삭제', '국가명 제거', '하위 영역 없음'],
      confirmText: '국가 삭제',
      danger: true,
      onConfirm: () => {
        dependencies.domains.projectDomain.recordHistory();
        for (const unit of dependencies.projectState.state.territorialUnits) {
          if (String(unit.properties?.sovereignId || '') !== key) continue;
          unit.properties.sovereignId = '';
          unit.properties.parentId = '';
        }
        dependencies.projectState.state.countriesData.features = dependencies.projectState.state.countriesData.features.filter(f => String(f.id) !== key);
        delete dependencies.projectState.state.countryOverrides[key];
        (0, dependencies.geometryMutation.reindexCountries)(dependencies.projectState.state.countriesData, true);
        (0, dependencies.spatialQuery.markCountryGeometriesChanged)([key]);
        dependencies.projectState.state.boundaryPreparation?.cancel();
        dependencies.projectState.state.boundaryPreparation = null;
        if ((dependencies.projectState.state.selected?.domain === 'territorial' && dependencies.projectState.state.selected.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY) && String(dependencies.projectState.state.selected.id) === key) dependencies.domains.selectionUiController.clear({ reason: 'country-delete-selection-clear' });
        else {
          (0, dependencies.layers.markLayerTreeDirty)();
          dependencies.domains.renderingDomain?.invalidateCountryPatch?.('country-deleted');
        }
        dependencies.domains.projectDomain.queueAutosave();
        (0, dependencies.feedback.setActionStatus)(`${name} 국가를 삭제했습니다.`, 'success');
      },
    });
  }

  function removeGenericFeatureById(id, statusText = '') {
    const key = String(id);
    const feature = dependencies.objectModelA.genericFeatureService.get(key);
    if (!feature) return false;
    const result = dependencies.objectModelA.genericFeatureService.remove(key, {
      beforeRemove: () => (0, dependencies.landRelations.reassignGenericFeatureParents)([key]),
    });
    if (!result.ok) return false;
    (0, dependencies.layers.markLayerTreeDirty)();
    if (dependencies.projectState.state.selected?.domain === 'generic' && String(dependencies.projectState.state.selected.id) === key) dependencies.domains.selectionUiController.clear({ reason: 'generic-delete-selection-clear' });
    (0, dependencies.feedback.setActionStatus)(statusText || `${(0, dependencies.objectPresentation.genericFeatureName)(feature)} 기타 객체를 삭제했습니다.`, 'success');
    return true;
  }

  function removeHydroEditById(id, statusText = '') {
    const key = String(id);
    const feature = (0, dependencies.hydroPresentation.hydroEditById)(key);
    if (!feature) return false;
    dependencies.domains.projectDomain.recordHistory();
    dependencies.projectState.state.hydroEdits = dependencies.projectState.state.hydroEdits.filter(candidate => String(candidate.id) !== key);
    const sourceId = String(feature.properties?.sourceFeatureId || '');
    if (sourceId && !dependencies.projectState.state.hydroEdits.some(candidate => String(candidate.properties?.sourceFeatureId || '') === sourceId)) {
      delete dependencies.projectState.state.physicalSettings.hiddenHydroIds[sourceId];
      dependencies.rendering.gpuMapRenderer.invalidateHydroVisibility();
    }
    (0, dependencies.layers.markLayerTreeDirty)();
    if (dependencies.projectState.state.selected?.domain === 'hydro' && String(dependencies.projectState.state.selected.id) === key) dependencies.domains.selectionUiController.clear({ reason: 'hydro-delete-selection-clear' });
    dependencies.domains.renderingDomain?.invalidateHydroPatch?.('hydro-feature-deleted');
    dependencies.domains.projectDomain.queueAutosave();
    const category = (0, dependencies.hydroPresentation.hydroCategoryLabel)(feature.properties?.category);
    const fallback = (0, dependencies.hydroPresentation.hydroFallbackName)(feature.properties?.category);
    (0, dependencies.feedback.setActionStatus)(statusText || `${(0, dependencies.hydroPresentation.hydroEditorName)(feature.properties?.name, fallback)}${(0, dependencies.objectModelA.hydroAccusativeLabel)(feature.properties?.category).slice(category.length)} 삭제했습니다.`, 'success');
    return true;
  }

  function removeLabelById(id, statusText = '') {
    const key = String(id);
    const label = dependencies.projectState.state.labels.find(candidate => String(candidate.id) === key);
    if (!label) return false;
    dependencies.domains.projectDomain.recordHistory();
    dependencies.projectState.state.labels = dependencies.projectState.state.labels.filter(candidate => String(candidate.id) !== key);
    delete dependencies.projectState.state.labelSettings[(0, dependencies.labelPresentation.labelKey)('label', key)];
    (0, dependencies.layers.markLayerTreeDirty)();
    if (dependencies.projectState.state.selected?.domain === 'label' && String(dependencies.projectState.state.selected.id) === key) dependencies.domains.selectionUiController.clear({ reason: 'label-delete-selection-clear' });
    dependencies.domains.renderingDomain?.invalidateLabels?.('label-deleted');
    dependencies.domains.projectDomain.queueAutosave();
    (0, dependencies.feedback.setActionStatus)(statusText || `${label.name || '지명'} 지명을 삭제했습니다.`, 'success');
    return true;
  }

  function requestExplicitTerritorialUnitDelete(feature) {
    const children = (0, dependencies.territorialServicesA.territorialChildren)(dependencies.projectState.state.territorialUnits, feature.id);
    if (children.length) {
      (0, dependencies.feedback.setActionStatus)(`하위 영역 ${children.length}개를 먼저 다른 부모로 옮기거나 삭제해야 합니다.`, 'error', 4200);
      return false;
    }
    (0, dependencies.projectRestore.openConfirmModal)({
      title: `${(0, dependencies.territorialServicesB.territorialTypeLabel)(feature.properties.unitType)} 삭제`,
      message: `${(0, dependencies.objectPresentation.territorialUnitName)(feature)}을(를) 프로젝트에서 삭제합니다. 국가나 다른 영역의 형상은 변경하지 않습니다.`,
      impacts: [`${(0, dependencies.territorialServicesB.territorialTypeLabel)(feature.properties.unitType)} 1개 삭제`, '국가 및 다른 영역 형상 변경 없음'],
      confirmText: `${(0, dependencies.territorialServicesB.territorialTypeLabel)(feature.properties.unitType)} 삭제`,
      danger: true,
      onConfirm: () => {
        const current = (0, dependencies.objectPresentation.territorialUnitById)(feature.id);
        if (!territorialDeletionAllowed([current], dependencies.projectState.state.territorialUnits)) return false;
        const snapshot = (0, dependencies.snapshots.snapshotEditable)();
        try {
          removeTerritorialUnits(dependencies.projectState.state, [feature.id], dependencies.territorialModel.DISTRIBUTION_MODES.TERRITORIAL);
          (0, dependencies.layers.markLayerTreeDirty)();
          if ((dependencies.projectState.state.selected?.domain === 'territorial' && dependencies.projectState.state.selected.type !== dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY) && String(dependencies.projectState.state.selected.id) === String(feature.id)) dependencies.domains.selectionUiController.clear({ reason: 'territorial-delete-selection-clear' });
          dependencies.domains.renderingDomain?.invalidateTerritorialPatch?.('territorial-unit-deleted');
          dependencies.domains.projectDomain.commitHistorySnapshot(snapshot);
          dependencies.projectState.state.stateRevision += 1;
          dependencies.domains.projectDomain.queueAutosave();
          (0, dependencies.feedback.setActionStatus)(`${(0, dependencies.objectPresentation.territorialUnitName)(feature)}을(를) 삭제했습니다.`, 'success');
        } catch (error) {
          (0, dependencies.projectSnapshots.restoreEditable)(snapshot);
          (0, dependencies.feedback.reportOperationError)(error, '삭제를 적용하지 못해 변경을 되돌렸습니다.', 'PL-SUBUNIT-DELETE', 4200);
          return false;
        }
      },
    });
    return true;
  }

  function requestTerritorialUnitDivisionRemoval(id) {
    const feature = (0, dependencies.objectPresentation.territorialUnitById)(id);
    if (!feature) return false;
    if (feature.properties?.locked) {
      (0, dependencies.feedback.setActionStatus)('잠금을 해제한 뒤 영역을 삭제할 수 있습니다.', 'error', 3200);
      return false;
    }
    return requestExplicitTerritorialUnitDelete(feature);
  }

  function deleteTerritorialUnit(type, id) {
    if (type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY) {
      requestDeleteCountry(id);
      return true;
    }
    const feature = (0, dependencies.objectPresentation.territorialUnitById)(id);
    if (!feature || feature.properties?.unitType !== type) return false;
    if ((0, dependencies.territorialServicesA.territorialChildren)(dependencies.projectState.state.territorialUnits, feature.id).length) {
      (0, dependencies.feedback.setActionStatus)('하위 영역을 먼저 다른 부모로 옮기거나 삭제해야 합니다.', 'error', 4200);
      return false;
    }
    requestTerritorialUnitDivisionRemoval(id);
    return true;
  }

  function deleteSelected() {
    if (!(0, dependencies.readinessUi.requireCanonicalData)()) return;
    if (!dependencies.projectState.state.selected) {
      (0, dependencies.feedback.setActionStatus)('삭제할 객체를 선택하세요', 'error');
      return;
    }
    if ((dependencies.projectState.state.selected.domain === 'territorial' && dependencies.projectState.state.selected.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY)) {
      deleteTerritorialUnit(dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY, dependencies.projectState.state.selected.id);
      return;
    }
    if (dependencies.projectState.state.selected.domain === 'hydro') {
      const feature = (0, dependencies.hydroModel.hydroFeatureById)(dependencies.projectState.state.selected.id);
      const category = (0, dependencies.hydroPresentation.hydroCategoryLabel)(feature?.properties?.category);
      if ((0, dependencies.hydroPresentation.hydroEditById)(dependencies.projectState.state.selected.id)) removeHydroEditById(dependencies.projectState.state.selected.id, `선택한 ${(0, dependencies.objectModelA.hydroAccusativeLabel)(feature?.properties?.category)} 삭제했습니다.`);
      else (0, dependencies.feedback.setActionStatus)(`내장 ${category}는 삭제할 수 없습니다. 편집용 복사본을 만들어 수정하세요.`, 'error', 3400);
      return;
    }
    if (dependencies.projectState.state.selected.domain === 'generic') {
      removeGenericFeatureById(dependencies.projectState.state.selected.id, '선택한 객체를 삭제했습니다.');
    } else if (dependencies.projectState.state.selected.domain === 'distribution') {
      (0, dependencies.propertyEditingA.deleteDistributionLayer)(dependencies.projectState.state.selected.id, { confirm: false });
    } else if ((dependencies.projectState.state.selected.domain === 'territorial' && dependencies.projectState.state.selected.type !== dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY)) {
      deleteTerritorialUnit(dependencies.projectState.state.selected.type, dependencies.projectState.state.selected.id);
    } else if (dependencies.projectState.state.selected.domain === 'label') {
      removeLabelById(dependencies.projectState.state.selected.id, '선택한 객체를 삭제했습니다.');
    }
  }



  return Object.freeze({
    connect,

    get deleteSelected() { return deleteSelected; },
    get requestDeleteCountry() { return requestDeleteCountry; },
    get requestTerritorialUnitDivisionRemoval() { return requestTerritorialUnitDivisionRemoval; },
  });
}
