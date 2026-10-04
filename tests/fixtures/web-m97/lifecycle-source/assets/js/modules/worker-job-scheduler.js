function defaultNow() {
  return globalThis.performance?.now?.() ?? Date.now();
}

export function createWorkerCancellationError(message = '작업을 취소했습니다.', reason = 'cancelled') {
  return Object.assign(new Error(message), {
    name: 'AbortError',
    cancelled: true,
    reason: String(reason || 'cancelled'),
  });
}

function percentile(values, ratio) {
  if (!values.length) return 0;
  const ordered = [...values].sort((left, right) => left - right);
  return ordered[Math.min(ordered.length - 1, Math.max(0, Math.ceil(ordered.length * ratio) - 1))] || 0;
}

/**
 * A deliberately small scheduler for expensive Worker calls.
 *
 * It does not own a Worker and it is not a general RPC layer. The caller owns
 * the stateful/stateless Worker and supplies `execute`/`cancelRunning` hooks.
 * Jobs sharing a key use latest-wins semantics: at most one running job and one
 * queued successor survive for that key.
 */
export function createLatestWorkerJobScheduler({
  execute,
  cancelRunning = () => {},
  discardResult = () => {},
  isCurrent = () => true,
  maxConcurrent = 1,
  now = defaultNow,
  latencySampleLimit = 120,
} = {}) {
  if (typeof execute !== 'function') throw new TypeError('Worker job execute callback is required.');
  const concurrency = Math.max(1, Math.floor(Number(maxConcurrent) || 1));
  let sequence = 0;
  let closed = false;
  const queue = [];
  const queuedByKey = new Map();
  const running = new Map();
  const runningByKey = new Map();
  const latencySamples = [];
  const metrics = {
    submitted: 0,
    started: 0,
    completed: 0,
    failed: 0,
    cancelled: 0,
    coalesced: 0,
    staleDiscarded: 0,
    maxQueueDepth: 0,
    lastLatencyMs: 0,
  };

  function rememberLatency(value) {
    const latency = Math.max(0, Number(value) || 0);
    metrics.lastLatencyMs = latency;
    latencySamples.push(latency);
    if (latencySamples.length > latencySampleLimit) latencySamples.splice(0, latencySamples.length - latencySampleLimit);
  }

  function rejectEntry(entry, error, metric = 'cancelled') {
    if (!entry || entry.settled) return;
    entry.settled = true;
    entry.detachAbort?.();
    if (metric && Object.hasOwn(metrics, metric)) metrics[metric] += 1;
    entry.reject(error);
  }

  function supersede(entry, reason = 'superseded') {
    if (!entry || entry.obsolete) return;
    entry.obsolete = true;
    const error = createWorkerCancellationError(
      reason === 'coalesced'
        ? '더 최신 편집 요청으로 대체했습니다.'
        : '작업을 취소했습니다.',
      reason,
    );
    if (running.has(entry.requestId)) {
      try { cancelRunning(entry); } catch (_) { /* Cancellation is best effort. */ }
    }
    rejectEntry(entry, error, reason === 'coalesced' ? 'coalesced' : 'cancelled');
  }

  function removeQueued(entry) {
    const index = queue.indexOf(entry);
    if (index >= 0) queue.splice(index, 1);
    if (queuedByKey.get(entry.jobKey) === entry) queuedByKey.delete(entry.jobKey);
  }

  function cancelRequest(requestId, reason = 'cancelled') {
    const numericId = Number(requestId || 0);
    const queued = queue.find(entry => entry.requestId === numericId);
    if (queued) {
      removeQueued(queued);
      supersede(queued, reason);
      return true;
    }
    const active = running.get(numericId);
    if (active) {
      supersede(active, reason);
      return true;
    }
    return false;
  }

  function settleRun(entry, ok, value) {
    running.delete(entry.requestId);
    if (runningByKey.get(entry.jobKey) === entry) runningByKey.delete(entry.jobKey);
    const stale = entry.obsolete || !isCurrent(entry);
    if (stale) {
      if (!entry.obsolete) {
        metrics.staleDiscarded += 1;
        rejectEntry(entry, createWorkerCancellationError(
          '지도 상태가 바뀌어 오래된 계산 결과를 폐기했습니다.',
          'stale',
        ), null);
      }
      if (ok) {
        try { discardResult(value, entry); } catch (_) { /* A stale result must never block the queue. */ }
      }
    } else if (!entry.settled) {
      entry.settled = true;
      entry.detachAbort?.();
      if (ok) {
        metrics.completed += 1;
        rememberLatency(now() - entry.submittedAt);
        entry.resolve(value);
      } else {
        metrics.failed += 1;
        entry.reject(value instanceof Error ? value : new Error(String(value || 'Worker job failed.')));
      }
    }
    pump();
  }

  function run(entry) {
    entry.startedAt = now();
    metrics.started += 1;
    running.set(entry.requestId, entry);
    runningByKey.set(entry.jobKey, entry);
    Promise.resolve()
      .then(() => execute(entry))
      .then(value => settleRun(entry, true, value), error => settleRun(entry, false, error));
  }

  function pump() {
    if (closed) return;
    while (running.size < concurrency && queue.length) {
      queue.sort((left, right) => right.priority - left.priority || left.requestId - right.requestId);
      const entry = queue.shift();
      if (queuedByKey.get(entry.jobKey) === entry) queuedByKey.delete(entry.jobKey);
      if (entry.obsolete) continue;
      run(entry);
    }
  }

  function enqueue({
    jobKey,
    geometryRevision = 0,
    targetRevision = geometryRevision,
    priority = 0,
    payload = null,
    signal = null,
    metadata = null,
  } = {}) {
    if (closed) throw createWorkerCancellationError('Worker scheduler가 종료되었습니다.', 'closed');
    const normalizedKey = String(jobKey || 'default');
    const requestId = ++sequence;
    let resolve;
    let reject;
    const promise = new Promise((onResolve, onReject) => { resolve = onResolve; reject = onReject; });
    const entry = {
      requestId,
      jobKey: normalizedKey,
      geometryRevision,
      targetRevision,
      priority: Number(priority || 0),
      payload,
      metadata,
      submittedAt: now(),
      startedAt: 0,
      obsolete: false,
      settled: false,
      resolve,
      reject,
      detachAbort: null,
    };
    metrics.submitted += 1;

    const queued = queuedByKey.get(normalizedKey);
    if (queued) {
      removeQueued(queued);
      supersede(queued, 'coalesced');
    }
    const active = runningByKey.get(normalizedKey);
    if (active) supersede(active, 'coalesced');

    if (signal) {
      const abort = () => cancelRequest(requestId, 'aborted');
      if (signal.aborted) {
        entry.obsolete = true;
        rejectEntry(entry, createWorkerCancellationError('작업을 취소했습니다.', 'aborted'));
        return Object.freeze({ requestId, promise, cancel: abort });
      }
      signal.addEventListener('abort', abort, { once: true });
      entry.detachAbort = () => signal.removeEventListener('abort', abort);
    }

    queue.push(entry);
    queuedByKey.set(normalizedKey, entry);
    metrics.maxQueueDepth = Math.max(metrics.maxQueueDepth, queue.length);
    pump();
    return Object.freeze({
      requestId,
      promise,
      cancel: () => cancelRequest(requestId),
    });
  }

  function cancelKey(jobKey, reason = 'cancelled') {
    const key = String(jobKey || 'default');
    let changed = false;
    const queued = queuedByKey.get(key);
    if (queued) {
      removeQueued(queued);
      supersede(queued, reason);
      changed = true;
    }
    const active = runningByKey.get(key);
    if (active) {
      supersede(active, reason);
      changed = true;
    }
    return changed;
  }

  function cancelAll(reason = 'cancelled') {
    for (const entry of [...queue]) {
      removeQueued(entry);
      supersede(entry, reason);
    }
    for (const entry of running.values()) supersede(entry, reason);
  }

  function close(reason = 'closed') {
    if (closed) return;
    closed = true;
    cancelAll(reason);
  }

  return Object.freeze({
    enqueue,
    cancel: cancelRequest,
    cancelKey,
    cancelAll,
    close,
    stats: () => Object.freeze({
      ...metrics,
      queueDepth: queue.length,
      runningCount: running.size,
      activeKeys: Object.freeze([...runningByKey.keys()]),
      queuedKeys: Object.freeze([...queuedByKey.keys()]),
      latencyP95Ms: percentile(latencySamples, 0.95),
      latencyP99Ms: percentile(latencySamples, 0.99),
    }),
  });
}
