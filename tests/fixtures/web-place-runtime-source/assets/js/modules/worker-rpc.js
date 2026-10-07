export const WORKER_RPC_PROTOCOL = 'pandolab-worker-rpc';
export const WORKER_RPC_PROTOCOL_VERSION = 1;

export const WORKER_RPC_MESSAGE_TYPES = Object.freeze({
  REQUEST: 'request',
  RESULT: 'result',
  CANCEL: 'cancel',
  EVENT: 'event',
});

export const WORKER_RPC_ERROR_CATEGORIES = Object.freeze({
  WORKER: 'WORKER',
  OPERATION: 'OPERATION',
  CANCELLED: 'CANCELLED',
  TIMEOUT: 'TIMEOUT',
  STALE_RESULT: 'STALE_RESULT',
  PROTOCOL: 'PROTOCOL',
});

const text = value => String(value ?? '').trim();
const finiteRevision = value => Number.isFinite(Number(value)) ? Number(value) : 0;
const defaultNow = () => globalThis.performance?.now?.() ?? Date.now();

function createWorkerRpcError({
  message = 'Worker 작업에 실패했습니다.',
  category = WORKER_RPC_ERROR_CATEGORIES.WORKER,
  code = 'PL-WORKER-RPC-001',
  operation = '',
  requestId = 0,
  projectRevision = 0,
  retryable = false,
  cause = null,
} = {}) {
  const error = new Error(text(message) || 'Worker 작업에 실패했습니다.', cause ? { cause } : undefined);
  error.code = text(code) || 'PL-WORKER-RPC-001';
  error.category = text(category) || WORKER_RPC_ERROR_CATEGORIES.WORKER;
  error.operation = text(operation);
  error.requestId = Number(requestId || 0);
  error.projectRevision = finiteRevision(projectRevision);
  error.retryable = retryable === true;
  if (error.category === WORKER_RPC_ERROR_CATEGORIES.CANCELLED || error.category === WORKER_RPC_ERROR_CATEGORIES.STALE_RESULT) {
    error.name = 'AbortError';
    error.cancelled = true;
  }
  return error;
}

function createCanonicalWorkerRpcCodec() {
  return Object.freeze({
    encodeRequest(envelope) { return envelope; },
    encodeCancel(envelope) { return envelope; },
    encodeEvent(envelope) { return envelope; },
    decodeMessage(message) {
      if (message?.rpc !== WORKER_RPC_PROTOCOL || Number(message?.protocolVersion) !== WORKER_RPC_PROTOCOL_VERSION) return null;
      if (message.type === WORKER_RPC_MESSAGE_TYPES.EVENT) {
        return { kind: 'event', operation: text(message.operation), payload: message.payload, projectRevision: finiteRevision(message.projectRevision), timing: message.timing || null };
      }
      if (message.type !== WORKER_RPC_MESSAGE_TYPES.RESULT) return null;
      return {
        kind: 'result',
        requestId: Number(message.requestId || 0),
        operation: text(message.operation),
        projectRevision: finiteRevision(message.projectRevision),
        ok: message.ok === true,
        result: message.result,
        error: message.error || null,
        timing: message.timing || null,
      };
    },
  });
}

export function createWorkerRpcClient({
  createWorker,
  codec = createCanonicalWorkerRpcCodec(),
  defaultTimeoutMs = 30_000,
  getProjectRevision = () => 0,
  isCurrent = null,
  onEvent = () => {},
  onCrash = () => {},
  restartOnCrash = true,
  now = defaultNow,
  schedule = (callback, delay) => setTimeout(callback, delay),
  clearSchedule = handle => clearTimeout(handle),
} = {}) {
  if (typeof createWorker !== 'function') throw new TypeError('Worker RPC createWorker callback is required.');
  for (const method of ['encodeRequest', 'encodeCancel', 'encodeEvent', 'decodeMessage']) {
    if (typeof codec?.[method] !== 'function') throw new TypeError(`Worker RPC codec.${method}() is required.`);
  }

  let worker = null;
  let sequence = 0;
  let closed = false;
  const pending = new Map();
  const metrics = {
    submitted: 0,
    completed: 0,
    failed: 0,
    cancelled: 0,
    timedOut: 0,
    staleDiscarded: 0,
    crashes: 0,
    restarted: 0,
    transferredRequests: 0,
    lastDurationMs: 0,
  };

  function currentRevision() {
    return finiteRevision(typeof getProjectRevision === 'function' ? getProjectRevision() : 0);
  }

  function cleanupPending(entry) {
    if (!entry) return;
    if (entry.timeoutHandle != null) clearSchedule(entry.timeoutHandle);
    entry.signal?.removeEventListener?.('abort', entry.abortHandler);
    pending.delete(entry.requestId);
  }

  function rejectEntry(entry, error) {
    if (!entry || entry.settled) return;
    entry.settled = true;
    cleanupPending(entry);
    entry.reject(error);
  }

  function rpcErrorFromMessage(entry, value) {
    const raw = value && typeof value === 'object' ? value : {};
    return createWorkerRpcError({
      message: raw.message || 'Worker 작업에 실패했습니다.',
      category: raw.category || WORKER_RPC_ERROR_CATEGORIES.OPERATION,
      code: raw.code || 'PL-WORKER-RPC-OPERATION',
      operation: entry.operation,
      requestId: entry.requestId,
      projectRevision: entry.projectRevision,
      retryable: raw.retryable === true,
    });
  }

  function staleFor(entry, decoded) {
    if (typeof isCurrent === 'function') return isCurrent({ entry, message: decoded }) === false;
    const expected = finiteRevision(entry.projectRevision);
    if (!expected) return false;
    const responseRevision = finiteRevision(decoded.projectRevision || expected);
    return responseRevision !== expected || currentRevision() !== expected;
  }

  function handleMessage(event) {
    const decoded = codec.decodeMessage(event?.data);
    if (!decoded) return;
    if (decoded.kind === 'event') {
      onEvent(decoded);
      return;
    }
    if (decoded.kind !== 'result') return;
    const entry = pending.get(Number(decoded.requestId || 0));
    if (!entry || entry.settled) return;
    if (staleFor(entry, decoded)) {
      metrics.staleDiscarded += 1;
      rejectEntry(entry, createWorkerRpcError({
        message: '프로젝트 상태가 바뀌어 오래된 Worker 결과를 폐기했습니다.',
        category: WORKER_RPC_ERROR_CATEGORIES.STALE_RESULT,
        code: 'PL-WORKER-RPC-STALE',
        operation: entry.operation,
        requestId: entry.requestId,
        projectRevision: entry.projectRevision,
      }));
      return;
    }
    entry.settled = true;
    cleanupPending(entry);
    metrics.lastDurationMs = Math.max(0, Number(decoded.timing?.durationMs ?? now() - entry.startedAt) || 0);
    if (decoded.ok) {
      metrics.completed += 1;
      entry.resolve(Object.freeze({
        requestId: entry.requestId,
        operation: entry.operation,
        projectRevision: entry.projectRevision,
        result: decoded.result,
        timing: decoded.timing || Object.freeze({ durationMs: metrics.lastDurationMs }),
      }));
    } else {
      metrics.failed += 1;
      entry.reject(rpcErrorFromMessage(entry, decoded.error));
    }
  }

  function rejectAll(error) {
    for (const entry of [...pending.values()]) rejectEntry(entry, error);
  }

  function handleCrash(event) {
    metrics.crashes += 1;
    const error = createWorkerRpcError({
      message: event?.message || 'Worker가 예기치 않게 종료되었습니다.',
      category: WORKER_RPC_ERROR_CATEGORIES.WORKER,
      code: 'PL-WORKER-RPC-CRASH',
      retryable: true,
    });
    rejectAll(error);
    try { worker?.terminate?.(); } catch (_) { /* best effort */ }
    worker = null;
    onCrash(error);
  }

  function ensureWorker() {
    if (closed) throw createWorkerRpcError({ message: 'Worker RPC client가 종료되었습니다.', category: WORKER_RPC_ERROR_CATEGORIES.CANCELLED, code: 'PL-WORKER-RPC-CLOSED' });
    if (worker) return worker;
    worker = createWorker();
    worker.onmessage = handleMessage;
    worker.onerror = handleCrash;
    if (metrics.crashes && restartOnCrash) metrics.restarted += 1;
    return worker;
  }

  function postCancel(entry, reason = 'cancelled') {
    if (!worker || !entry) return;
    const envelope = {
      rpc: WORKER_RPC_PROTOCOL,
      protocolVersion: WORKER_RPC_PROTOCOL_VERSION,
      type: WORKER_RPC_MESSAGE_TYPES.CANCEL,
      requestId: entry.requestId,
      operation: entry.operation,
      projectRevision: entry.projectRevision,
      reason: text(reason) || 'cancelled',
    };
    try { worker.postMessage(codec.encodeCancel(envelope)); } catch (_) { /* cancellation is best effort */ }
  }

  function cancel(requestId, reason = 'cancelled') {
    const entry = pending.get(Number(requestId || 0));
    if (!entry || entry.settled) return false;
    postCancel(entry, reason);
    metrics.cancelled += 1;
    rejectEntry(entry, createWorkerRpcError({
      message: 'Worker 작업을 취소했습니다.',
      category: WORKER_RPC_ERROR_CATEGORIES.CANCELLED,
      code: 'PL-WORKER-RPC-CANCELLED',
      operation: entry.operation,
      requestId: entry.requestId,
      projectRevision: entry.projectRevision,
    }));
    return true;
  }

  function request(operation, payload = null, {
    requestId = null,
    projectRevision = currentRevision(),
    priority = 0,
    timeoutMs = defaultTimeoutMs,
    signal = null,
    transfer = [],
    metadata = null,
  } = {}) {
    const resolvedOperation = text(operation);
    if (!resolvedOperation) throw new TypeError('Worker RPC operation is required.');
    const resolvedRequestId = requestId == null ? sequence + 1 : Number(requestId);
    if (!Number.isInteger(resolvedRequestId) || resolvedRequestId <= 0) throw new TypeError('Worker RPC requestId must be a positive integer.');
    sequence = Math.max(sequence, resolvedRequestId);
    if (pending.has(resolvedRequestId)) throw new Error(`Worker RPC requestId ${resolvedRequestId} is already pending.`);
    if (signal?.aborted) {
      return Promise.reject(createWorkerRpcError({
        message: 'Worker 작업을 취소했습니다.',
        category: WORKER_RPC_ERROR_CATEGORIES.CANCELLED,
        code: 'PL-WORKER-RPC-ABORTED',
        operation: resolvedOperation,
        requestId: resolvedRequestId,
        projectRevision,
      }));
    }

    const startedAt = now();
    const envelope = {
      rpc: WORKER_RPC_PROTOCOL,
      protocolVersion: WORKER_RPC_PROTOCOL_VERSION,
      type: WORKER_RPC_MESSAGE_TYPES.REQUEST,
      requestId: resolvedRequestId,
      operation: resolvedOperation,
      projectRevision: finiteRevision(projectRevision),
      priority: Number(priority || 0),
      sentAt: startedAt,
      payload,
      metadata,
    };
    let resolve;
    let reject;
    const promise = new Promise((onResolve, onReject) => { resolve = onResolve; reject = onReject; });
    const entry = {
      requestId: resolvedRequestId,
      operation: resolvedOperation,
      projectRevision: envelope.projectRevision,
      priority: envelope.priority,
      metadata,
      startedAt,
      resolve,
      reject,
      settled: false,
      timeoutHandle: null,
      signal,
      abortHandler: null,
    };
    pending.set(resolvedRequestId, entry);
    metrics.submitted += 1;

    const timeout = Math.max(0, Number(timeoutMs || 0));
    if (timeout > 0) {
      entry.timeoutHandle = schedule(() => {
        if (!pending.has(entry.requestId) || entry.settled) return;
        postCancel(entry, 'timeout');
        metrics.timedOut += 1;
        rejectEntry(entry, createWorkerRpcError({
          message: `Worker 작업이 ${timeout}ms 제한 시간을 초과했습니다.`,
          category: WORKER_RPC_ERROR_CATEGORIES.TIMEOUT,
          code: 'PL-WORKER-RPC-TIMEOUT',
          operation: entry.operation,
          requestId: entry.requestId,
          projectRevision: entry.projectRevision,
          retryable: true,
        }));
      }, timeout);
    }
    if (signal) {
      entry.abortHandler = () => cancel(entry.requestId, 'aborted');
      signal.addEventListener('abort', entry.abortHandler, { once: true });
    }

    try {
      const encoded = codec.encodeRequest(envelope);
      const transferables = Array.isArray(transfer) ? transfer.filter(Boolean) : [];
      if (transferables.length) {
        metrics.transferredRequests += 1;
        ensureWorker().postMessage(encoded, transferables);
      } else {
        ensureWorker().postMessage(encoded);
      }
    } catch (error) {
      rejectEntry(entry, createWorkerRpcError({
        message: error?.message || 'Worker 요청을 전송하지 못했습니다.',
        category: WORKER_RPC_ERROR_CATEGORIES.WORKER,
        code: 'PL-WORKER-RPC-SEND',
        operation: entry.operation,
        requestId: entry.requestId,
        projectRevision: entry.projectRevision,
        cause: error,
      }));
    }
    return promise;
  }

  function notify(operation, payload = null, {
    projectRevision = currentRevision(),
    priority = 0,
    transfer = [],
    metadata = null,
  } = {}) {
    const envelope = {
      rpc: WORKER_RPC_PROTOCOL,
      protocolVersion: WORKER_RPC_PROTOCOL_VERSION,
      type: WORKER_RPC_MESSAGE_TYPES.EVENT,
      operation: text(operation),
      projectRevision: finiteRevision(projectRevision),
      priority: Number(priority || 0),
      sentAt: now(),
      payload,
      metadata,
    };
    if (!envelope.operation) throw new TypeError('Worker RPC event operation is required.');
    const encoded = codec.encodeEvent(envelope);
    const transferables = Array.isArray(transfer) ? transfer.filter(Boolean) : [];
    if (transferables.length) ensureWorker().postMessage(encoded, transferables);
    else ensureWorker().postMessage(encoded);
  }

  function cancelAll(reason = 'cancelled') {
    for (const requestId of [...pending.keys()]) cancel(requestId, reason);
  }

  function stop(reason = 'stopped') {
    if (closed) return;
    cancelAll(reason);
    closed = true;
    try { worker?.terminate?.(); } catch (_) { /* best effort */ }
    worker = null;
  }

  return Object.freeze({
    request,
    notify,
    cancel,
    cancelAll,
    stop,
    stats: () => Object.freeze({
      ...metrics,
      pendingCount: pending.size,
      projectRevision: currentRevision(),
      workerActive: !!worker,
      closed,
    }),
  });
}
