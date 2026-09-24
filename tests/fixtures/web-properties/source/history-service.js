export function createHistoryService({
  store,
  maxEntries,
  snapshot,
  restore,
  normalizeMetadata,
  onRecord = () => {},
  onChange = () => {},
}) {
  function trim() {
    while (store.history.length > maxEntries) {
      store.history.shift();
      store.historyMeta.shift();
    }
  }

  function commitSnapshot(value, metadata = {}) {
    store.history.push(value);
    store.historyMeta.push(normalizeMetadata(metadata));
    trim();
    store.future = [];
    store.futureMeta = [];
    onRecord();
    onChange();
  }

  function record(metadata = {}) {
    commitSnapshot(snapshot(), metadata);
  }

  function discardLast() {
    if (!store.history.length) return false;
    store.history.pop();
    store.historyMeta.pop();
    onChange();
    return true;
  }

  function undo(fallbackMetadata = {}) {
    if (!store.history.length) return false;
    const current = snapshot();
    restoreHistory(store.history.at(-1), current);
    store.future.push(current);
    store.futureMeta.push(store.historyMeta.at(-1) || normalizeMetadata(fallbackMetadata));
    store.history.pop();
    store.historyMeta.pop();
    onChange();
    return true;
  }

  function redo(fallbackMetadata = {}) {
    if (!store.future.length) return false;
    const current = snapshot();
    restoreHistory(store.future.at(-1), current);
    store.history.push(current);
    store.historyMeta.push(store.futureMeta.at(-1) || normalizeMetadata(fallbackMetadata));
    trim();
    store.future.pop();
    store.futureMeta.pop();
    onChange();
    return true;
  }

  function restoreHistory(target, current) {
    try { restore(target, { mode: 'history' }); }
    catch (error) {
      try { restore(current, { mode: 'rollback' }); }
      catch (restoreError) { error.restoreError = restoreError; }
      throw error;
    }
  }

  function reset() {
    store.history = [];
    store.historyMeta = [];
    store.future = [];
    store.futureMeta = [];
    onChange();
  }

  return Object.freeze({
    canRedo: () => store.future.length > 0,
    canUndo: () => store.history.length > 0,
    commitSnapshot,
    discardLast,
    record,
    redo,
    reset,
    undo,
  });
}
