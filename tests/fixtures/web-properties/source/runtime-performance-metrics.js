const DEFAULT_SAMPLE_LIMIT = 120;
const FIRST_LOAD_WINDOW_MS = 5_000;
const EVENT_DURATION_FLOOR_MS = 16;

export const PERFORMANCE_METRIC_NAMES = Object.freeze({
  AUTOSAVE: 'autosave.persist',
  COMMAND: 'project.command',
  TRANSACTION: 'project.transaction',
  MOBILE_SHEET_DRAG: 'mobile-sheet.drag',
  COMMIT_TO_PAINT: 'ui.commit-to-paint',
  MAP_RESIZE: 'map.resize',
});

const PERFORMANCE_DIAGNOSTIC_THRESHOLDS = Object.freeze({
  firstLoadWindowMs: FIRST_LOAD_WINDOW_MS,
  longTaskMs: 50,
  interactionFrameBudgetMs: 16.7,
  slowInteractionFrameMs: 33.4,
  commitToPaintMs: 100,
  autosaveMs: 750,
});

const finite = value => Number.isFinite(Number(value));
const round = value => Math.round(Number(value || 0) * 100) / 100;
const nowFrom = performanceObject => (
  typeof performanceObject?.now === 'function' ? performanceObject.now() : Date.now()
);

function detailValue(value, depth = 0) {
  if (value == null || typeof value === 'string' || typeof value === 'boolean') return value;
  if (typeof value === 'number') return finite(value) ? round(value) : null;
  if (depth >= 4) return String(value);
  if (Array.isArray(value)) return value.slice(0, 20).map(item => detailValue(item, depth + 1));
  if (typeof value === 'object') {
    return Object.fromEntries(Object.entries(value).slice(0, 30).map(([key, item]) => [key, detailValue(item, depth + 1)]));
  }
  return String(value);
}

function percentile(values, ratio) {
  if (!values.length) return 0;
  const sorted = [...values].sort((left, right) => left - right);
  return sorted[Math.min(sorted.length - 1, Math.max(0, Math.ceil(sorted.length * ratio) - 1))];
}

function summarizeSamples(samples) {
  const durations = samples.map(sample => Number(sample.durationMs || 0)).filter(finite);
  const total = durations.reduce((sum, value) => sum + value, 0);
  return {
    count: durations.length,
    totalMs: round(total),
    averageMs: round(durations.length ? total / durations.length : 0),
    p95Ms: round(percentile(durations, 0.95)),
    maxMs: round(durations.length ? Math.max(...durations) : 0),
    last: samples.at(-1) || null,
    samples: samples.map(sample => ({ ...sample })),
  };
}

function targetName(target) {
  if (!target || typeof target !== 'object') return '';
  const node = target.closest?.('[data-action], [data-tool], button, input, select, textarea, [role="button"]') || target;
  return String(
    node.id
      || node.dataset?.action
      || node.dataset?.tool
      || node.getAttribute?.('name')
      || node.getAttribute?.('role')
      || node.tagName
      || '',
  ).trim();
}

export function createRuntimePerformanceMetrics({
  globalObject = globalThis,
  performanceObject = globalObject.performance,
  PerformanceObserverCtor = globalObject.PerformanceObserver,
  requestFrame = globalObject.requestAnimationFrame?.bind(globalObject),
  cancelFrame = globalObject.cancelAnimationFrame?.bind(globalObject),
  maxSamples = DEFAULT_SAMPLE_LIMIT,
} = {}) {
  const startedAt = nowFrom(performanceObject);
  const operationSamples = new Map();
  const marks = [];
  const memory = [];
  const activeInteractions = new Map();
  const observers = [];
  const longTasks = [];
  const eventTimings = [];
  const layoutShifts = [];
  let interactionId = 0;
  let disposed = false;

  const trim = samples => {
    while (samples.length > maxSamples) samples.shift();
  };

  function record(name, durationMs, detail = {}) {
    if (disposed || !name || !finite(durationMs)) return null;
    const sample = {
      name: String(name),
      durationMs: round(Math.max(0, Number(durationMs))),
      atMs: round(nowFrom(performanceObject)),
      detail: detailValue(detail),
    };
    const samples = operationSamples.get(sample.name) || [];
    samples.push(sample);
    trim(samples);
    operationSamples.set(sample.name, samples);
    return sample;
  }

  function mark(name, detail = {}) {
    if (disposed || !name) return null;
    const sample = {
      name: String(name),
      atMs: round(nowFrom(performanceObject)),
      sinceInstallMs: round(nowFrom(performanceObject) - startedAt),
      detail: detailValue(detail),
    };
    marks.push(sample);
    trim(marks);
    return sample;
  }

  function sampleMemory(label = 'runtime') {
    const source = performanceObject?.memory;
    if (!source) return null;
    const sample = {
      label: String(label),
      atMs: round(nowFrom(performanceObject)),
      usedJSHeapSize: finite(source.usedJSHeapSize) ? Number(source.usedJSHeapSize) : null,
      totalJSHeapSize: finite(source.totalJSHeapSize) ? Number(source.totalJSHeapSize) : null,
      jsHeapSizeLimit: finite(source.jsHeapSizeLimit) ? Number(source.jsHeapSizeLimit) : null,
    };
    memory.push(sample);
    trim(memory);
    return sample;
  }

  function beginInteraction(name, detail = {}) {
    if (disposed || !name) return null;
    const token = `${String(name)}:${++interactionId}`;
    activeInteractions.set(token, {
      token,
      name: String(name),
      startedAt: nowFrom(performanceObject),
      detail: detailValue(detail),
      frameCount: 0,
      frameIntervals: [],
      lastFrameAt: null,
      longTaskCount: 0,
      longTaskDurationMs: 0,
      frameHandle: 0,
    });
    return token;
  }

  function sampleInteractionFrame(token, timestamp = nowFrom(performanceObject)) {
    const interaction = activeInteractions.get(token);
    if (!interaction || !finite(timestamp)) return false;
    const at = Number(timestamp);
    if (interaction.lastFrameAt != null) interaction.frameIntervals.push(Math.max(0, at - interaction.lastFrameAt));
    interaction.lastFrameAt = at;
    interaction.frameCount += 1;
    trim(interaction.frameIntervals);
    return true;
  }

  function startInteractionFrames(token) {
    const interaction = activeInteractions.get(token);
    if (!interaction || typeof requestFrame !== 'function' || interaction.frameHandle) return false;
    const loop = timestamp => {
      const current = activeInteractions.get(token);
      if (!current) return;
      sampleInteractionFrame(token, timestamp);
      current.frameHandle = requestFrame(loop);
    };
    interaction.frameHandle = requestFrame(loop);
    return true;
  }

  function endInteraction(token, detail = {}) {
    const interaction = activeInteractions.get(token);
    if (!interaction) return null;
    activeInteractions.delete(token);
    if (interaction.frameHandle && typeof cancelFrame === 'function') cancelFrame(interaction.frameHandle);
    const durationMs = Math.max(0, nowFrom(performanceObject) - interaction.startedAt);
    const intervals = interaction.frameIntervals;
    const slowFrames = intervals.filter(value => value > PERFORMANCE_DIAGNOSTIC_THRESHOLDS.slowInteractionFrameMs).length;
    const missedFrames = intervals.reduce((sum, value) => (
      sum + Math.max(0, Math.round(value / PERFORMANCE_DIAGNOSTIC_THRESHOLDS.interactionFrameBudgetMs) - 1)
    ), 0);
    return record(interaction.name, durationMs, {
      ...interaction.detail,
      ...detailValue(detail),
      frameCount: interaction.frameCount,
      averageFrameMs: round(intervals.length ? intervals.reduce((sum, value) => sum + value, 0) / intervals.length : 0),
      maxFrameMs: round(intervals.length ? Math.max(...intervals) : 0),
      slowFrameCount: slowFrames,
      estimatedMissedFrames: missedFrames,
      longTaskCount: interaction.longTaskCount,
      longTaskDurationMs: round(interaction.longTaskDurationMs),
    });
  }

  function recordLongTask(entry) {
    const sample = {
      startTime: round(entry.startTime),
      durationMs: round(entry.duration),
      name: String(entry.name || 'longtask'),
    };
    longTasks.push(sample);
    trim(longTasks);
    const taskStart = Number(entry.startTime || 0);
    const taskEnd = taskStart + Number(entry.duration || 0);
    for (const interaction of activeInteractions.values()) {
      const interactionStart = interaction.startedAt;
      if (taskEnd < interactionStart || taskStart > nowFrom(performanceObject)) continue;
      interaction.longTaskCount += 1;
      interaction.longTaskDurationMs += Number(entry.duration || 0);
    }
  }

  function installObserver(type, callback, options = {}) {
    if (typeof PerformanceObserverCtor !== 'function') return false;
    try {
      const observer = new PerformanceObserverCtor(list => callback(list.getEntries()));
      observer.observe({ type, buffered: true, ...options });
      observers.push(observer);
      return true;
    } catch (_) {
      return false;
    }
  }

  function observe() {
    const support = {
      longtask: installObserver('longtask', entries => entries.forEach(recordLongTask)),
      event: installObserver('event', entries => {
        for (const entry of entries) {
          if (Number(entry.duration || 0) < EVENT_DURATION_FLOOR_MS) continue;
          eventTimings.push({
            name: String(entry.name || 'event'),
            target: targetName(entry.target),
            startTime: round(entry.startTime),
            durationMs: round(entry.duration),
            interactionId: Number(entry.interactionId || 0),
          });
          trim(eventTimings);
        }
      }, { durationThreshold: EVENT_DURATION_FLOOR_MS }),
      layoutShift: installObserver('layout-shift', entries => {
        for (const entry of entries) {
          if (entry.hadRecentInput) continue;
          layoutShifts.push({ startTime: round(entry.startTime), value: round(entry.value) });
          trim(layoutShifts);
        }
      }),
    };
    return support;
  }

  function browserTimings() {
    const paints = typeof performanceObject?.getEntriesByType === 'function'
      ? performanceObject.getEntriesByType('paint') || []
      : [];
    const navigation = typeof performanceObject?.getEntriesByType === 'function'
      ? performanceObject.getEntriesByType('navigation')?.[0] || null
      : null;
    return {
      paint: Object.fromEntries(paints.map(entry => [String(entry.name || 'paint'), round(entry.startTime)])),
      navigation: navigation ? {
        responseEndMs: round(navigation.responseEnd),
        domInteractiveMs: round(navigation.domInteractive),
        domContentLoadedMs: round(navigation.domContentLoadedEventEnd),
        loadEventEndMs: round(navigation.loadEventEnd),
        transferSize: finite(navigation.transferSize) ? Number(navigation.transferSize) : null,
        decodedBodySize: finite(navigation.decodedBodySize) ? Number(navigation.decodedBodySize) : null,
      } : null,
    };
  }

  function snapshot() {
    const operations = Object.fromEntries([...operationSamples.entries()].map(([name, samples]) => [name, summarizeSamples(samples)]));
    const firstLoadLongTasks = longTasks.filter(sample => sample.startTime <= FIRST_LOAD_WINDOW_MS);
    const longTaskTotal = longTasks.reduce((sum, sample) => sum + sample.durationMs, 0);
    const firstLoadTotal = firstLoadLongTasks.reduce((sum, sample) => sum + sample.durationMs, 0);
    const layoutShiftScore = layoutShifts.reduce((sum, sample) => sum + sample.value, 0);
    return {
      version: 1,
      capturedAt: new Date().toISOString(),
      installedAtMs: round(startedAt),
      startup: detailValue(globalObject.__PANDOLAB_STARTUP_METRICS__ || null),
      browser: browserTimings(),
      marks: marks.map(sample => ({ ...sample })),
      operations,
      longTasks: {
        count: longTasks.length,
        totalMs: round(longTaskTotal),
        maxMs: round(longTasks.length ? Math.max(...longTasks.map(sample => sample.durationMs)) : 0),
        firstLoadWindowMs: FIRST_LOAD_WINDOW_MS,
        firstLoadCount: firstLoadLongTasks.length,
        firstLoadTotalMs: round(firstLoadTotal),
        samples: longTasks.map(sample => ({ ...sample })),
      },
      eventTimings: {
        count: eventTimings.length,
        maxMs: round(eventTimings.length ? Math.max(...eventTimings.map(sample => sample.durationMs)) : 0),
        samples: eventTimings.map(sample => ({ ...sample })),
      },
      layoutShifts: {
        count: layoutShifts.length,
        score: round(layoutShiftScore),
        samples: layoutShifts.map(sample => ({ ...sample })),
      },
      memory: memory.map(sample => ({ ...sample })),
      activeInteractions: activeInteractions.size,
    };
  }

  function dispose() {
    disposed = true;
    for (const observer of observers) observer.disconnect?.();
    observers.length = 0;
    for (const interaction of activeInteractions.values()) {
      if (interaction.frameHandle && typeof cancelFrame === 'function') cancelFrame(interaction.frameHandle);
    }
    activeInteractions.clear();
  }

  return Object.freeze({
    version: 1,
    mark,
    record,
    sampleMemory,
    beginInteraction,
    sampleInteractionFrame,
    startInteractionFrames,
    endInteraction,
    observe,
    snapshot,
    dispose,
  });
}

function installMobileSheetProbe(documentRef, globalObject, metrics) {
  if (!documentRef?.addEventListener || typeof globalObject.matchMedia !== 'function') return () => {};
  let active = null;

  const sheetTarget = target => {
    const handle = target?.closest?.('[data-sheet-handle]');
    if (handle) return documentRef.getElementById(handle.dataset.sheetHandle) || handle.closest('.workspace-surface');
    const header = target?.closest?.('.workspace-surface.ui-sheet > .surface-header');
    if (!header || target.closest?.('button, input, textarea, select')) return null;
    return header.closest('.workspace-surface');
  };

  const finish = event => {
    if (!active || event.pointerId !== active.pointerId) return;
    metrics.endInteraction(active.token, {
      panelId: active.panelId,
      cancelled: event.type === 'pointercancel',
    });
    active = null;
  };

  const onPointerDown = event => {
    if (!globalObject.matchMedia('(max-width: 799px)').matches || active || event.button > 0) return;
    const panel = sheetTarget(event.target);
    if (!panel) return;
    const token = metrics.beginInteraction(PERFORMANCE_METRIC_NAMES.MOBILE_SHEET_DRAG, { panelId: panel.id || '' });
    if (!token) return;
    active = { token, pointerId: event.pointerId, panelId: panel.id || '' };
    metrics.startInteractionFrames(token);
  };

  documentRef.addEventListener('pointerdown', onPointerDown, true);
  documentRef.addEventListener('pointerup', finish, true);
  documentRef.addEventListener('pointercancel', finish, true);
  return () => {
    documentRef.removeEventListener('pointerdown', onPointerDown, true);
    documentRef.removeEventListener('pointerup', finish, true);
    documentRef.removeEventListener('pointercancel', finish, true);
  };
}

function installCommitToPaintProbe(documentRef, globalObject, metrics) {
  if (!documentRef?.addEventListener || typeof globalObject.requestAnimationFrame !== 'function') return () => {};
  const clickSelectors = [
    '#modePrimaryBtn',
    '#territorialTypeConfirmBtn',
    '#distributionTypeConfirmBtn',
    '#confirmModalConfirmBtn',
    '#coastReconciliationCountryBtn',
    '#coastReconciliationAdminBtn',
    '#coastReconciliationIndependentBtn',
    '#gisImportConfirmBtn',
    '#gisExportConfirmBtn',
    '#saveProjectBtn',
    '.editor-action-row',
  ].join(',');
  const changeSelectors = [
    '#rightPanel input',
    '#rightPanel select',
    '#rightPanel textarea',
  ].join(',');

  const schedule = event => {
    const selector = event.type === 'change' ? changeSelectors : clickSelectors;
    const target = event.target?.closest?.(selector);
    if (!target) return;
    const startedAt = nowFrom(globalObject.performance);
    globalObject.requestAnimationFrame(() => {
      globalObject.requestAnimationFrame(() => {
        metrics.record(PERFORMANCE_METRIC_NAMES.COMMIT_TO_PAINT, nowFrom(globalObject.performance) - startedAt, {
          event: event.type,
          target: targetName(target),
        });
      });
    });
  };

  documentRef.addEventListener('click', schedule, true);
  documentRef.addEventListener('change', schedule, true);
  return () => {
    documentRef.removeEventListener('click', schedule, true);
    documentRef.removeEventListener('change', schedule, true);
  };
}

export function installRuntimePerformanceMetrics({
  globalObject = globalThis,
  documentRef = globalObject.document,
} = {}) {
  const existing = globalObject.__PANDOLAB_PERFORMANCE__;
  if (existing?.version === 1) return existing;

  const core = createRuntimePerformanceMetrics({ globalObject });
  core.observe();
  core.mark('runtime-monitor-installed');
  core.sampleMemory('runtime-monitor-installed');

  const removeSheetProbe = installMobileSheetProbe(documentRef, globalObject, core);
  const removeCommitProbe = installCommitToPaintProbe(documentRef, globalObject, core);
  const milestoneEvents = ['pandolab:editable', 'pandolab:ready'];
  const listeners = milestoneEvents.map(type => {
    const listener = () => {
      core.mark(type.replace('pandolab:', ''));
      core.sampleMemory(type.replace('pandolab:', ''));
    };
    globalObject.addEventListener?.(type, listener, { once: true });
    return [type, listener];
  });

  const metrics = Object.freeze({
    ...core,
    dispose: () => {
      removeSheetProbe();
      removeCommitProbe();
      for (const [type, listener] of listeners) globalObject.removeEventListener?.(type, listener);
      core.dispose();
    },
  });
  globalObject.__PANDOLAB_PERFORMANCE__ = metrics;
  globalObject.__PANDOLAB_PERFORMANCE_REPORT__ = () => metrics.snapshot();
  globalObject.__PANDOLAB_PERFORMANCE_THRESHOLDS__ = PERFORMANCE_DIAGNOSTIC_THRESHOLDS;
  return metrics;
}

export function getRuntimePerformanceMetrics(globalObject = globalThis) {
  const existing = globalObject.__PANDOLAB_PERFORMANCE__;
  if (existing?.version === 1) return existing;
  if (!globalObject.document) return null;
  return installRuntimePerformanceMetrics({ globalObject, documentRef: globalObject.document });
}
