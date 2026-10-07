import { createWorkerRpcClient } from './worker-rpc.js';
import { createLatestWorkerJobScheduler, createWorkerCancellationError } from './worker-job-scheduler.js';
import { isBuiltinPlaceId, normalizePlace, PLACE_LIMITS } from './place-contract.js';
const EMPTY = Object.freeze({ revision: 0, signature: 'empty', records: Object.freeze([]) });

/** Canonical owner of the builtin label snapshot and Worker lifecycle. */
export function createPlaceRuntime({ manifestUrl = new URL('../../data/places/manifest.json', import.meta.url).href, createWorker = null, rpc = null, onSnapshot = () => {}, onError = () => {}, onSettled = () => {}, getProtectedIds = () => [] } = {}) {
  if (typeof manifestUrl !== 'string' || !manifestUrl) throw new TypeError('Place manifest URL must be a nonempty string');
  let snapshot=EMPTY, revision=0, moving=false, closed=false, desired=null, requestedKey=null;
  const retained=new Map();
  const client=rpc || createWorkerRpcClient({ createWorker, defaultTimeoutMs: 15_000 });
  const scheduler=createLatestWorkerJobScheduler({
    execute: entry => client.request(entry.jobKey === 'viewport' ? 'place.viewport' : 'place.search', { ...entry.payload, manifestUrl }, { signal: entry.metadata.controller.signal }).then(message => message.result),
    cancelRunning: entry => entry.metadata.controller.abort(),
    isCurrent: entry => !closed && (entry.jobKey !== 'viewport' || (!moving && entry.targetRevision === revision)),
  });
  function remember(record) {
    if (!record || !isBuiltinPlaceId(record.id)) return;
    retained.delete(record.id); retained.set(record.id,record);
    const protectedIds=new Set(getProtectedIds());
    while (retained.size > PLACE_LIMITS.retainedRecords) {
      const oldest=[...retained.keys()].find(id => !protectedIds.has(id));
      // Selection can be arbitrarily large; the primary is first in protectedIds.
      retained.delete(oldest || [...retained.keys()].find(id => id !== protectedIds.values().next().value));
    }
  }
  async function prepare(view) {
    desired=view; if (closed || moving) return false;
    const key=JSON.stringify(view);
    if (key === requestedKey) return false;
    requestedKey=key;
    const current=++revision;
    try {
      const job=scheduler.enqueue({ jobKey: 'viewport', targetRevision: current, payload: { view }, metadata: { controller: new AbortController() } });
      const result=await job.promise;
      if (closed || moving || current !== revision) return false;
      if (!Array.isArray(result.records) || result.records.length > PLACE_LIMITS.candidates) throw new RangeError('Place snapshot candidate budget exceeded');
      const records=Object.freeze(result.records.map(normalizePlace));
      snapshot=Object.freeze({ ...result, revision: current, records });
      onSnapshot(snapshot); return true;
    } catch (error) { if (!error.cancelled && error.name !== 'AbortError') { if (current === revision) requestedKey=null; onError(error); } return false; }
  }
  function cancelViewport() { requestedKey=null; revision++; scheduler.cancelKey('viewport'); }
  return Object.freeze({
    prepare,
    cancelViewport,
    beginInteraction() { moving=true; cancelViewport(); scheduler.cancelKey('search'); },
    settle(view=desired) { const wasMoving=moving; moving=false; const task=view ? prepare(view) : Promise.resolve(false); if (wasMoving) onSettled(); return task; },
    snapshot: () => snapshot,
    resolve: id => retained.get(String(id)) || snapshot.records.find(record => record.id === String(id)) || null,
    retain: remember,
    async search(query) {
      if (moving || closed) throw createWorkerCancellationError('지도 이동 중에는 지명 조회를 보류합니다.', 'interaction');
      const job=scheduler.enqueue({ jobKey: 'search', payload: { query }, metadata: { controller: new AbortController() } });
      const result=await job.promise;
      if (result.records.length > PLACE_LIMITS.searchResults) throw new RangeError('Place search result budget exceeded');
      const records=result.records.map(normalizePlace); records.forEach(remember); return { ...result, records };
    },
    cancelSearch: () => scheduler.cancelKey('search'),
    isSearchOpen: () => !closed && !moving,
    stats: () => ({ ...scheduler.stats(), snapshotRecords: snapshot.records.length, retainedRecords: retained.size, moving }),
    dispose() { if (closed) return; closed=true; revision++; scheduler.close(); client.stop(); retained.clear(); snapshot=EMPTY; },
  });
}
