const revisions = new WeakMap();
export const geometryRevision = geometry => geometry && revisions.get(geometry) || 0;
export function touchGeometry(geometry) {
  if (geometry) revisions.set(geometry, geometryRevision(geometry) + 1);
}

// Only history owns these frozen copies. Live editing coordinates never alias them.
export function createGeometrySnapshotPool() {
  let copies = new WeakMap();
  function freeze(value) {
    if (!value || typeof value !== 'object' || Object.isFrozen(value)) return value;
    Object.values(value).forEach(freeze);
    return Object.freeze(value);
  }
  function clone(value) {
    if (!value || typeof value !== 'object') return value;
    if (value.type && (Array.isArray(value.coordinates) || value.type === 'GeometryCollection')) {
      const revision = geometryRevision(value);
      let entry = copies.get(value);
      if (!entry || entry.revision !== revision) {
        entry = { revision, geometry: freeze(structuredClone(value)) };
        copies.set(value, entry);
      }
      return entry.geometry;
    }
    if (Array.isArray(value)) return value.map(clone);
    if (Object.getPrototypeOf(value) !== Object.prototype) return structuredClone(value);
    return Object.fromEntries(Object.entries(value).map(([key, item]) => [key, clone(item)]));
  }
  function restore(value, current) {
    if (!value || typeof value !== 'object') return value;
    if (value.type && (Array.isArray(value.coordinates) || value.type === 'GeometryCollection')) {
      const entry = current && copies.get(current);
      return entry?.geometry === value && entry.revision === geometryRevision(current) ? current : structuredClone(value);
    }
    if (Array.isArray(value)) {
      const byId = new Map((Array.isArray(current) ? current : []).filter(item => item?.id != null).map(item => [String(item.id), item]));
      return value.map((item, index) => restore(item, item?.id != null ? byId.get(String(item.id)) : current?.[index]));
    }
    if (Object.getPrototypeOf(value) !== Object.prototype) return structuredClone(value);
    return Object.fromEntries(Object.entries(value).map(([key, item]) => [key, restore(item, current?.[key])]));
  }
  return { clone, restore, clear: () => { copies = new WeakMap(); } };
}
