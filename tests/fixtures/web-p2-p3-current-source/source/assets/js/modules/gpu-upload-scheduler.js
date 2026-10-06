// One upload frame budget shared by every GPU consumer. Steps must be synchronous.
export function createGpuUploadScheduler({ requestFrame = callback => globalThis.requestAnimationFrame?.(callback) ?? globalThis.setTimeout(callback, 16), cancelFrame = id => globalThis.cancelAnimationFrame?.(id), now = () => performance.now(), isHidden = () => globalThis.document?.hidden, isInputPending = () => globalThis.navigator?.scheduling?.isInputPending?.(), getByteBudget = () => 2 * 1024 * 1024 } = {}) {
  const jobs = new Map();
  let frame = 0, active = false, lastInput = -Infinity, disposed = false;
  const stats = { frames: 0, uploadedBytes: 0, maxFrameMs: 0, overrunCount: 0, completed: 0, cancelled: 0 };
  const schedule = () => { if (!disposed && jobs.size && !frame) frame = requestFrame(drain); };
  function drain() {
    frame = 0;
    if (disposed) return;
    if (active || isHidden() || isInputPending()) { lastInput = now(); schedule(); return; }
    if (now() - lastInput < 500) { schedule(); return; }
    const start = now();
    let bytes = Math.min(2 * 1024 * 1024, Math.max(4, getByteBudget()));
    const order = [...jobs.values()].sort((a, b) => b.priority - a.priority);
    for (const job of order) {
      if (now() - start >= 2 || bytes <= 0) break;
      if (!jobs.has(job.key)) continue;
      try {
        const result = job.step({ byteBudget: Math.min(bytes, 256 * 1024), deadline: start + 2 }) || {};
        const used = Math.max(0, Number(result.bytes) || 0);
        bytes -= used; stats.uploadedBytes += used;
        jobs.delete(job.key);
        if (result.done) { stats.completed++; job.resolve(result.value); }
        else jobs.set(job.key, job); // Round-robin within each priority.
      } catch (error) { jobs.delete(job.key); job.dispose?.(); job.reject(error); }
    }
    const elapsed = now() - start;
    stats.frames++; stats.maxFrameMs = Math.max(stats.maxFrameMs, elapsed);
    if (elapsed > 2) stats.overrunCount++;
    schedule();
  }
  function cancelWhere(predicate) {
    for (const [key, job] of jobs) if (predicate(job)) {
      jobs.delete(key); job.dispose?.(); stats.cancelled++;
      job.reject(Object.assign(new Error('GPU upload cancelled'), { name: 'AbortError' }));
    }
  }
  return Object.freeze({
    enqueueUpload(input) {
      if (disposed) return Promise.reject(new Error('GPU upload scheduler disposed'));
      if (jobs.has(input.key)) return jobs.get(input.key).promise;
      const job = { priority: 0, ...input };
      job.promise = new Promise((resolve, reject) => { job.resolve = resolve; job.reject = reject; });
      jobs.set(job.key, job); schedule(); return job.promise;
    },
    noteInput(value = false) { active = value === true; lastInput = now(); schedule(); },
    defer() { lastInput = now(); schedule(); },
    cancelGeneration(generation) { cancelWhere(job => job.projectGeneration === generation); },
    cancelKey(key) { cancelWhere(job => job.key === key); },
    cancelAll() { cancelWhere(() => true); },
    getStats: () => ({ ...stats, pending: jobs.size, active }),
    dispose() { disposed = true; if (frame) cancelFrame(frame); frame = 0; cancelWhere(() => true); },
  });
}
