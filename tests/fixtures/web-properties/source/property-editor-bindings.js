export function createPropertyEditorBindings({
  getElement: $,
  document,
  getPrimary,
  TERRITORIAL_UNIT_TYPES,
  bindColorPickers,
  commitHydroEdit,
  commitTerritorialUnitMeta,
  commitDistributionMeta,
  commitLabelEdit,
  removeDistributionEntry,
  addTerritorialDistributionEntry,
  requestDraftDiscard,
  completeToolStart,
  startGeometryDistributionDraft,
  requestTerritorialUnitDivisionRemoval,
  enterTerritorialUnitCoastMode,
  enterTerritorialCreateWorkflow,
  enterTerritorialUnitAnnexMode,
  enterTerritorialUnitMergeMode,
  enterTerritorialUnitRedrawMode,
  territorialUnitById,
  reconcileAdminCountryCoast,
  requestTerritorialUnitPromotion,
  openTerritorialTypeModal,
  syncTerritorialTypeModal,
  closeTerritorialTypeModal,
  confirmTerritorialTypeConversion,
  setEditorShellView,
  setActionStatus,
  focusObjectRef,
  convertSelectedGenericFeature,
  copySelectedHydroForEditing,
  undo,
  redo,
  closeObjectActionsMenu,
  batchSetVisibility,
  enterCountryBorderEditFromSelection,
} = {}) {

  let bound = false;
  const listeners = [];
  function listen(element, type, handler) {
    if (!element) return;
    element.addEventListener(type, handler);
    listeners.push(() => element.removeEventListener(type, handler));
  }
  function dispose() {
    for (const remove of listeners.splice(0)) remove();
    bound = false;
  }

  function bindChangeFields(definitions) {
    for (const { id, field, commit, transform = value => value } of definitions) {
      listen($(id), 'change', event => commit(field, transform(event.target.value)));
    }
  }

  function bindEditorFields() {
    if (bound) return;
    bound = true;
    bindColorPickers();
    bindChangeFields([
      { id: 'hydroNameInput', field: 'name', commit: commitHydroEdit, transform: value => value.trim() },
      { id: 'hydroNotesInput', field: 'notes', commit: commitHydroEdit },
      { id: 'subunitNameInput', field: 'name', commit: commitTerritorialUnitMeta, transform: value => value.trim() },
      { id: 'subunitCountryInput', field: 'sovereignId', commit: commitTerritorialUnitMeta },
      { id: 'subunitParentInput', field: 'parentId', commit: commitTerritorialUnitMeta },
      { id: 'subunitNotesInput', field: 'notes', commit: commitTerritorialUnitMeta },
      { id: 'regionNameInput', field: 'name', commit: commitTerritorialUnitMeta, transform: value => value.trim() },
      { id: 'regionCountryInput', field: 'sovereignId', commit: commitTerritorialUnitMeta },
      { id: 'regionParentInput', field: 'parentId', commit: commitTerritorialUnitMeta },
      { id: 'regionValidFromInput', field: 'validFrom', commit: commitTerritorialUnitMeta, transform: value => value.trim() },
      { id: 'regionValidToInput', field: 'validTo', commit: commitTerritorialUnitMeta, transform: value => value.trim() },
      { id: 'regionNotesInput', field: 'notes', commit: commitTerritorialUnitMeta },
      { id: 'distributionNameInput', field: 'name', commit: commitDistributionMeta, transform: value => value.trim() },
      { id: 'distributionParentInput', field: 'parentId', commit: commitDistributionMeta },
      { id: 'labelNameInput', field: 'name', commit: commitLabelEdit, transform: value => value.trim() },
      { id: 'labelKindInput', field: 'kind', commit: commitLabelEdit },
      { id: 'labelNotesInput', field: 'notes', commit: commitLabelEdit },
    ]);
    listen($('distributionEntryList'), 'click', event => {
      const button = event.target.closest('[data-distribution-entry-delete]');
      if (button) removeDistributionEntry(button.dataset.distributionEntryDelete);
    });
    listen($('addTerritorialDistributionBtn'), 'click', addTerritorialDistributionEntry);
    listen($('addGeometryDistributionBtn'), 'click', () => requestDraftDiscard(() => completeToolStart(startGeometryDistributionDraft())));
    listen($('removeSubunitDivisionBtn'), 'click', () => (getPrimary()?.domain === 'territorial' && getPrimary().type !== TERRITORIAL_UNIT_TYPES.COUNTRY) && requestTerritorialUnitDivisionRemoval(getPrimary().id));
    for (const id of ['addCountrySubunitBtn', 'addSubunitChildBtn']) {
      listen($(id), 'click', () => requestDraftDiscard(() => completeToolStart(enterTerritorialCreateWorkflow(TERRITORIAL_UNIT_TYPES.SUBUNIT))));
    }
    listen($('editSubunitCoastBtn'), 'click', () => requestDraftDiscard(() => completeToolStart(enterTerritorialUnitCoastMode(getPrimary()?.id))));
    listen($('annexSubunitBtn'), 'click', () => requestDraftDiscard(() => completeToolStart(enterTerritorialUnitAnnexMode(getPrimary()?.id))));
    listen($('mergeSubunitBtn'), 'click', () => (getPrimary()?.domain === 'territorial' && getPrimary().type !== TERRITORIAL_UNIT_TYPES.COUNTRY) && requestDraftDiscard(() => completeToolStart(enterTerritorialUnitMergeMode(getPrimary().id))));
    listen($('mergeRegionBtn'), 'click', () => (getPrimary()?.domain === 'territorial' && getPrimary().type !== TERRITORIAL_UNIT_TYPES.COUNTRY) && requestDraftDiscard(() => completeToolStart(enterTerritorialUnitMergeMode(getPrimary().id))));
    listen($('reassignSubunitShapeBtn'), 'click', () => (getPrimary()?.domain === 'territorial' && getPrimary().type !== TERRITORIAL_UNIT_TYPES.COUNTRY) && requestDraftDiscard(() => completeToolStart(enterTerritorialUnitRedrawMode(getPrimary().id))));
    listen($('reconcileSubunitCoastBtn'), 'click', () => {
      if (!(getPrimary()?.domain === 'territorial' && getPrimary().type !== TERRITORIAL_UNIT_TYPES.COUNTRY)) return;
      const feature = territorialUnitById(getPrimary().id);
      if (feature?.properties?.unitType !== TERRITORIAL_UNIT_TYPES.SUBUNIT || feature.properties?.locked === true) return;
      reconcileAdminCountryCoast(getPrimary().id);
    });
    listen($('reassignRegionShapeBtn'), 'click', () => (getPrimary()?.domain === 'territorial' && getPrimary().type !== TERRITORIAL_UNIT_TYPES.COUNTRY) && requestDraftDiscard(() => completeToolStart(enterTerritorialUnitRedrawMode(getPrimary().id))));
    listen($('promoteSubunitBtn'), 'click', () => (getPrimary()?.domain === 'territorial' && getPrimary().type !== TERRITORIAL_UNIT_TYPES.COUNTRY) && requestTerritorialUnitPromotion(getPrimary().id));
    listen($('changeCountryTypeBtn'), 'click', () => (getPrimary()?.domain === 'territorial' && getPrimary().type === TERRITORIAL_UNIT_TYPES.COUNTRY)
      && openTerritorialTypeModal(TERRITORIAL_UNIT_TYPES.COUNTRY, getPrimary().id));
    listen($('territorialTypeInput'), 'change', syncTerritorialTypeModal);
    listen($('territorialTypeSovereignInput'), 'change', () => {
      $('territorialTypeParentInput').value = '';
      syncTerritorialTypeModal();
    });
    listen($('territorialTypeParentInput'), 'change', syncTerritorialTypeModal);
    listen($('territorialTypeCancelBtn'), 'click', closeTerritorialTypeModal);
    listen($('territorialTypeModal').querySelector('.confirm-modal-dim'), 'click', closeTerritorialTypeModal);
    listen($('territorialTypeConfirmBtn'), 'click', confirmTerritorialTypeConversion);
    listen($('transferRegionBtn'), 'click', () => {
      setEditorShellView('info');
      $('regionCountryInput').focus();
      setActionStatus('소속 국가와 상위 단위를 확인한 뒤 변경하세요.', 'success', 3400);
    });

    const syncGenericFeatureConversionFields = () => {
      const target = $('genericFeatureConvertType').value;
      const countryField = $('genericFeatureConvertCountryField');
      const distributionField = $('genericFeatureConvertDistributionField');
      countryField?.classList.toggle('hidden', !['subunit', 'region'].includes(target) || countryField.dataset.singleChoice === 'true');
      distributionField?.classList.toggle('hidden', target !== 'distribution' || distributionField.dataset.singleChoice === 'true');
      const countryRequired = ['subunit', 'region'].includes(target);
      const distributionRequired = target === 'distribution';
      $('convertGenericFeatureBtn').disabled = (countryRequired
        && (!$('genericFeatureConvertCountryInput').value || countryField?.dataset.invalidChoice === 'true'))
        || (distributionRequired && !$('genericFeatureConvertDistributionInput').value);
    };
    listen($('genericFeatureConvertType'), 'change', syncGenericFeatureConversionFields);
    listen($('genericFeatureConvertCountryInput'), 'change', syncGenericFeatureConversionFields);
    listen($('genericFeatureConvertDistributionInput'), 'change', syncGenericFeatureConversionFields);
    listen($('convertGenericFeatureBtn'), 'click', () => {
      const primary = getPrimary();
      if (primary?.domain !== 'generic') return;
      void convertSelectedGenericFeature?.({
        target: $('genericFeatureConvertType')?.value,
        sovereignId: $('genericFeatureConvertCountryInput')?.value,
        distributionLayerId: $('genericFeatureConvertDistributionInput')?.value,
      });
    });

    listen($('copyHydroBtn'), 'click', copySelectedHydroForEditing);

    listen($('undoBtn'), 'click', undo);
    listen($('redoBtn'), 'click', redo);

    listen($('focusSelectedObjectBtn'), 'click', () => getPrimary() && focusObjectRef(getPrimary()));
    listen($('objectFocusMenuBtn'), 'click', () => { closeObjectActionsMenu(); if (getPrimary()) focusObjectRef(getPrimary()); });
    listen($('objectActionsMenu'), 'keydown', event => {
      if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); closeObjectActionsMenu({ restoreFocus: true }); return; }
      const items = [...event.currentTarget.querySelectorAll('[role="menuitem"]:not(.hidden):not(:disabled)')];
      const current = items.indexOf(document.activeElement);
      const delta = event.key === 'ArrowDown' ? 1 : event.key === 'ArrowUp' ? -1 : 0;
      if (!delta || !items.length) return;
      event.preventDefault();
      items[(current + delta + items.length) % items.length]?.focus();
    });
    listen($('multiPropertiesVisibilityInput'), 'change', event => batchSetVisibility(event.target.checked));
    listen($('multiSubunitMergeBtn'), 'click', () => requestDraftDiscard(() => completeToolStart(enterCountryBorderEditFromSelection('merge'))));
    listen($('multiBorderEditBtn'), 'click', () => requestDraftDiscard(() => completeToolStart(enterCountryBorderEditFromSelection())));
  }

  return Object.freeze({
    bind: bindEditorFields,
    dispose,
  });
}
