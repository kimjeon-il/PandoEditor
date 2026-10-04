import './territorial-edit-plan.js';

/** ObjectMetadata: extracted application responsibility.
 * Dependencies are explicitly wired once by the composition modules.
 * Mutable bindings stay local; exported accessors retain live identity.
 */
export function createObjectMetadata() {
  let dependencies;

  function connect(ports) {
    if (dependencies) throw new Error('object-metadata already connected');
    dependencies = ports;
  }

  function commitTerritorialMetadata(ref, field, value) {
    if (ref?.domain !== 'territorial') return { ok: false, code: 'invalid-ref' };
    const result = dependencies.objectModelB.territorialApplicationService.updateMetadata(ref.id, field, value);
    if (!result.ok) {
      dependencies.feedback.setActionStatus(result.issues?.[0] || '영역 정보를 변경할 수 없습니다.', 'error', 4200);
      return result;
    }
    if (!result.changed) return result;
    if (field === 'color') {
      if ((dependencies.territorialModel.entityRepository.get(ref?.id)?.properties.entityKind === 'general' && !dependencies.territorialModel.entityRepository.get(ref?.id)?.properties.parentId)) dependencies.rendering.gpuMapRenderer.invalidateCountryPalette({ base: true, emphasis: true }, 'territorial-color-edited');
      dependencies.domains.renderingDomain.invalidateBaseScene('territorial-color-edited');
      dependencies.domains.renderingDomain.invalidateTerritorialPatch('territorial-color-edited');
    }
    if (field === 'name') dependencies.layers.markLayerTreeDirty();
    if (field === 'name' || field === 'flagDataUrl') dependencies.domains.renderingDomain.invalidateLabels('territorial-metadata-edited');
    const primary = dependencies.projectState.state.selected;
    if (primary?.domain === 'territorial' && primary.type === ref.type && String(primary.id) === String(ref.id)) {
      dependencies.domains.selectionUiController.presentPrimary({ refreshOnly: true });
    }
    dependencies.feedback.setActionStatus('영역 정보를 변경했습니다.', 'success');
    return result;
  }

  function commitGenericFeatureMeta(field, value) {
    if (dependencies.projectState.state.selected?.domain !== 'generic') return;
    const f = dependencies.projectState.state.genericFeatures.find(x => String(x.id) === dependencies.projectState.state.selected.id);
    if (!f) return;
    const result = dependencies.objectModelA.genericFeatureService.updateMetadata(f.id, field, value);
    if (!result.ok) return;
    if (field === 'name') (0, dependencies.layers.markLayerTreeDirty)();
    (0, dependencies.propertyEditingA.applyGenericSelectionIntent)(dependencies.projectState.state.selected.id, true);
    (0, dependencies.feedback.setActionStatus)('기타 객체 정보를 변경했습니다.', 'success');
  }

  function commitHydroEdit(field, value) {
    if (dependencies.projectState.state.selected?.domain !== 'hydro') return;
    const feature = (0, dependencies.hydroPresentation.hydroEditById)(dependencies.projectState.state.selected.id);
    if (!feature || feature.properties?.locked === true) {
      if (feature?.properties?.locked === true) (0, dependencies.feedback.setActionStatus)(`잠금을 해제한 뒤 ${(0, dependencies.hydroPresentation.hydroCategoryLabel)(feature.properties.category)} 정보를 변경하세요.`, 'error', 3200);
      return;
    }
    const nextValue = field === 'editorColor'
      ? (0, dependencies.colorModel.normalizeEditorColor)(value, dependencies.hydroPresentation.HYDRO_TOOL_CONFIG[feature.properties.category].color)
      : value;
    if (feature.properties[field] === nextValue) return false;
    dependencies.domains.projectDomain.recordHistory();
    feature.properties[field] = nextValue;
    dependencies.projectState.state.stateRevision += 1;
    if (field === 'name') (0, dependencies.layers.markLayerTreeDirty)();
    if (field === 'editorColor') dependencies.domains.renderingDomain.invalidateHydroPatch('hydro-color-edited');
    (0, dependencies.propertyEditingA.applyHydroSelectionIntent)(String(feature.id), true);
    dependencies.domains.projectDomain.queueAutosave();
    (0, dependencies.feedback.setActionStatus)(`${(0, dependencies.hydroPresentation.hydroCategoryLabel)(feature.properties.category)} 정보를 변경했습니다.`, 'success');
  }

  function territorialUnitInsideContainer(feature, container) {
    const clipper = window.polygonClipping;
    if (!feature?.geometry || !container?.geometry || !clipper?.difference) return false;
    const outside = clipper.difference(feature.geometry.coordinates, container.geometry.coordinates);
    return (0, dependencies.territoryGeometry.multiPolygonPlanarArea)(outside) <= Math.max(1e-9, (0, dependencies.territoryGeometry.multiPolygonPlanarArea)(feature.geometry.coordinates) * 1e-9);
  }

  function commitTerritorialRelation(field, value) {
    const ref = dependencies.projectState.state.selected;
    const feature = ref?.domain === 'territorial' ? dependencies.territorialModel.entityRepository.get(ref.id) : null;
    if (!feature || field !== 'parentId' || feature.properties.entityKind !== 'general') return false;
    const result = dependencies.objectModelB.territorialApplicationService.changeAdministrativeParent(feature.id, value, {
      validateCandidate: ({ candidateUnits, previousUnits }) => {
        globalThis.PandoLabTerritorialEdit.createKernel(window.polygonClipping).validate(candidateUnits, previousUnits, [feature.id]);
        return true;
      },
    });
    dependencies.domains.selectionUiController.presentPrimary({ refreshOnly: true });
    if (!result.ok) {
      dependencies.feedback.setActionStatus(result.issues?.[0] || '상위 객체를 변경할 수 없습니다.', 'error', 4200);
      return result;
    }
    if (result.changed) dependencies.layers.markLayerTreeDirty();
    return result;
  }


  return Object.freeze({
    connect,

    get commitTerritorialMetadata() { return commitTerritorialMetadata; },
    get commitGenericFeatureMeta() { return commitGenericFeatureMeta; },
    get commitHydroEdit() { return commitHydroEdit; },
    get commitTerritorialRelation() { return commitTerritorialRelation; },
    get territorialUnitInsideContainer() { return territorialUnitInsideContainer; },
  });
}
