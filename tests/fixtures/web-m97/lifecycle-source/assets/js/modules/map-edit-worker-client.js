import { geometryRevision } from './geometry-versions.js';
import { createEditSourceTracker } from './edit-source-tracker.js';
import { createLatestWorkerJobScheduler } from './worker-job-scheduler.js';
import {
  WORKER_RPC_ERROR_CATEGORIES,
  createWorkerRpcClient,
} from './worker-rpc.js';

function createMapEditWorkerCodec() {
  return Object.freeze({
    encodeRequest(envelope) {
      const metadata = envelope.metadata || {};
      return {
        type: 'execute',
        operation: envelope.operation,
        requestId: envelope.requestId,
        jobKey: String(metadata.jobKey || ''),
        dataRevision: Number(metadata.dataRevision || 0),
        geometryRevision: Number(metadata.geometryRevision || metadata.dataRevision || 0),
        targetRevision: Number(envelope.projectRevision || 0),
        priority: Number(envelope.priority || 0),
        ...(envelope.payload || {}),
      };
    },
    encodeCancel(envelope) {
      return {
        type: 'cancel',
        requestId: envelope.requestId,
        targetRevision: Number(envelope.projectRevision || 0),
        reason: envelope.reason || 'cancelled',
      };
    },
    encodeEvent(envelope) {
      const legacyTypes = {
        'map-edit.rebase': 'rebase',
        'map-edit.boundary-sync': 'boundary-sync',
        'map-edit.boundary-invalidate': 'boundary-invalidate',
        'map-edit.sync-patch': 'sync-patch',
        'map-edit.commit': 'commit',
        'map-edit.discard': 'discard',
      };
      return {
        type: legacyTypes[envelope.operation] || envelope.operation,
        ...(envelope.payload || {}),
      };
    },
    decodeMessage(message) {
      if (message?.type === 'ready') {
        return {
          kind: 'event',
          operation: 'map-edit.ready',
          payload: message,
          projectRevision: Number(message.targetRevision || 0),
        };
      }
      if (message?.type !== 'result') return null;
      return {
        kind: 'result',
        requestId: Number(message.requestId || 0),
        operation: '',
        projectRevision: Number(message.targetRevision || 0),
        ok: message.ok === true,
        result: message.result,
        error: message.ok === true ? null : {
          category: message.cancelled ? WORKER_RPC_ERROR_CATEGORIES.CANCELLED : WORKER_RPC_ERROR_CATEGORIES.OPERATION,
          code: message.cancelled ? 'PL-MAP-EDIT-CANCELLED' : 'PL-MAP-EDIT-WORKER',
          message: message.message || '지도 편집 계산에 실패했습니다.',
        },
        timing: message.timing || null,
      };
    },
  });
}

export function createMapEditWorkerClient({
  createWorker,
  getEntities,
  getFeatureById,
  getBoundaryFeatures = null,
  getEditSources = null,
  getTargetRevision = null,
  now = () => performance.now(),
  schedule = (callback, delay) => setTimeout(callback, delay),
  readyTimeoutMs = 3000,
  requestTimeoutMs = 60_000,
}) {
  let rpc = null;
  let dataRevision = 0;
  let ready = false;
  let boundarySources = new Map();
  const editSources = createEditSourceTracker();
  let temporaryEntities = null;
  const sourceRows = () => {
    const rows = getEditSources?.() || getEntities().map(feature => ({ kind: 'territorial', feature }));
    return temporaryEntities ? [...rows.filter(row => row.kind !== 'territorial'),
      ...temporaryEntities.map(feature => ({ kind: 'territorial', feature }))] : rows;
  };
  function syncEditSources() {
    const patch = editSources.update(sourceRows());
    if (patch.patches.length || patch.removedKeys.length) ensureRpc().notify('edit-sync', patch);
    return patch.sourceRevision;
  }
  const boundarySignature = feature => JSON.stringify([geometryRevision(feature.geometry), feature.properties?.entityKind, feature.properties?.parentId, undefined, !!(feature.boundaryLocked ?? feature.properties?.locked)]);
  const currentTargetRevision = () => typeof getTargetRevision === 'function'
    ? Number(getTargetRevision())
    : dataRevision;

  function ensureRpc() {
    if (rpc) return rpc;
    rpc = createWorkerRpcClient({
      createWorker,
      codec: createMapEditWorkerCodec(),
      defaultTimeoutMs: requestTimeoutMs,
      getProjectRevision: currentTargetRevision,
      isCurrent: ({ entry }) => Number(entry.metadata?.geometryRevision) === dataRevision
        && (entry.metadata?.boundaryReadOnly || Number(entry.projectRevision) === currentTargetRevision()),
      onEvent: event => {
        if (event.operation !== 'map-edit.ready') return;
        const message = event.payload || {};
        const readyRevision = message.dataRevision == null ? dataRevision : Number(message.dataRevision);
        if (readyRevision === dataRevision) ready = true;
      },
      onCrash: () => { ready = false; },
      now,
    });
    return rpc;
  }

  async function waitForReady() {
    await new Promise(resolve => {
      const started = now();
      const poll = () => ready || now() - started > readyTimeoutMs ? resolve() : schedule(poll, 16);
      poll();
    });
    if (!ready) throw new Error('지도 편집 Worker를 준비하지 못했습니다. 잠시 후 다시 시도하세요.');
  }

  function sendExecute(entry) {
    return ensureRpc().request(entry.payload.operation, entry.payload.payload, {
      requestId: entry.requestId,
      projectRevision: entry.targetRevision,
      priority: entry.priority,
      timeoutMs: requestTimeoutMs,
      metadata: {
        boundaryReadOnly: entry.payload.operation.startsWith('boundary-') || entry.payload.operation.startsWith('territorial-'),
        jobKey: entry.jobKey,
        dataRevision: entry.geometryRevision,
        geometryRevision: entry.geometryRevision,
        targetRevision: entry.targetRevision,
      },
    }).then(response => response.result).catch(error => {
      // A cancel message cannot interrupt a synchronous polygon operation.
      if (['PL-WORKER-RPC-TIMEOUT', 'PL-WORKER-RPC-CRASH'].includes(error?.code)) {
        const timedOutRpc = rpc;
        // Deliver the timeout to its caller before cancelling the other queued work.
        setTimeout(() => { if (rpc === timedOutRpc) stop(); }, 0);
      }
      throw error;
    });
  }

  const scheduler = createLatestWorkerJobScheduler({
    maxConcurrent: 1,
    now,
    execute: sendExecute,
    cancelRunning: entry => ensureRpc().cancel(entry.requestId, 'superseded'),
    discardResult: (_result, entry) => ensureRpc().notify('map-edit.discard', {
      requestId: entry.requestId,
      dataRevision,
      targetRevision: entry.targetRevision,
    }, { projectRevision: entry.targetRevision }),
    isCurrent: entry => Number(entry.geometryRevision) === dataRevision
      && (entry.payload.operation.startsWith('boundary-') || entry.payload.operation.startsWith('territorial-') || Number(entry.targetRevision) === currentTargetRevision()),
  });

  function boundarySnapshot() {
    const features = temporaryEntities || getBoundaryFeatures?.() || getEntities();
    boundarySources = new Map(features.map(feature => [String(feature.id), { geometry: feature.geometry, signature: boundarySignature(feature) }]));
    return features;
  }

  function syncBoundarySources() {
    const features = temporaryEntities || getBoundaryFeatures?.() || getEntities();
    const ids = new Set(features.map(feature => String(feature.id)));
    const patches = features.filter(feature => {
      const previous = boundarySources.get(String(feature.id));
      return previous?.geometry !== feature.geometry || previous?.signature !== boundarySignature(feature);
    });
    const removedIds = [...boundarySources.keys()].filter(id => !ids.has(id));
    if (!patches.length && !removedIds.length) return;
    for (const id of removedIds) boundarySources.delete(id);
    for (const feature of patches) boundarySources.set(String(feature.id), { geometry: feature.geometry, signature: boundarySignature(feature) });
    ensureRpc().notify('map-edit.boundary-sync', { features: patches, removedIds }, { projectRevision: currentTargetRevision() });
  }

  function rebase(entities = null) {
    if (entities !== null && !Array.isArray(entities)) throw new TypeError('Worker 입력은 공통 엔티티 배열이어야 합니다.');
    temporaryEntities = entities;
    scheduler.cancelAll('rebase');
    dataRevision += 1;
    ready = false;
    ensureRpc().notify('map-edit.rebase', {
      dataRevision,
      geometryRevision: dataRevision,
      targetRevision: currentTargetRevision(),
      boundaryIds: boundarySnapshot().map(feature => String(feature.id)),
      editSources: (editSources.reset(), editSources.update(sourceRows())),
    }, { projectRevision: currentTargetRevision(), priority: 1000 });
    return dataRevision;
  }

  function syncPatch(rawIds) {
    if (!rpc || !ready) return false;
    const ids = [...new Set([...rawIds].map(String).filter(Boolean))];
    if (!ids.length) return false;
    scheduler.cancelAll('state-changed');
    const features = ids.map(getFeatureById).filter(Boolean);
    const removedIds = ids.filter(id => !getFeatureById(id));
    dataRevision += 1;
    ensureRpc().notify('map-edit.sync-patch', {
      dataRevision,
      geometryRevision: dataRevision,
      targetRevision: currentTargetRevision(),
      features,
      removedIds,
      editSources: editSources.update(sourceRows()),
    }, { projectRevision: currentTargetRevision(), priority: 900 });
    return true;
  }

  async function prepareWorker() {
    if (!rpc) rebase();
    if (!ready) {
      const preparingRpc = rpc;
      try { await waitForReady(); }
      catch (error) { if (rpc === preparingRpc) stop(); throw error; }
    }
  }

  async function execute(operation, payload, {
    jobKey = `map-edit:${operation}`,
    targetRevision = null,
    priority = 100,
    signal = null,
  } = {}) {
    await prepareWorker();
    if (signal?.aborted) throw Object.assign(new Error('작업을 취소했습니다.'), { cancelled: true });
    const sourceRevision = syncEditSources();
    if (operation.startsWith('boundary-')) syncBoundarySources();
    const geometryRevision = dataRevision;
    const resolvedTargetRevision = targetRevision == null ? currentTargetRevision() : Number(targetRevision);
    const ticket = scheduler.enqueue({
      jobKey,
      geometryRevision,
      targetRevision: resolvedTargetRevision,
      priority,
      signal,
      payload: { operation, payload: sourceRevision == null ? payload : { ...payload, sourceRevision } },
    });
    const result = await ticket.promise;
    if (sourceRevision != null && sourceRevision !== syncEditSources()) throw Object.assign(new Error('원본이 바뀌어 계산을 취소했습니다.'), { cancelled: true });
    return {
      sourceRevision,
      requestId: ticket.requestId,
      jobKey: String(jobKey),
      geometryRevision,
      targetRevision: resolvedTargetRevision,
      result,
    };
  }

  function commit(requestId) {
    const nextDataRevision = dataRevision + 1;
    ensureRpc().notify('map-edit.commit', { requestId, dataRevision, nextDataRevision }, {
      projectRevision: currentTargetRevision(),
      priority: 1000,
    });
    dataRevision = nextDataRevision;
  }

  function discard(requestId) {
    scheduler.cancel(requestId, 'discarded');
    ensureRpc().notify('map-edit.discard', { requestId, dataRevision }, {
      projectRevision: currentTargetRevision(),
      priority: 1000,
    });
  }

  function cancel() {
    scheduler.cancelAll('cancelled');
    rpc?.cancelAll('cancelled');
  }

  function invalidateBoundaryCache() {
    rpc?.notify('map-edit.boundary-invalidate', {}, { projectRevision: currentTargetRevision() });
  }

  function stop() {
    scheduler.cancelAll('stopped');
    rpc?.stop('stopped');
    rpc = null;
    ready = false;
    temporaryEntities = null;
  }

  return Object.freeze({
    cancel,
    commit,
    discard,
    execute,
    invalidateBoundaryCache,
    rebase,
    stop,
    syncPatch,
    sourcesCurrent: revision => !!rpc && ready && syncEditSources() === revision,
    stats: () => Object.freeze({
      role: 'stateful-dedicated-rpc',
      dataRevision,
      targetRevision: currentTargetRevision(),
      ready,
      ...(rpc?.stats?.() || { pendingCount: 0, workerActive: false }),
      ...scheduler.stats(),
    }),
  });
}
