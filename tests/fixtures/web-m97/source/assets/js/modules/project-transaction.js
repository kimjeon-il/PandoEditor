import {
  RELIABILITY_ERROR_CATEGORIES,
  RELIABILITY_SEVERITIES,
  createOperationalError,
  isAbortError,
} from './reliability-core.js';
import { PERFORMANCE_METRIC_NAMES, getRuntimePerformanceMetrics } from './runtime-performance-metrics.js';

function validationError(operationType, issues) {
  const detail = Array.isArray(issues) ? issues : [issues].filter(Boolean);
  const error = createOperationalError({
    code: 'PL-TX-VALIDATION-001',
    category: RELIABILITY_ERROR_CATEGORIES.TRANSACTION,
    severity: RELIABILITY_SEVERITIES.ERROR,
    retryable: false,
    operationType,
    technicalMessage: `Transaction validation failed: ${detail.map(item => item?.message || item).join(' / ')}`,
  });
  error.issues = detail;
  return error;
}

function assertValidationResult(result, operationType) {
  if (result == null || result === true) return;
  if (result === false) throw validationError(operationType, ['validation returned false']);
  if (typeof result === 'object' && result.ok === false) {
    throw validationError(operationType, result.issues || result.errors || []);
  }
}

function metricNow() {
  return typeof performance !== 'undefined' ? performance.now() : Date.now();
}

function elapsed(startedAt) {
  return metricNow() - startedAt;
}

function recordTransactionMetric(startedAt, operationType, result, stage, stageDurations, detail = {}) {
  getRuntimePerformanceMetrics()?.record?.(
    PERFORMANCE_METRIC_NAMES.TRANSACTION,
    elapsed(startedAt),
    {
      operationType,
      result,
      finalStage: stage,
      stages: stageDurations,
      ...detail,
    },
  );
}

async function runNonCriticalSideEffect({
  stage,
  action,
  diagnostic,
  operationType,
  onSideEffectError,
}) {
  try {
    await action();
    return null;
  } catch (error) {
    diagnostic?.push?.({
      category: stage === 'autosave' ? 'storage' : 'transaction',
      operation: operationType,
      result: `${stage}-failed`,
      errorCode: String(error?.code || (stage === 'autosave' ? 'PL-STORAGE-AUTOSAVE-001' : 'PL-TX-SIDE-EFFECT-001')),
    });
    try { await onSideEffectError(error, { stage, operationType }); }
    catch (callbackError) { console.error('[PL-TX-SIDE-EFFECT-002]', callbackError); }
    return error;
  }
}

export async function runProjectTransaction({
  operationType = 'project-edit',
  snapshot,
  prepare = async () => undefined,
  normalize = value => value,
  validatePrepared = () => true,
  applyCanonical = async value => value,
  validateCanonical = () => true,
  commitExternal = async () => {},
  commitHistory = () => {},
  queueAutosave = () => {},
  restore = async () => {},
  onSuccess = () => {},
  onError = () => {},
  onSideEffectError = () => {},
  diagnostic = null,
} = {}) {
  let stage = 'prepare';
  let stageStartedAt = metricNow();
  let externalCommitted = false;
  let historyCommitted = false;
  const startedAt = stageStartedAt;
  const stageDurations = {};
  const enterStage = nextStage => {
    stageDurations[stage] = Math.round((Number(stageDurations[stage] || 0) + elapsed(stageStartedAt)) * 100) / 100;
    stage = nextStage;
    stageStartedAt = metricNow();
  };
  const finishStage = () => {
    stageDurations[stage] = Math.round((Number(stageDurations[stage] || 0) + elapsed(stageStartedAt)) * 100) / 100;
  };

  try {
    const prepared = await prepare();

    enterStage('normalize');
    const normalized = await normalize(prepared);

    enterStage('validate-prepared');
    assertValidationResult(await validatePrepared(normalized), operationType);

    enterStage('apply-canonical');
    const applyOutput = await applyCanonical(normalized);
    const applied = applyOutput === undefined ? normalized : applyOutput;

    enterStage('validate-canonical');
    assertValidationResult(await validateCanonical(applied, normalized), operationType);

    enterStage('commit-external');
    await commitExternal(applied, normalized);
    externalCommitted = true;

    enterStage('history');
    await commitHistory(snapshot, applied, normalized);
    historyCommitted = true;

    // Autosave and success-notification hooks are intentionally non-critical.
    // Once canonical state + history are committed, a storage/UI side effect must
    // never roll the data model back to an older snapshot.
    enterStage('autosave');
    const autosaveError = await runNonCriticalSideEffect({
      stage,
      action: () => queueAutosave(applied, normalized),
      diagnostic,
      operationType,
      onSideEffectError,
    });

    enterStage('success');
    const successHookError = await runNonCriticalSideEffect({
      stage: 'success-hook',
      action: () => onSuccess(applied, normalized),
      diagnostic,
      operationType,
      onSideEffectError,
    });
    finishStage();

    diagnostic?.push?.({
      category: 'transaction',
      operation: operationType,
      result: autosaveError || successHookError ? 'success-with-warning' : 'success',
      duration: elapsed(startedAt),
      errorCode: autosaveError?.code || successHookError?.code || '',
    });
    recordTransactionMetric(
      startedAt,
      operationType,
      autosaveError || successHookError ? 'success-with-warning' : 'success',
      stage,
      stageDurations,
      {
        externalCommitted,
        historyCommitted,
        autosaveError: String(autosaveError?.code || ''),
        successHookError: String(successHookError?.code || ''),
      },
    );

    return {
      ok: true,
      result: applied,
      prepared: normalized,
      externalCommitted,
      historyCommitted,
      autosaveError,
      successHookError,
    };
  } catch (error) {
    finishStage();
    const cancelled = isAbortError(error);
    try {
      const restoreStartedAt = metricNow();
      await restore(snapshot, {
        stage,
        externalCommitted,
        historyCommitted,
        operationType,
        error,
      });
      stageDurations.restore = Math.round(elapsed(restoreStartedAt) * 100) / 100;
    } catch (restoreError) {
      diagnostic?.push?.({
        category: 'transaction',
        operation: operationType,
        result: 'restore-failed',
        errorCode: 'PL-TX-RESTORE-001',
      });
      if (!cancelled) console.error('[PL-TX-RESTORE-001]', restoreError);
    }

    if (!cancelled) await onError(error, { stage, externalCommitted, historyCommitted, operationType });
    diagnostic?.push?.({
      category: 'transaction',
      operation: operationType,
      result: cancelled ? 'cancelled' : 'failed',
      errorCode: String(error?.code || 'PL-TX-001'),
      duration: elapsed(startedAt),
    });
    recordTransactionMetric(
      startedAt,
      operationType,
      cancelled ? 'cancelled' : 'failed',
      stage,
      stageDurations,
      {
        externalCommitted,
        historyCommitted,
        errorCode: String(error?.code || 'PL-TX-001'),
      },
    );
    return { ok: false, cancelled, error, stage, externalCommitted, historyCommitted };
  }
}
