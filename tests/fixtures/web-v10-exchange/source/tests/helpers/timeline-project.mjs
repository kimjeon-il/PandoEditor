import {TERRITORIAL_SCHEMA_VERSION} from '../../assets/js/modules/territorial-units.js';
import { timelineStorageCases } from '../fixtures/timeline-storage-cases.mjs';
import { createEmptyTerritorialState, createTerritorialEntityStore, createStaticTerritorialSnapshot } from '../../assets/js/modules/territorial-entity-store.js';
import { createProjectSerializer } from '../../assets/js/modules/project-serializer.js';
import { DISTRIBUTION_MODES } from '../../assets/js/modules/distribution-model.js';

export function projectForStorage(row = timelineStorageCases()[0]) {
  return {
    territorialEntities: row.entities.map(({ id, entityKind }) => ({ type: 'Feature', id,
      geometry: null, properties: { schemaVersion: TERRITORIAL_SCHEMA_VERSION, entityKind, name: id, notes: '',
        style: {}, locked: false, metadata: {}, sourceFolderId: '', sourceEntityId: '', sourceGeometryVersion: '' } })),
    projectFields: { timelineRecords: structuredClone(row.input.records),
      geometries: structuredClone(row.input.geometries), sourceInfo: null, labels: [],
      genericFeatures: [], hydroEdits: [], distributionLayers: [], distributionEntries: [],
      labelSettings: {}, distributionSettings: { renderMode: 'overlap', activeLayerId: '', boundaryVisible: true },
      layerPresentation: { schemaVersion: 4, styles: {}, objectStyles: {}, objectOrder: [] },
      physicalSettings: {}, layerVisibility: {}, itemVisibility: {} }, fullAutosave: true,
  };
}

export function initializeTestTerritorialState(state) {
  const features = state.territorialEntities;
  Object.assign(state, createEmptyTerritorialState());
  createTerritorialEntityStore({ getState: () => state }).replaceEntities(features);
  state.historyDirtyEntityIds = new Set();
  return state;
}

export function staticSerializerSnapshot(snapshot) {
  const content = createStaticTerritorialSnapshot(snapshot.territorialEntities);
  return { baseDatasetFingerprint: '1'.repeat(64), ...snapshot, territorialEntities: content.territorialEntities,
    entityDelta: snapshot.entityDelta ? { ...snapshot.entityDelta,
      changed: snapshot.entityDelta.changed.map(feature => content.territorialEntities.find(entity => entity.id === feature.id)) } : undefined,
    projectFields: { ...projectForStorage().projectFields, ...snapshot.projectFields,
      timelineRecords: content.timelineRecords, geometries: content.geometries } };
}

/** Current complete autosave for browser fixtures, without a baseline delta. */
export function staticAutosaveProject(projectFields = {}, territorialEntities = []) {
  const snapshot = staticSerializerSnapshot({ territorialEntities, fullAutosave: true, projectFields });
  return createProjectSerializer({ appVersion: 'fixture', distributionModes: Object.values(DISTRIBUTION_MODES),
    terrainDataset: 'fixture-terrain', hydroDataset: 'fixture-hydro', readSnapshot: () => snapshot }).buildAutosave();
}

export function snapshotTestTerritorialState(state) {
  const { territorialEntities, geometries, ...fields } = state;
  const store = createTerritorialEntityStore({ getState: () => state });
  return { ...structuredClone(fields), territorialEntities: store.identities(), geometries: geometries.snapshot() };
}

export function restoreTestTerritorialState(state, snapshot) {
  createTerritorialEntityStore({ getState: () => state }).restoreProject(snapshot);
  const { territorialEntities, geometries, timelineRecords, ...fields } = snapshot;
  Object.assign(state, structuredClone(fields));
}
