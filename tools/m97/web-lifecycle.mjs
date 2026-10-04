import { readFileSync, readdirSync } from 'node:fs';
import { Worker } from 'node:worker_threads';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { resolve, relative } from 'node:path';
import { verifySources } from './contract.mjs';

export const behavioralCommit = '53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47';
const fixtureRoot = fileURLToPath(new URL('../../tests/fixtures/web-m97/', import.meta.url));
const clone = value => structuredClone(value);
const noop = () => {};

export function verifyLifecycleSources({ root = resolve(fixtureRoot, 'lifecycle-source'), manifest = JSON.parse(readFileSync(resolve(fixtureRoot, 'lifecycle-manifest.json'))) } = {}) {
  if (manifest.behavioralCommit !== behavioralCommit) throw Error('Wrong lifecycle behavioral commit');
  const failures = verifySources(root, manifest);
  if (failures.length) throw Error(`Lifecycle source verification failed: ${failures.join('; ')}`);
  const listed = new Set(manifest.sources.map(source => source.path));
  for (const entry of readdirSync(root, { recursive: true, withFileTypes: true })) {
    const path = relative(root, resolve(entry.parentPath, entry.name));
    if (/\.(?:js|mjs|cjs)$/.test(path) && !listed.has(path)) throw Error(`Unlisted executable source: ${path}`);
  }
  if (JSON.stringify(JSON.parse(readFileSync(resolve(root, 'package.json')))) !== '{"type":"module"}') {
    throw Error('Lifecycle host package must only declare ESM loading');
  }
  return { root, manifest };
}

async function productionModules(options) {
  const verified = verifyLifecycleSources(options);
  const names = ['territorial-units', 'territorial-entity-store', 'territorial-entity-repository',
    'territorial-service', 'project-command-pipeline', 'project-state', 'history-service',
    'app-project-snapshots', 'distribution-model', 'app-hydro-settings', 'app-layer-list',
    'generic-feature-service', 'layer-presentation', 'project-invariants', 'geometry-preview',
    'app-geometry-preview', 'app-territorial-drafts', 'app-object-presentation', 'map-object-categories',
    'app-object-metadata', 'builtin-subunits', 'map-edit-worker-client', 'geometry-metrics'];
  const modules = await Promise.all(names.map(name => import(pathToFileURL(resolve(verified.root, `assets/js/modules/${name}.js`)).href)));
  await import(pathToFileURL(resolve(verified.root, 'assets/js/vendor/polygon-clipping.min.js')).href);
  globalThis.window = globalThis;
  return { ...verified, api: Object.assign({}, ...modules) };
}

function square(x0, y0, x1, y1) {
  return { type: 'Polygon', coordinates: [[[x0,y0],[x0,y1],[x1,y1],[x1,y0],[x0,y0]]] };
}

function seedFeatures(api) {
  return [
    ['country-a', '', [0,0,10,10]],
    ['child-a', 'country-a', [0,0,5,10]],
    ['child-b', 'country-a', [5,0,10,10]],
    ['grandchild-b', 'child-b', [6,1,8,3]],
  ].map(([id, parentId, box]) => api.createTerritorialFeature({
    id, name: id, entityKind: 'general', parentId,
    coverageMode: parentId ? 'partition' : 'explicit', geometry: square(...box),
  }));
}

function createRuntime(api) {
  const state = {
    ...api.createEmptyTerritorialState(), stateRevision: 0, tool: 'select', selected: null,
    history: [], historyMeta: [], future: [], futureMeta: [], historyDirtyEntityIds: new Set(),
    sourceInfo: null, labels: [], genericFeatures: [], hydroEdits: [],
    distributionLayers: api.normalizeDistributionLayers([{ id: 'distribution', schemaVersion: 3, name: 'Fixture', parentId: '' }]),
    distributionEntries: api.normalizeDistributionEntries([
      { id: 'entry-a', schemaVersion: 3, layerId: 'distribution', mode: 'territorial', territorialUnitId: 'child-a', value: 1 },
      { id: 'entry-b', schemaVersion: 3, layerId: 'distribution', mode: 'territorial', territorialUnitId: 'child-b', value: 2 },
    ], { layerExists: id => id === 'distribution' }),
    distributionSettings: { renderMode: 'overlap', activeLayerId: 'distribution', boundaryVisible: true },
    labelSettings: { 'territorial:child-b': { visible: false } },
    layerPresentation: { schemaVersion: 4, styles: {}, objectStyles: { 'territorial:entity:child-b': { opacity: 0.5 } }, objectOrder: [] },
    itemVisibility: {}, layerVisibility: {}, layerFolders: {}, physicalSettings: {},
    geometryPreview: api.createGeometryPreviewState(), layerTreeRevision: 0,
  };
  const entityStore = api.createTerritorialEntityStore({ getState: () => state });
  entityStore.restoreProject(api.createStaticTerritorialSnapshot(seedFeatures(api)));
  state.historyDirtyEntityIds.clear();
  const entityRepository = api.createTerritorialEntityRepository({ entityStore });
  const territorialModel = { ...api, entityStore, entityRepository };
  const snapshots = api.createProjectSnapshots();
  const hydro = api.createHydroSettings();
  const layerList = api.createLayerList();
  const objectPresentation = api.createObjectPresentation();
  objectPresentation.connect({ objectCatalog: api });
  objectPresentation.initializeObjectPresentationModel();
  const effects = [];
  const effect = name => (...args) => effects.push({ name, args: args.map(arg => typeof arg === 'function' ? '[function]' : arg) });
  const selectionDomain = { primary: () => null, clear: effect('selection.clear'), prune: effect('selection.prune') };
  const dom = new Map();
  const platform = { deepClone: clone, $: id => {
    if (!dom.has(id)) dom.set(id, { textContent: '', classList: { add: noop } });
    return dom.get(id);
  } };
  hydro.connect({ projectState: { state }, platform, physicalConfig: { PHYSICAL_DATASET: 'lifecycle-fixture' } });
  layerList.connect({ projectState: { state }, territorialModel,
    layerPresentation: { LAYER_GROUP_KEYS: objectPresentation.LAYER_GROUP_KEYS },
    hydroPresentation: { activeLayerFolderKeys: hydro.activeLayerFolderKeys, HYDRO_LAYER_META: {} },
    countries: { builtinTerritorialScene: () => ({ labelById: new Map(entityRepository.list({ kind: 'general', parentId: '' }).map(entity => [entity.id, null])) }) },
    domains: { selectionDomain },
  });
  state.itemVisibility = layerList.normalizeLayerItemState({ subunits: { 'child-b': false } });
  const domains = {
    selectionDomain,
    selectionUiController: { applyIntent: effect('selection.applyIntent'), presentPrimary: effect('selection.presentPrimary') },
    editingDomain: { clearDraft: effect('editing.clearDraft'), setTool: effect('editing.setTool'), refreshDraftPresentation: noop },
    renderingDomain: {},
    projectDomain: { commitHistorySnapshot: snapshot => snapshots.historyService.commitSnapshot(snapshot), queueAutosave: effect('autosave.queued') },
  };
  const taskUi = { updateModeButtons: noop, setModeBanner: noop };
  const feedback = { setActionStatus: noop, reportOperationError: error => { throw error; } };
  const spatialQuery = { markCountryGeometriesChanged: noop, mapEditClient: { invalidateBoundaryCache: noop } };
  const ports = {
    projectState: { state }, territorialModel, platform, domains, taskUi, feedback, spatialQuery,
    projectServices: api, distributionServices: api, applicationConstantsA: api,
    modelValidation: api, hydroModel: hydro, layerTree: layerList,
    labelPresentation: { labelKey: (type, id) => `${type}:${id}` },
    rendering: { gpuMapRenderer: { invalidateHydroVisibility: noop } },
    layers: { markLayerTreeDirty: layerList.markLayerTreeDirty },
    countries: { scheduleCountryLabelAnchors: noop },
    countryEditingB: Object.fromEntries(['resetBoundaryEditState', 'resetMergeState', 'resetGenericFeatureMergeState', 'resetTerritorialUnitEditState', 'resetTerritoryEditingState'].map(key => [key, noop])),
    domainControllers: { objectPropertyController: { show: noop } },
    projectSession: { saveState: { markContentChanged: noop } },
    platformConfigurationB: { MAX_HISTORY: 100 }, lifecycleUi: { projectUi: { syncHistory: noop } },
    surfaces: { uid: () => `history-${state.history.length}` },
    geometryPreview: { rebuildBoundaryTopology: noop },
  };
  snapshots.connect(ports);
  snapshots.initializeHistoryStore();
  const pipeline = api.createProjectCommandPipeline({
    captureSnapshot: snapshots.snapshotEditable,
    restoreSnapshot: snapshots.restoreEditTransactionSnapshot,
    recordHistory: (metadata, snapshot) => snapshots.historyService.commitSnapshot(snapshot, metadata),
    discardHistory: snapshots.historyService.discardLast,
    validateProject: () => api.assertProjectReferenceIntegrity({ ...state, territorialEntities: entityRepository.list() }),
    advanceRevision: () => ++state.stateRevision,
  });
  const service = api.createTerritorialApplicationService({ entityStore, entityRepository, commandPipeline: pipeline });
  const metadata = api.createObjectMetadata();
  metadata.connect({ ...ports, objectModelB: { territorialApplicationService: service } });
  const geometryPreview = api.createGeometryPreview();
  geometryPreview.connect({ ...ports, geometryEditingCore: api, snapshots,
    validation: snapshots });
  return { state, entityStore, entityRepository, snapshots, metadata, service, ports, domains,
    geometryPreview, effects, api };
}

function observe(runtime) {
  const { state, entityRepository, entityStore } = runtime;
  return clone({
    document: {
      entities: entityRepository.list().map(feature => ({ id: feature.id,
        parentId: feature.properties.parentId, coverageMode: feature.properties.coverageMode,
        entityKind: feature.properties.entityKind, properties: feature.properties, geometry: feature.geometry })),
      identities: entityStore.identities(), timelineRecords: state.timelineRecords,
      geometryVersions: state.geometries.snapshot(), distributionLayers: state.distributionLayers,
      distributionEntries: state.distributionEntries,
    },
    presentation: { itemVisibility: state.itemVisibility, labelSettings: state.labelSettings, layerPresentation: state.layerPresentation },
    history: { undo: state.history.length, redo: state.future.length },
    preview: state.geometryPreview.session,
  });
}
const observed = (runtime, outcome) => ({ observed: true, state: observe(runtime), outcome });
const compactOutcome = result => ({ ok: result.ok, changed: result.changed ?? false, ...(result.code ? { code: result.code } : {}) });

function runDetach(api) {
  const runtime = createRuntime(api);
  runtime.state.selected = { domain: 'territorial', type: 'entity', id: 'child-a' };
  const before = observed(runtime, null);
  const result = runtime.metadata.commitTerritorialRelation('parentId', '');
  const confirm = observed(runtime, compactOutcome(result));
  const undo = observed(runtime, { ok: runtime.snapshots.historyService.undo() });
  const redo = observed(runtime, { ok: runtime.snapshots.historyService.redo() });
  return {
    case: 'parent-detach', input: { entityId: 'child-a', parentId: '' }, referenceEffects: referenceEffects(before.state.document, confirm.state.document), entrypoint: 'app-object-metadata.commitTerritorialRelation → territorial-service.changeAdministrativeParent',
    stages: { before, preview: { observed: false, reason: 'The production parent relation command applies synchronously; no preview stage is exposed.' },
      cancel: { observed: false, reason: 'The production parent relation command has no pending preview to cancel.' }, confirm, undo, redo },
  };
}

function workerFactory(root, manifest) {
  return () => {
    const worker = new Worker(new URL('./web-lifecycle-worker-host.mjs', import.meta.url), { workerData: { root, manifest } });
    const adapter = { onmessage: null, onerror: null, postMessage: message => worker.postMessage(message), terminate: () => worker.terminate() };
    worker.on('message', data => adapter.onmessage?.({ data }));
    worker.on('error', error => adapter.onerror?.(error));
    return adapter;
  };
}

function referenceEffects(before, after) {
  const beforeById = new Map(before.entities.map(entity => [entity.id, entity]));
  const afterById = new Map(after.entities.map(entity => [entity.id, entity]));
  return {
    createdIds: after.entities.filter(entity => !beforeById.has(entity.id)).map(entity => entity.id),
    deletedIds: before.entities.filter(entity => !afterById.has(entity.id)).map(entity => entity.id),
    retainedIds: before.entities.filter(entity => afterById.has(entity.id)).map(entity => entity.id),
    rewrittenDistributionEntryIds: after.distributionEntries.filter(entry => before.distributionEntries.find(row => row.id === entry.id)?.territorialUnitId !== entry.territorialUnitId).map(entry => entry.id),
    reparentedIds: after.entities.filter(entity => beforeById.has(entity.id) && beforeById.get(entity.id).parentId !== entity.parentId).map(entity => entity.id),
  };
}

async function runStagedEdit(api, root, manifest, operation) {
  const runtime = createRuntime(api);
  const client = api.createMapEditWorkerClient({ createWorker: workerFactory(root, manifest),
    getEntities: runtime.entityRepository.list, getFeatureById: runtime.entityRepository.get,
    getTargetRevision: () => runtime.state.stateRevision });
  runtime.ports.spatialQuery.mapEditClient = client;
  const drafts = api.createTerritorialDrafts();
  const modalDecisions = [];
  let modalDecision = 'confirm';
  drafts.connect({ ...runtime.ports, snapshots: runtime.snapshots,
    geometryOperations: runtime.geometryPreview,
    projectRestore: { openConfirmModal: modal => {
      modalDecisions.push({ title: modal.title, decision: modalDecision });
      if (modalDecision === 'cancel') modal.onCancel(); else modal.onConfirm();
    } },
    countryValidation: { refreshCountryCentroids: noop },
  });
  const request = operation === 'merge'
    ? { operation, targetId: 'child-a', parentId: 'country-a', sourceIds: ['child-b'] }
    : operation === 'annex'
      ? { operation, targetId: 'child-a', parentId: 'country-a', sourceId: 'child-b', draft: clone(runtime.entityRepository.get('child-b').geometry) }
      : { operation: 'create', targetId: 'child-a', parentId: 'country-a', sourceId: 'child-a', draft: square(1,1,3,3),
        newFeature: api.createTerritorialFeature({ id: 'child-new', name: 'New child', entityKind: 'general', parentId: 'country-a', coverageMode: 'partition', geometry: square(1,1,3,3) }) };
  const begin = () => drafts.previewTerritorialEdit(request, { selectedId: operation === 'create' ? 'child-new' : 'child-a' });
  try {
    const before = observed(runtime, null);
    const preview = observed(runtime, { ok: await begin() });
    const cancel = observed(runtime, { ok: runtime.geometryPreview.discardActiveGeometryPreview() });
    let impactCancel = { observed: false, reason: 'This create fixture has no impact confirmation modal.' };
    if (operation !== 'create') {
      if (!await begin()) throw Error(`Could not prepare ${operation} for modal rejection`);
      modalDecision = 'cancel';
      impactCancel = observed(runtime, { ok: await runtime.geometryPreview.applyActiveGeometryPreview() });
      modalDecision = 'confirm';
    }
    if (!await begin()) throw Error(`Could not prepare ${operation} again after discard`);
    const confirm = observed(runtime, { ok: await runtime.geometryPreview.applyActiveGeometryPreview() });
    const undo = observed(runtime, { ok: runtime.snapshots.historyService.undo() });
    const redo = observed(runtime, { ok: runtime.snapshots.historyService.redo() });
    return { case: `sibling-${operation === 'annex' ? 'annex-full' : operation}`, input: { request },
      entrypoint: 'app-territorial-drafts.previewTerritorialEdit → map-edit-worker → app-geometry-preview → app-project-snapshots/history',
      stages: { before, preview, cancel, confirm, undo, redo }, additionalStages: { impactCancel }, modalDecisions,
      referenceEffects: referenceEffects(before.state.document, confirm.state.document) };
  } finally { client.stop(); }
}

/** Direct calculation only. No UI/session lifecycle stages are implied by this hook. */
export async function runWorkerCalculation(operation, payload, { entities = [], ...sourceOptions } = {}) {
  const { api, root, manifest } = await productionModules(sourceOptions);
  const client = api.createMapEditWorkerClient({ createWorker: workerFactory(root, manifest),
    getEntities: () => entities, getFeatureById: id => entities.find(feature => String(feature.id) === String(id)),
    getTargetRevision: () => 0 });
  try {
    const response = await client.execute(operation, { payload });
    return response.result;
  } finally { client.stop(); }
}

export async function runLifecycleCorpus(options) {
  const { api, root, manifest } = await productionModules(options);
  return {
    schema: 'pando-web-lifecycle-observations', version: 1, behavioralCommit,
    sourceCount: manifest.sources.length,
    observationMode: 'Unmodified production modules and actual worker client/RPC/worker with headless platform/presentation ports. Browser UI rendering and selection are unobserved.',
    unobserved: [
      { scope: 'Country merge and country annex lifecycle', reason: 'The observed merge/annex cases are sibling territorial edits, not country command workflows.' },
      { scope: 'Parent-detach preview/cancel', reason: 'The actual parent relation command is synchronous and exposes no preview/cancel stage.' },
      { scope: 'DOM, rendered pixels, pointer gestures, selection state, persisted autosave, browser worker transport', reason: 'Presentation/platform ports record or discard effects; actual application document owners and worker code execute headlessly.' },
      { scope: 'Other edit operations, invalid geometry, locks, source races, failures, and concurrent command behavior', reason: 'Outside this bounded four-case lifecycle corpus.' },
    ],
    cases: [runDetach(api), ...await Promise.all(['merge', 'annex', 'create'].map(operation => runStagedEdit(api, root, manifest, operation)))],
  };
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  process.stdout.write(`${JSON.stringify(await runLifecycleCorpus(), null, 2)}\n`);
}
