/** TerritorialConversion: extracted application responsibility.
 * Dependencies are explicitly wired once by the composition modules.
 * Mutable bindings stay local; exported accessors retain live identity.
 */
export function createTerritorialConversion() {
  let dependencies;
  let territorialTypeSource;
  function connect(ports) {
    if (dependencies) throw new Error('territorial-conversion already connected');
    dependencies = ports;
  }

  async function promoteTerritorialUnitToCountry(unitId) {
    const source = (0, dependencies.objectPresentation.territorialUnitById)(unitId);
    const sourceCountryId = String(source?.properties?.sovereignId || '');
    const sourceCountry = (0, dependencies.countries.countryFeatureById)(sourceCountryId);
    const name = String(source?.properties?.name || '').trim();
    if (!source || !sourceCountry || !name) {
      (0, dependencies.feedback.setActionStatus)('새 국가로 독립하려면 이름과 소속 국가가 있는 하위단위를 선택하세요.', 'error', 3800);
      return false;
    }
    if (source.properties?.locked) {
      (0, dependencies.feedback.setActionStatus)('영역 잠금을 해제한 뒤 국가로 전환하세요.', 'error', 3400);
      return false;
    }
    if (!(0, dependencies.objectOperationsB.requireCountriesUnlocked)([sourceCountryId], '하위단위를 국가로 전환')) return false;
    if ((0, dependencies.countries.countryFeatureById)(source.id)) {
      (0, dependencies.feedback.setActionStatus)('영역 ID가 국가 ID와 겹칩니다. ID를 바꾸세요.', 'error', 4200);
      return false;
    }
    const descendantIds = new Set();
    const queue = [String(source.id)];
    while (queue.length) {
      const parentId = queue.shift();
      for (const child of (0, dependencies.territorialServicesA.territorialChildren)(dependencies.projectState.state.territorialUnits, parentId)) {
        if (descendantIds.has(String(child.id))) continue;
        descendantIds.add(String(child.id));
        queue.push(String(child.id));
      }
    }
    if (dependencies.projectState.state.territorialUnits.some(unit => descendantIds.has(String(unit.id)) && unit.properties?.locked)) {
      (0, dependencies.feedback.setActionStatus)('잠긴 자식 하위단위를 먼저 잠금 해제하세요.', 'error', 3600);
      return false;
    }
    const convertedMetadata = source.properties?.metadata?.convertedFromCountry || {};
    const country = (0, dependencies.objectPicking.createCountryFeature)(name, [], (0, dependencies.objectModelB.territorialStyleColor)(source) || null, (0, dependencies.countryValidation.snapGeometryToGrid)(source.geometry, 7));
    country.id = String(source.id);
    country.properties = { name };
    const restoredOverride = convertedMetadata.override && typeof convertedMetadata.override === 'object'
      ? (0, dependencies.platform.deepClone)(convertedMetadata.override)
      : {};
    return (0, dependencies.territorialEditingB.previewTerritorialEdit)({
      operation: 'promote', targetId: String(source.id), newCountry: country,
      countryOverride: {
        ...restoredOverride, name,
        color: (0, dependencies.objectModelB.territorialStyleColor)(source) || restoredOverride.color || '',
        notes: String(source.properties?.notes || restoredOverride.notes || ''),
      },
    }, { selectedId: String(source.id) });
  }

  function buildTerritorialStructurePreview({ source, sourceType, targetType, sovereignId = '', parentId = '' } = {}) {
    if (!source || !sourceType || !targetType) return null;
    const sourceName = territorialTypeSourceName(source);
    const sourceLabel = dependencies.applicationConstantsB.TERRITORIAL_TYPE_LABELS[sourceType] || '영역';
    const targetLabel = dependencies.applicationConstantsB.TERRITORIAL_TYPE_LABELS[targetType] || '영역';
    const subjectLabel = ({ 국가: '국가를', 하위단위: '하위단위를', 지방: '지방을' })[sourceLabel] || `${sourceLabel}을`;
    const directionLabel = ({ 국가: '국가로', 하위단위: '하위단위로', 지방: '지방으로' })[targetLabel] || `${targetLabel}으로`;
    const sourceIsCountry = sourceType === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY;
    const targetIsCountry = targetType === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY;
    const childCount = sourceIsCountry
      ? dependencies.projectState.state.territorialUnits.filter(candidate => String(candidate.properties?.sovereignId || '') === String(source.id || '')).length
      : (0, dependencies.territorialServicesA.territorialChildren)(dependencies.projectState.state.territorialUnits, source.id).length;
    const targetCountry = (0, dependencies.countries.countryFeatureById)(sovereignId);
    const targetParent = (0, dependencies.objectPresentation.territorialUnitById)(parentId);
    const impacts = [];
    let summary = `${sourceName}의 종류를 ${targetLabel}(으)로 변경합니다.`;

    if (sourceIsCountry && !targetIsCountry) {
      if (!targetCountry) {
        return {
          title: `${subjectLabel} ${directionLabel} 전환`,
          summary: '소속 국가를 선택해야 변경 결과를 확인할 수 있습니다.',
          impacts: [],
          confirmText: `${directionLabel} 전환`,
          danger: true,
        };
      }
      summary = `${sourceName}의 국토를 ${(0, dependencies.presentation.countryName)(targetCountry)}에 합치고 같은 객체를 ${targetLabel}(으)로 유지합니다.`;
      impacts.push(`${(0, dependencies.presentation.countryName)(targetCountry)} 국경 변경 및 기존 국가 관계 해제`);
    } else if (targetIsCountry) {
      summary = `${sourceName}의 영역을 현재 소속 국가에서 분리해 독립 국가로 전환합니다.`;
      impacts.push('기존 상위 단위와 소속 국가 관계 해제', '기존 국가 국경 변경 및 새 국가 1개 생성');
    } else {
      const sovereign = (0, dependencies.countries.countryFeatureById)(sovereignId || source.properties?.sovereignId);
      impacts.push(`소속 국가 유지${sovereign ? `: ${(0, dependencies.presentation.countryName)(sovereign)}` : ''}`);
      if (targetType === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.SUBUNIT) {
        const parentName = targetParent ? (0, dependencies.objectPresentation.territorialUnitName)(targetParent) : sovereign ? (0, dependencies.presentation.countryName)(sovereign) : '선택한 상위 단위';
        if (String(parentId) !== String(sovereignId)) impacts.push(`상위 단위: ${parentName}`);
      }
    }

    impacts.push('현재 형상과 객체 ID 유지', `하위 영역 ${childCount}개 유지`, '연결된 분포 참조 유지', '한 번의 실행취소로 복구 가능');
    return {
      title: `${subjectLabel} ${directionLabel} 전환`,
      summary,
      impacts,
      confirmText: `${directionLabel} 전환`,
      danger: sourceIsCountry || targetIsCountry,
    };
  }

  function requestTerritorialUnitPromotion(unitId) {
    const feature = (0, dependencies.objectPresentation.territorialUnitById)(unitId);
    if (!feature) return;
    const preview = buildTerritorialStructurePreview({
      source: feature,
      sourceType: feature.properties?.unitType,
      targetType: dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY,
      sovereignId: feature.properties?.sovereignId,
    });
    if (!preview) return;
    (0, dependencies.projectRestore.openConfirmModal)({
      title: preview.title,
      message: preview.summary,
      impacts: preview.impacts,
      confirmText: preview.confirmText,
      danger: preview.danger,
      onConfirm: () => promoteTerritorialUnitToCountry(feature.id),
    });
  }

  function territorialTypeSourceFeature() {
    if (!territorialTypeSource) return null;
    return territorialTypeSource.unitType === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY
      ? (0, dependencies.countries.countryFeatureById)(territorialTypeSource.id)
      : (0, dependencies.objectPresentation.territorialUnitById)(territorialTypeSource.id);
  }

  function territorialTypeSourceName(feature = territorialTypeSourceFeature()) {
    return territorialTypeSource?.unitType === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY
      ? (0, dependencies.presentation.countryName)(feature)
      : (0, dependencies.objectPresentation.territorialUnitName)(feature);
  }

  function territorialTypeParentOptions(source, sovereignId) {
    const choices = (0, dependencies.territorialServicesA.subunitParentChoices)(sovereignId, dependencies.projectState.state.countriesData.features, dependencies.projectState.state.territorialUnits, {
      exclude: [source.id], name: item => item.properties?.unitType ? (0, dependencies.objectPresentation.territorialUnitName)(item) : (0, dependencies.presentation.countryName)(item),
    }).filter(option => option.value === String(sovereignId) || (0, dependencies.objectMetadata.territorialUnitInsideContainer)(source, (0, dependencies.objectPresentation.territorialUnitById)(option.value)));
    const oldParent = String(source.properties?.parentId || '');
    if (oldParent && String(source.properties?.sovereignId || '') === String(sovereignId) && !choices.some(option => option.value === oldParent)) {
      const parent = (0, dependencies.objectPresentation.territorialUnitById)(oldParent) || (0, dependencies.countries.countryFeatureById)(oldParent);
      choices.push({ value: oldParent, label: `${parent?.properties?.name || oldParent} · 기존 소속` });
    }
    return choices;
  }

  function syncTerritorialTypeModal() {
    const source = territorialTypeSourceFeature();
    if (!source) {
      closeTerritorialTypeModal();
      return;
    }
    const sourceType = territorialTypeSource.unitType;
    const targetType = (0, dependencies.platform.$)('territorialTypeInput').value;
    const sourceIsCountry = sourceType === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY;
    const targetIsCountry = targetType === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY;
    const targetIsAdmin = targetType === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.SUBUNIT;
    const sovereignRow = (0, dependencies.platform.$)('territorialTypeSovereignRow');
    let sovereignChoice = { single: false };

    let sovereignId = sourceIsCountry ? (0, dependencies.platform.$)('territorialTypeSovereignInput').value : String(source.properties?.sovereignId || '');
    if (sourceIsCountry && !targetIsCountry) {
      const candidates = (0, dependencies.propertyEditingB.territorialUnitCountryOptions)().filter(option => option.value && option.value !== String(territorialTypeSource.id));
      const options = [{ value: '', label: '소속 국가 선택', placeholder: true }, ...candidates];
      if (!candidates.some(option => option.value === sovereignId)) sovereignId = '';
      sovereignChoice = (0, dependencies.propertyEditingB.replaceSelectOptions)((0, dependencies.platform.$)('territorialTypeSovereignInput'), options, sovereignId, { autoSelectSingle: true });
      sovereignId = sovereignChoice.value;
    }
    sovereignRow.classList.toggle('hidden', !sourceIsCountry || targetIsCountry || sovereignChoice.single);

    const parentRow = (0, dependencies.platform.$)('territorialTypeParentRow');
    parentRow.classList.toggle('hidden', !targetIsAdmin);
    if (targetIsAdmin) {
      const parentCandidates = territorialTypeParentOptions(source, sovereignId);
      const options = parentCandidates.length
        ? parentCandidates
        : [{ value: '', label: '상위 단위 선택', placeholder: true }];
      const currentParent = (0, dependencies.platform.$)('territorialTypeParentInput').value;
      const preferredParent = options.some(option => option.value === currentParent)
        ? currentParent
        : options.some(option => option.value === String(source.properties?.parentId || ''))
          ? String(source.properties.parentId)
          : String(options[0]?.value || '');
      const parentChoice = (0, dependencies.propertyEditingB.replaceSelectOptions)((0, dependencies.platform.$)('territorialTypeParentInput'), options, preferredParent, { autoSelectSingle: true });
      parentRow.classList.toggle('hidden', !(0, dependencies.territorialServicesA.shouldShowTerritorialParentChoice)({
        sovereignId,
        parentId: parentChoice.value,
        options: parentCandidates,
      }));
    }

    const targetCountry = (0, dependencies.countries.countryFeatureById)(sovereignId);
    const preview = buildTerritorialStructurePreview({
      source,
      sourceType,
      targetType,
      sovereignId,
      parentId: (0, dependencies.platform.$)('territorialTypeParentInput').value,
    });
    (0, dependencies.platform.$)('territorialTypeTitle').textContent = preview?.title || '종류 변경';
    (0, dependencies.platform.$)('territorialTypeImpactSummary').textContent = preview?.summary || '';
    (0, dependencies.platform.$)('territorialTypeImpactList').replaceChildren(...(preview?.impacts || []).map(value => {
      const item = document.createElement('li');
      item.textContent = value;
      return item;
    }));
    (0, dependencies.platform.$)('territorialTypeImpact').classList.toggle('danger-preview', preview?.danger === true);
    const confirm = (0, dependencies.platform.$)('territorialTypeConfirmBtn');
    confirm.textContent = preview?.confirmText || '종류 변경';
    confirm.classList.toggle('danger-confirm', preview?.danger === true);
    confirm.disabled = sourceType === targetType || (sourceIsCountry && !targetIsCountry && !targetCountry)
      || (targetIsAdmin && !(0, dependencies.platform.$)('territorialTypeParentInput').value);
  }

  function openTerritorialTypeModal(unitType, id) {
    const source = unitType === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY ? (0, dependencies.countries.countryFeatureById)(id) : (0, dependencies.objectPresentation.territorialUnitById)(id);
    if (!source || ![dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY, dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.SUBUNIT].includes(unitType)) return;
    if (!(0, dependencies.readinessUi.requireCanonicalData)()) return;
    const sourceLocked = unitType === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY ? (0, dependencies.objectOperationsA.isCountryLocked)(id) : source.properties?.locked === true;
    if (sourceLocked) {
      (0, dependencies.feedback.setActionStatus)('잠금을 해제한 뒤 종류를 변경하세요.', 'error', 3400);
      return;
    }
    territorialTypeSource = { unitType, id: String(id) };
    const options = Object.entries(dependencies.applicationConstantsB.TERRITORIAL_TYPE_LABELS)
      .filter(([type]) => [dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY, dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.SUBUNIT].includes(type) && type !== unitType)
      .map(([value, label]) => ({ value, label }));
    const preferred = unitType === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY ? dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.SUBUNIT : dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY;
    const targetChoice = (0, dependencies.propertyEditingB.replaceSelectOptions)((0, dependencies.platform.$)('territorialTypeInput'), options, preferred, { autoSelectSingle: true });
    (0, dependencies.platform.$)('territorialTypeInput').closest('.field-group')?.classList.toggle('hidden', targetChoice.single);
    (0, dependencies.platform.$)('territorialTypeContext').textContent = `${territorialTypeSourceName(source)} · 현재 ${dependencies.applicationConstantsB.TERRITORIAL_TYPE_LABELS[unitType]}`;
    (0, dependencies.platform.$)('territorialTypeSovereignInput').value = '';
    (0, dependencies.platform.$)('territorialTypeParentInput').value = '';
    (0, dependencies.platform.$)('territorialTypeModal').classList.remove('hidden');
    syncTerritorialTypeModal();
    requestAnimationFrame(() => {
      const modal = (0, dependencies.platform.$)('territorialTypeModal');
      const focusTarget = [...(modal?.querySelectorAll?.('select, button') || [])]
        .find(element => !element.disabled && !element.closest('.hidden, [hidden]'));
      focusTarget?.focus();
    });
  }

  function closeTerritorialTypeModal() {
    (0, dependencies.platform.$)('territorialTypeModal')?.classList.add('hidden');
    territorialTypeSource = null;
  }

  async function convertTerritorialUnitType(unitId, targetType, parentId = '') {
    const source = (0, dependencies.objectPresentation.territorialUnitById)(unitId);
    if (!source || ![dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.SUBUNIT].includes(targetType)) return false;
    if (source.properties?.locked) {
      (0, dependencies.feedback.setActionStatus)('잠금을 해제한 뒤 종류를 변경하세요.', 'error', 3200);
      return false;
    }
    const sovereignId = String(source.properties?.sovereignId || '');
    const parent = targetType === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.SUBUNIT
      ? dependencies.presentation.territorialRepository.get(parentId || sovereignId)
      : (0, dependencies.countries.countryFeatureById)(sovereignId);
    if (!parent || (targetType === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.SUBUNIT && !(0, dependencies.objectMetadata.territorialUnitInsideContainer)(source, parent))) {
      (0, dependencies.feedback.setActionStatus)('영역 전체를 포함하는 올바른 상위 단위를 선택하세요.', 'error', 3900);
      return false;
    }
    try {
      await (0, dependencies.objectModelB.runTerritorialUnitTransaction)({
        snapshot: dependencies.snapshots.snapshotEditable,
        calculate: async () => {
          const nextUnits = (0, dependencies.platform.deepClone)(dependencies.projectState.state.territorialUnits);
          const index = nextUnits.findIndex(feature => String(feature.id) === String(unitId));
          if (index < 0) throw new Error('종류를 변경할 영역을 찾을 수 없습니다.');
          const converted = (0, dependencies.applicationServicesA.changeUnitType)(nextUnits[index], targetType);
          converted.properties.parentId = targetType === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.SUBUNIT
            ? String(parentId || sovereignId)
            : sovereignId;
          nextUnits[index] = converted;
          return (0, dependencies.territorialModel.normalizeTerritorialUnits)(nextUnits, { countryExists: id => !!(0, dependencies.countries.countryFeatureById)(id) });
        },
        validate: nextUnits => (0, dependencies.objectModelB.validateTerritorialUnitRelations)(nextUnits, {
          countryExists: id => !!(0, dependencies.countries.countryFeatureById)(id),
          relations: dependencies.projectState.state.territorialRelations,
        }),
        apply: async nextUnits => {
          dependencies.projectState.state.territorialUnits = nextUnits;
          (0, dependencies.landRelations.reconcileTerritorialUnitCompleteness)([sovereignId]);
          dependencies.projectState.state.territorialUnits = (0, dependencies.territorialModel.normalizeTerritorialUnits)(dependencies.projectState.state.territorialUnits, { countryExists: id => !!(0, dependencies.countries.countryFeatureById)(id) });
          (0, dependencies.layers.markLayerTreeDirty)();
          (0, dependencies.propertyEditingA.applyTerritorialUnitSelectionIntent)(unitId, true);
          dependencies.domains.renderingDomain?.invalidateTerritorialPatch?.('territorial-type-converted');
        },
        restore: before => (0, dependencies.projectSnapshots.restoreEditable)(before),
        recordHistory: before => dependencies.domains.projectDomain.commitHistorySnapshot(before),
        autosave: (...args) => dependencies.domains.projectDomain.queueAutosave(...args),
      });
      (0, dependencies.feedback.setActionStatus)(`${(0, dependencies.objectPresentation.territorialUnitName)((0, dependencies.objectPresentation.territorialUnitById)(unitId))}을(를) ${dependencies.applicationConstantsB.TERRITORIAL_TYPE_LABELS[targetType]}(으)로 변경했습니다.`, 'success', 3600);
      return true;
    } catch (error) {
      (0, dependencies.feedback.reportOperationError)(error, '영역 종류를 변경하지 못해 변경을 되돌렸습니다.', 'PL-TYPE-001', 4500);
      return false;
    }
  }

  async function convertCountryToRegionType(countryId, targetType, targetCountryId, parentId = '') {
    const source = (0, dependencies.countries.countryFeatureById)(countryId);
    const target = (0, dependencies.countries.countryFeatureById)(targetCountryId);
    if (!source || !target || source === target || ![dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.SUBUNIT].includes(targetType)) {
      (0, dependencies.feedback.setActionStatus)('종류를 변경할 국가와 소속 국가를 다시 선택하세요.', 'error', 3800);
      return false;
    }
    if (!(0, dependencies.objectOperationsB.requireCountriesUnlocked)([countryId, targetCountryId], '국가 종류를 변경')) return false;
    if (dependencies.projectState.state.territorialUnits.some(feature => String(feature.id) === String(countryId))) {
      (0, dependencies.feedback.setActionStatus)('같은 ID의 영역이 이미 있어 종류를 변경할 수 없습니다.', 'error', 4000);
      return false;
    }
    const parent = targetType === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.SUBUNIT ? dependencies.presentation.territorialRepository.get(parentId || targetCountryId) : target;
    if (!parent || (String(parent.id || '') !== String(targetCountryId) && !(0, dependencies.objectMetadata.territorialUnitInsideContainer)(source, parent))) {
      (0, dependencies.feedback.setActionStatus)('국가 영역 전체를 포함하는 올바른 상위 단위를 선택하세요.', 'error', 3900);
      return false;
    }
    const sourceOverride = (0, dependencies.platform.deepClone)(dependencies.projectState.state.countryOverrides[countryId] || {});
    const sourceProperties = (0, dependencies.platform.deepClone)(source.properties || {});
    const name = (0, dependencies.presentation.countryName)(source);
    const sourceGeometry = (0, dependencies.platform.deepClone)(source.geometry);
    const converted = (0, dependencies.territorialServicesA.createTerritorialFeature)({
      id: (0, dependencies.surfaces.uid)(),
      unitType: targetType,
      name,
      geometry: sourceGeometry,
      parentId: targetType === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.SUBUNIT ? String(parentId || targetCountryId) : String(targetCountryId),
      sovereignId: String(targetCountryId),
      coverageMode: dependencies.territorialModel.TERRITORIAL_COVERAGE_MODES.PARTITION,
      color: String(sourceOverride.color || ''),
      notes: String(sourceOverride.notes || ''),
      metadata: { convertedFromCountry: { countryId: String(countryId), properties: sourceProperties, override: sourceOverride } },
    });
    const snapshot = (0, dependencies.snapshots.snapshotEditable)();
    (0, dependencies.feedback.setActionStatus)(`${name}의 국가 경계를 대상 국가에 합치는 중입니다.`, 'working', 0);
    const result = await (0, dependencies.geometryOperations.transactCountryEdit)({
      operation: 'merge',
      payload: { sourceId: String(targetCountryId), targetIds: [String(countryId)] },
      snapshot,
      applyResult: plan => {
        (0, dependencies.cutOperations.applyWorkerCountryPatches)(plan);
        dependencies.projectState.state.territorialUnits.push(converted);
        for (const feature of dependencies.projectState.state.territorialUnits) {
          if (String(feature.properties?.sovereignId || '') !== String(countryId)) continue;
          feature.properties.sovereignId = String(targetCountryId);
          if (String(feature.properties?.parentId || '') === String(countryId)) feature.properties.parentId = String(converted.id);
        }
        for (const relation of dependencies.projectState.state.territorialRelations) {
          if (String(relation.sovereignId || '') === String(countryId)) relation.sovereignId = String(targetCountryId);
          if (String(relation.parentId || '') === String(countryId)) relation.parentId = String(converted.id);
        }
        for (const entry of dependencies.projectState.state.distributionEntries) {
          if (entry.mode === dependencies.territorialModel.DISTRIBUTION_MODES.TERRITORIAL && String(entry.territorialUnitId) === String(countryId)) entry.territorialUnitId = String(converted.id);
        }
        for (const genericFeature of dependencies.projectState.state.genericFeatures) {
          if (String(genericFeature.properties?.ownerId || '') !== String(countryId)) continue;
          genericFeature.properties.ownerId = String(targetCountryId);
          if (String(genericFeature.properties.topologyGroup || '') === `land:${countryId}`) genericFeature.properties.topologyGroup = `land:${targetCountryId}`;
        }
        dependencies.projectState.state.territorialUnits = (0, dependencies.territorialModel.normalizeTerritorialUnits)(dependencies.projectState.state.territorialUnits, { countryExists: id => !!(0, dependencies.countries.countryFeatureById)(id) });
        (0, dependencies.landRelations.reconcileTerritorialUnitCompleteness)([targetCountryId]);
        dependencies.projectState.state.territorialUnits = (0, dependencies.territorialModel.normalizeTerritorialUnits)(dependencies.projectState.state.territorialUnits, { countryExists: id => !!(0, dependencies.countries.countryFeatureById)(id) });
        const territorialValidation = (0, dependencies.objectModelB.validateTerritorialUnitRelations)(dependencies.projectState.state.territorialUnits, {
          countryExists: id => !!(0, dependencies.countries.countryFeatureById)(id),
          relations: dependencies.projectState.state.territorialRelations,
        });
        if (!territorialValidation.ok) throw new Error(territorialValidation.issues[0] || '영역 관계가 올바르지 않습니다.');
        (0, dependencies.countryValidation.refreshCountryCentroids)(new Set(plan.affectedIds));
        dependencies.projectState.state.boundaryPreparation?.cancel();
        dependencies.projectState.state.boundaryPreparation = null;
        (0, dependencies.layers.markLayerTreeDirty)();
        (0, dependencies.propertyEditingA.applyTerritorialUnitSelectionIntent)(converted.id, true);
        dependencies.domains.renderingDomain?.invalidateCountryPatch?.('country-converted-to-region');
      },
      onSuccess: () => (0, dependencies.feedback.setActionStatus)(`${name}을(를) ${(0, dependencies.presentation.countryName)(target)} 소속 ${dependencies.applicationConstantsB.TERRITORIAL_TYPE_LABELS[targetType]}(으)로 변경했습니다.`, 'success', 4300),
      onError: error => (0, dependencies.feedback.reportOperationError)(error, '국가 종류를 변경하지 못해 변경을 되돌렸습니다.', 'PL-TYPE-002', 4800),
    });
    return result.ok;
  }

  async function confirmTerritorialTypeConversion() {
    const source = territorialTypeSourceFeature();
    if (!source || !territorialTypeSource) return;
    const sourceRef = { ...territorialTypeSource };
    const targetType = (0, dependencies.platform.$)('territorialTypeInput').value;
    const sovereignId = sourceRef.unitType === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY
      ? (0, dependencies.platform.$)('territorialTypeSovereignInput').value
      : String(source.properties?.sovereignId || '');
    const parentId = (0, dependencies.platform.$)('territorialTypeParentInput').value;
    closeTerritorialTypeModal();
    if (sourceRef.unitType === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY) {
      await convertCountryToRegionType(sourceRef.id, targetType, sovereignId, parentId);
    } else if (targetType === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY) {
      await promoteTerritorialUnitToCountry(sourceRef.id);
    } else {
      await convertTerritorialUnitType(sourceRef.id, targetType, parentId);
    }
  }

  function commitLabelEdit(field, value) {
    if (dependencies.projectState.state.selected?.domain !== 'label') return;
    const label = dependencies.projectState.state.labels.find(x => x.id === dependencies.projectState.state.selected.id);
    if (!label) return;
    dependencies.domains.projectDomain.recordHistory();
    label[field] = value;
    if (field === 'name' || field === 'kind') (0, dependencies.layers.markLayerTreeDirty)();
    (0, dependencies.propertyEditingA.applyLabelSelectionIntent)(label.id, true);
    dependencies.domains.projectDomain.queueAutosave();
    (0, dependencies.feedback.setActionStatus)('지명 정보를 변경했습니다.', 'success');
  }

  function initializeTerritorialTypeSource() {
    (territorialTypeSource = null);
  }

  return Object.freeze({
    connect,
    initializeTerritorialTypeSource,
    get closeTerritorialTypeModal() { return closeTerritorialTypeModal; },
    get commitLabelEdit() { return commitLabelEdit; },
    get confirmTerritorialTypeConversion() { return confirmTerritorialTypeConversion; },
    get openTerritorialTypeModal() { return openTerritorialTypeModal; },
    get requestTerritorialUnitPromotion() { return requestTerritorialUnitPromotion; },
    get syncTerritorialTypeModal() { return syncTerritorialTypeModal; },
  });
}
