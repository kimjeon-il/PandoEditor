import { PERFORMANCE_METRIC_NAMES, getRuntimePerformanceMetrics } from './runtime-performance-metrics.js';

export const PROJECT_COMMAND_KINDS = Object.freeze({
  VIEW: 'view',
  DOCUMENT: 'document',
});

const text = value => String(value ?? '').trim();
const isPromiseLike = value => !!value && typeof value.then === 'function';
const metricNow = () => (typeof performance !== 'undefined' ? performance.now() : Date.now());

function recordCommandMetric(startedAt, command, result) {
  getRuntimePerformanceMetrics()?.record?.(
    PERFORMANCE_METRIC_NAMES.COMMAND,
    metricNow() - startedAt,
    {
      command: String(command?.id || result?.command || ''),
      kind: String(command?.kind || result?.kind || ''),
      result: result?.ok === false ? 'failed' : result?.changed === false ? 'unchanged' : 'success',
      stage: String(result?.stage || ''),
      errorCode: String(result?.error?.code || ''),
    },
  );
}

function assertSynchronous(value, stage, commandId) {
  if (isPromiseLike(value)) {
    throw new TypeError(`${commandId} ${stage} returned a Promise. Use runProjectTransaction for asynchronous/geometry transactions.`);
  }
  return value;
}

function commandError(code, message, detail = null) {
  const error = new Error(message);
  error.code = code;
  if (detail != null) error.detail = detail;
  return error;
}

function assertValidation(result, commandId, stage) {
  if (result == null || result === true) return;
  if (result === false) throw commandError('PL-CMD-VALIDATION-001', `${commandId} ${stage} validation failed.`);
  if (typeof result === 'object' && result.ok === false) {
    const issues = result.issues || result.errors || result.code || result;
    throw commandError('PL-CMD-VALIDATION-001', `${commandId} ${stage} validation failed.`, issues);
  }
}

function normalizeDefinition(id, definition = {}) {
  const commandId = text(definition.id || id);
  if (!commandId) throw new TypeError('project command id is required');
  const kind = definition.kind === PROJECT_COMMAND_KINDS.VIEW
    ? PROJECT_COMMAND_KINDS.VIEW
    : PROJECT_COMMAND_KINDS.DOCUMENT;
  if (typeof definition.execute !== 'function') throw new TypeError(`${commandId} requires execute()`);
  return Object.freeze({ ...definition, id: commandId, kind });
}

function resolveValue(value, ...args) {
  return typeof value === 'function' ? value(...args) : value;
}

/**
 * Synchronous application-command pipeline for ordinary UI mutations.
 * Heavy/async geometry work continues to use project-transaction.js.
 */
export function createProjectCommandPipeline({
  commands = {},
  captureSnapshot = () => undefined,
  restoreSnapshot = () => {},
  recordHistory = () => {},
  discardHistory = () => {},
  validateProject = () => true,
  advanceRevision = () => undefined,
  invalidateRender = () => {},
  queueAutosave = () => {},
  onSuccess = () => {},
  onError = () => {},
} = {}) {
  const registry = new Map();

  function register(id, definition) {
    const normalized = normalizeDefinition(id, definition);
    registry.set(normalized.id, normalized);
    return normalized;
  }

  for (const [id, definition] of Object.entries(commands || {})) register(id, definition);

  function runView(command, context, payload) {
    try {
      assertValidation(assertSynchronous(command.validate?.(context, payload), 'validate', command.id), command.id, 'pre');
      const prepared = command.prepare
        ? assertSynchronous(command.prepare(context, payload), 'prepare', command.id)
        : { context, payload };
      if (prepared?.skip === true) return { ok: true, changed: false, command: command.id, kind: command.kind };
      const value = assertSynchronous(command.execute(context, payload, prepared), 'execute', command.id);
      const renderDirty = resolveValue(command.renderDirty, context, payload, value, prepared);
      let renderError = null;
      if (renderDirty != null) {
        try { invalidateRender(renderDirty, command.id, value); }
        catch (error) { renderError = error; }
      }
      onSuccess({ command: command.id, kind: command.kind, context, payload, value, changed: value?.changed !== false });
      return { ok: true, changed: value?.changed !== false, command: command.id, kind: command.kind, value, renderError };
    } catch (error) {
      onError(error, { command: command.id, kind: command.kind, context, payload, stage: 'view' });
      return { ok: false, changed: false, command: command.id, kind: command.kind, error };
    }
  }

  function runDocument(command, context, payload) {
    let snapshot;
    let historyRecorded = false;
    let stage = 'validate';
    try {
      assertValidation(assertSynchronous(command.validate?.(context, payload), 'validate', command.id), command.id, 'pre');
      stage = 'prepare';
      const prepared = command.prepare
        ? assertSynchronous(command.prepare(context, payload), 'prepare', command.id)
        : { context, payload };
      if (prepared?.skip === true) return { ok: true, changed: false, command: command.id, kind: command.kind };

      stage = 'snapshot';
      snapshot = assertSynchronous(captureSnapshot(), 'captureSnapshot', command.id);

      stage = 'history';
      const history = resolveValue(command.history, context, payload, prepared) || {
        type: command.id,
        affectedIds: prepared?.affectedIds || [],
      };
      recordHistory({ ...history, command: command.id }, snapshot);
      historyRecorded = true;

      stage = 'mutate';
      const value = assertSynchronous(command.execute(context, payload, prepared), 'execute', command.id);
      if (value && typeof value === 'object' && value.ok === false) {
        throw commandError(value.code || 'PL-CMD-MUTATION-001', `${command.id} mutation was rejected.`, value);
      }
      if (value?.changed === false) {
        if (historyRecorded) discardHistory();
        return { ok: true, changed: false, command: command.id, kind: command.kind, value };
      }

      stage = 'validate-project';
      assertValidation(assertSynchronous(validateProject({ command: command.id, context, payload, prepared, value }), 'validateProject', command.id), command.id, 'project');

      stage = 'revision';
      const revision = assertSynchronous(advanceRevision({ command: command.id, context, payload, prepared, value }), 'advanceRevision', command.id);

      const renderDirty = resolveValue(command.renderDirty, context, payload, value, prepared);
      let renderError = null;
      if (renderDirty != null) {
        try { invalidateRender(renderDirty, command.id, value); }
        catch (error) { renderError = error; }
      }

      let autosaveError = null;
      if (command.autosave !== false) {
        try { queueAutosave({ command: command.id, context, payload, prepared, value, revision }); }
        catch (error) { autosaveError = error; }
      }

      onSuccess({ command: command.id, kind: command.kind, context, payload, prepared, value, revision, changed: true });
      return {
        ok: true,
        changed: true,
        command: command.id,
        kind: command.kind,
        value,
        revision,
        renderError,
        autosaveError,
      };
    } catch (error) {
      if (snapshot !== undefined) {
        try { restoreSnapshot(snapshot, { command: command.id, stage, error }); }
        catch (restoreError) { error.restoreError = restoreError; }
      }
      if (historyRecorded) {
        try { discardHistory(); }
        catch (discardError) { error.discardHistoryError = discardError; }
      }
      onError(error, { command: command.id, kind: command.kind, context, payload, stage });
      return { ok: false, changed: false, command: command.id, kind: command.kind, error, stage };
    }
  }

  function execute(id, context = {}, payload = undefined) {
    const startedAt = metricNow();
    const command = registry.get(text(id));
    let result;
    if (!command) {
      result = { ok: false, changed: false, command: text(id), error: commandError('PL-CMD-UNKNOWN-001', `Unknown project command: ${id}`) };
      recordCommandMetric(startedAt, null, result);
      return result;
    }
    result = command.kind === PROJECT_COMMAND_KINDS.VIEW
      ? runView(command, context, payload)
      : runDocument(command, context, payload);
    recordCommandMetric(startedAt, command, result);
    return result;
  }

  function runMutation(meta = {}, mutate, options = {}) {
    if (typeof mutate !== 'function') throw new TypeError('runMutation requires a mutation function');
    const id = text(options.command || meta.command || meta.type || 'project.mutate');
    const command = normalizeDefinition(id, {
      kind: PROJECT_COMMAND_KINDS.DOCUMENT,
      history: { ...meta, command: id },
      renderDirty: options.renderDirty ?? 'document',
      autosave: options.autosave !== false,
      validate: options.validate,
      execute: (_context, _payload, prepared) => {
        const result = mutate(prepared);
        return result === undefined ? { changed: true } : result;
      },
    });
    const startedAt = metricNow();
    const result = runDocument(command, options.context || {}, options.payload);
    recordCommandMetric(startedAt, command, result);
    return result;
  }

  return Object.freeze({
    execute,
    register,
    runMutation,
    has: id => registry.has(text(id)),
    get: id => registry.get(text(id)) || null,
    list: () => [...registry.values()],
  });
}
