const VALID_DOMAINS = new Set(['territorial', 'distribution', 'generic', 'hydro', 'label']);

export function normalizeObjectRef(value) {
  if (!value || typeof value !== 'object') return null;
  const domain = String(value.domain || '').trim();
  const type = String(value.type || '').trim();
  const id = String(value.id ?? '').trim();
  if (!VALID_DOMAINS.has(domain) || !type || !id) return null;
  return Object.freeze({ domain, type, id, key: `${domain}:${type}:${encodeURIComponent(id)}` });
}

export const objectRefKey = value => normalizeObjectRef(value)?.key || '';

export function createObjectSelectionController({ onChange = () => {} } = {}) {
  const selected = new Map();
  const rangeAnchors = new Map();
  let primaryKey = null;

  const snapshot = () => Object.freeze({
    primaryKey,
    keys: Object.freeze([...selected.keys()]),
    items: Object.freeze([...selected.values()]),
  });

  const emit = reason => {
    const value = snapshot();
    onChange(value, reason);
    return value;
  };

  const sameSelection = (nextSelected, nextPrimaryKey) => {
    if (primaryKey !== nextPrimaryKey || selected.size !== nextSelected.size) return false;
    const currentKeys = selected.keys();
    const nextKeys = nextSelected.keys();
    while (true) {
      const current = currentKeys.next();
      const next = nextKeys.next();
      if (current.done || next.done) return current.done === next.done;
      if (current.value !== next.value) return false;
    }
  };

  const replaceSelection = (nextSelected, nextPrimaryKey, reason) => {
    if (sameSelection(nextSelected, nextPrimaryKey)) return snapshot();
    selected.clear();
    for (const [key, ref] of nextSelected) selected.set(key, ref);
    primaryKey = nextPrimaryKey;
    return emit(reason);
  };

  function replace(ref, { scope = '' } = {}) {
    const normalized = normalizeObjectRef(ref);
    if (!normalized) return snapshot();
    const unchanged = selected.size === 1 && selected.has(normalized.key) && primaryKey === normalized.key;
    selected.clear();
    selected.set(normalized.key, normalized);
    primaryKey = normalized.key;
    if (scope) rangeAnchors.set(scope, normalized.key);
    return unchanged ? snapshot() : emit('replace');
  }

  function toggle(ref, { scope = '' } = {}) {
    const normalized = normalizeObjectRef(ref);
    if (!normalized) return snapshot();
    if (selected.has(normalized.key)) {
      selected.delete(normalized.key);
      if (primaryKey === normalized.key) primaryKey = [...selected.keys()].at(-1) || null;
    } else {
      selected.set(normalized.key, normalized);
      primaryKey = normalized.key;
    }
    if (scope) rangeAnchors.set(scope, normalized.key);
    return emit('toggle');
  }

  function selectRange(ref, orderedRefs, { scope = 'default', additive = false } = {}) {
    const normalized = normalizeObjectRef(ref);
    const ordered = (orderedRefs || []).map(normalizeObjectRef).filter(Boolean);
    if (!normalized || !ordered.length) return snapshot();
    const targetIndex = ordered.findIndex(candidate => candidate.key === normalized.key);
    if (targetIndex < 0) return snapshot();
    const anchorKey = rangeAnchors.get(scope);
    const anchorIndex = ordered.findIndex(candidate => candidate.key === anchorKey);
    const nextSelected = additive ? new Map(selected) : new Map();
    if (anchorIndex < 0) {
      nextSelected.set(normalized.key, normalized);
    } else {
      const start = Math.min(anchorIndex, targetIndex);
      const end = Math.max(anchorIndex, targetIndex);
      for (let index = start; index <= end; index += 1) {
        const candidate = ordered[index];
        nextSelected.set(candidate.key, candidate);
      }
    }
    rangeAnchors.set(scope, normalized.key);
    return replaceSelection(nextSelected, normalized.key, 'range');
  }

  function setMany(refs, { primary = null, scope = '' } = {}) {
    const normalized = (refs || []).map(normalizeObjectRef).filter(Boolean);
    const nextSelected = new Map();
    for (const ref of normalized) nextSelected.set(ref.key, ref);
    const requestedPrimary = normalizeObjectRef(primary);
    const nextPrimaryKey = requestedPrimary && nextSelected.has(requestedPrimary.key)
      ? requestedPrimary.key
      : normalized.at(-1)?.key || null;
    if (scope && nextPrimaryKey) rangeAnchors.set(scope, nextPrimaryKey);
    return replaceSelection(nextSelected, nextPrimaryKey, 'set-many');
  }

  function remove(refOrKey) {
    const key = typeof refOrKey === 'string' ? refOrKey : objectRefKey(refOrKey);
    if (!key || !selected.delete(key)) return snapshot();
    if (primaryKey === key) primaryKey = [...selected.keys()].at(-1) || null;
    return emit('remove');
  }

  function prune(predicate) {
    let changed = false;
    for (const [key, ref] of selected) {
      if (predicate(ref)) continue;
      selected.delete(key);
      changed = true;
    }
    if (primaryKey && !selected.has(primaryKey)) {
      primaryKey = [...selected.keys()].at(-1) || null;
      changed = true;
    }
    return changed ? emit('prune') : snapshot();
  }

  function clear() {
    if (!selected.size && !primaryKey) return snapshot();
    selected.clear();
    primaryKey = null;
    rangeAnchors.clear();
    return emit('clear');
  }

  return Object.freeze({
    replace,
    toggle,
    selectRange,
    setMany,
    remove,
    prune,
    clear,
    snapshot,
    has: refOrKey => selected.has(typeof refOrKey === 'string' ? refOrKey : objectRefKey(refOrKey)),
    primary: () => selected.get(primaryKey) || null,
    items: () => [...selected.values()],
    keys: () => [...selected.keys()],
    size: () => selected.size,
    rangeAnchor: scope => rangeAnchors.get(scope) || null,
  });
}
