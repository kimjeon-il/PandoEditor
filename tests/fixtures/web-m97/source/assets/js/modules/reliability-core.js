export const RELIABILITY_ERROR_CATEGORIES = Object.freeze({
  NETWORK: 'NETWORK',
  PARSE: 'PARSE',
  SCHEMA: 'SCHEMA',
  GEOMETRY: 'GEOMETRY',
  RELATION: 'RELATION',
  TRANSACTION: 'TRANSACTION',
  RENDERER: 'RENDERER',
  CACHE: 'CACHE',
  WORKER: 'WORKER',
  STORAGE: 'STORAGE',
  CANCELLED: 'CANCELLED',
  STALE_RESULT: 'STALE_RESULT',
});

export const RELIABILITY_SEVERITIES = Object.freeze({
  INFO: 'info',
  WARNING: 'warning',
  ERROR: 'error',
  FATAL: 'fatal',
});

export function isAbortError(error) {
  return error?.name === 'AbortError' || error?.cancelled === true;
}

export function createCancellationError(message = '작업을 취소했습니다.') {
  const error = new Error(String(message || '작업을 취소했습니다.'));
  error.name = 'AbortError';
  error.cancelled = true;
  return error;
}

export function createOperationalError({
  code = 'PL-INTERNAL-001',
  category = RELIABILITY_ERROR_CATEGORIES.TRANSACTION,
  severity = RELIABILITY_SEVERITIES.ERROR,
  retryable = false,
  userMessage = '',
  technicalMessage = '',
  objectIds = [],
  operationType = '',
  revision = 0,
  cause = null,
} = {}) {
  const error = new Error(technicalMessage || userMessage || code, cause ? { cause } : undefined);
  error.code = code;
  error.category = category;
  error.severity = severity;
  error.retryable = retryable === true;
  error.userMessage = String(userMessage || '');
  error.objectIds = [...new Set((objectIds || []).map(String).filter(Boolean))];
  error.operationType = String(operationType || '');
  error.revision = Number(revision || 0);
  if (cause && !error.cause) error.cause = cause;
  return error;
}

export function isRetryableHttpStatus(status) {
  return [408, 425, 429, 500, 502, 503, 504].includes(Number(status));
}

export function retryDelay(attempt, {
  baseDelay = 400,
  maxDelay = 4000,
  jitter = 0.18,
  random = Math.random,
} = {}) {
  const exponential = Math.min(maxDelay, baseDelay * (2 ** Math.max(0, Number(attempt || 1) - 1)));
  const spread = exponential * Math.max(0, Number(jitter || 0));
  return Math.max(0, Math.round(exponential - spread + random() * spread * 2));
}

async function delayWithSignal(ms, signal) {
  if (signal?.aborted) throw signal.reason || new DOMException('Aborted', 'AbortError');
  await new Promise((resolve, reject) => {
    let settled = false;
    const finish = callback => value => {
      if (settled) return;
      settled = true;
      signal?.removeEventListener('abort', onAbort);
      callback(value);
    };
    const onResolve = finish(resolve);
    const onReject = finish(reject);
    const timer = setTimeout(onResolve, Math.max(0, Number(ms || 0)));
    const onAbort = () => {
      clearTimeout(timer);
      onReject(signal.reason || new DOMException('Aborted', 'AbortError'));
    };
    signal?.addEventListener('abort', onAbort, { once: true });
  });
}

export async function withRetry(operation, {
  maxAttempts = 3,
  baseDelay = 400,
  maxDelay = 4000,
  jitter = 0.18,
  shouldRetry = () => true,
  signal = null,
  onRetry = () => {},
} = {}) {
  let lastError = null;
  const attempts = Math.max(1, Number(maxAttempts || 1));
  for (let attempt = 1; attempt <= attempts; attempt += 1) {
    if (signal?.aborted) throw signal.reason || new DOMException('Aborted', 'AbortError');
    try {
      return await operation({ attempt, signal });
    } catch (error) {
      lastError = error;
      if (isAbortError(error) || attempt >= attempts || !shouldRetry(error, attempt)) throw error;
      const delay = retryDelay(attempt, { baseDelay, maxDelay, jitter });
      onRetry({ attempt, delay, error });
      await delayWithSignal(delay, signal);
    }
  }
  throw lastError;
}

export async function fetchWithRetry(input, init = {}, {
  maxAttempts = 3,
  baseDelay = 400,
  maxDelay = 4000,
  jitter = 0.18,
  timeoutMs = 15000,
  signal = init.signal || null,
  retryStatuses = isRetryableHttpStatus,
  onRetry = () => {},
} = {}) {
  return withRetry(async ({ signal: retrySignal }) => {
    const controller = new AbortController();
    let timedOut = false;
    const abort = () => controller.abort(retrySignal?.reason);
    retrySignal?.addEventListener('abort', abort, { once: true });
    const timeout = setTimeout(() => {
      timedOut = true;
      controller.abort(new DOMException('Request timed out', 'TimeoutError'));
    }, Math.max(1000, Number(timeoutMs || 15000)));
    try {
      const response = await fetch(input, { ...init, signal: controller.signal });
      if (!response.ok && retryStatuses(response.status)) {
        const error = new Error(`HTTP ${response.status}`);
        error.httpStatus = response.status;
        error.retryable = true;
        throw error;
      }
      return response;
    } catch (error) {
      if (timedOut) {
        const timeoutError = new Error('Request timed out');
        timeoutError.name = 'TimeoutError';
        timeoutError.retryable = true;
        throw timeoutError;
      }
      throw error;
    } finally {
      clearTimeout(timeout);
      retrySignal?.removeEventListener('abort', abort);
    }
  }, {
    maxAttempts, baseDelay, maxDelay, jitter, signal, onRetry,
    shouldRetry: error => {
      if (isAbortError(error)) return false;
      if (error?.retryable === true || error?.name === 'TimeoutError' || error instanceof TypeError) return true;
      if (Number.isFinite(Number(error?.httpStatus))) return retryStatuses(error.httpStatus);
      return false;
    },
  });
}

export function createAssetState(initial = {}) {
  const state = {
    manifest: 'idle',
    worker: 'idle',
    view: 'idle',
    cache: 'idle',
    lastError: null,
    ...initial,
  };
  return {
    state,
    set(part, value, error = null) {
      if (!Object.prototype.hasOwnProperty.call(state, part)) throw new Error(`Unknown asset state part: ${part}`);
      state[part] = value;
      if (error) state.lastError = error;
      return { ...state };
    },
    snapshot() { return Object.freeze({ ...state }); },
  };
}

export function createDiagnosticLog({ limit = 200, now = () => typeof performance !== 'undefined' ? performance.now() : Date.now() } = {}) {
  const events = [];
  function push(event = {}) {
    events.push(Object.freeze({
      timestamp: now(),
      category: String(event.category || 'internal'),
      operation: String(event.operation || ''),
      revision: Number(event.revision || 0),
      objectIds: [...new Set((event.objectIds || []).map(String).filter(Boolean))],
      duration: Number(event.duration || 0),
      result: String(event.result || ''),
      errorCode: String(event.errorCode || ''),
      technicalMessage: String(event.technicalMessage || ''),
      stack: String(event.stack || ''),
      rollback: String(event.rollback || ''),
    }));
    while (events.length > Math.max(1, Number(limit || 200))) events.shift();
    return events[events.length - 1];
  }
  return Object.freeze({
    push,
    snapshot: () => events.slice(),
    clear: () => { events.length = 0; },
  });
}
