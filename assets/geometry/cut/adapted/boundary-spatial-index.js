const overlaps = (a, b) => a[0] <= b[2] && a[2] >= b[0] && a[1] <= b[3] && a[3] >= b[1];
export const segmentBounds = row => [Math.min(row.a[0], row.b[0]), Math.min(row.a[1], row.b[1]), Math.max(row.a[0], row.b[0]), Math.max(row.a[1], row.b[1])];

/** Geographic broad phase only. Exact topology still uses its original tolerance. */
export function createBoundarySpatialIndex() {
  const cells = new Map();
  const entries = new Map();
  const large = new Set();
  function keys(bounds) {
    const x0 = Math.floor(bounds[0]), x1 = Math.floor(bounds[2]);
    const y0 = Math.floor(bounds[1]), y1 = Math.floor(bounds[3]);
    if ((x1 - x0 + 1) * (y1 - y0 + 1) > 4096) return null;
    const result = [];
    for (let x = x0; x <= x1; x++) for (let y = y0; y <= y1; y++) result.push(`${x}:${y}`);
    return result;
  }
  function remove(id) {
    const entry = entries.get(id);
    if (!entry) return;
    for (const key of entry.keys || []) {
      const bucket = cells.get(key);
      bucket.delete(id);
      if (!bucket.size) cells.delete(key);
    }
    large.delete(id); entries.delete(id);
  }
  function insert(id, value, bounds) {
    remove(id);
    const locations = keys(bounds);
    entries.set(id, { value, bounds, keys: locations });
    if (!locations) large.add(id);
    else for (const key of locations) {
      if (!cells.has(key)) cells.set(key, new Set());
      cells.get(key).add(id);
    }
  }
  function query(bounds) {
    if (bounds[0] > bounds[2]) return [...new Set([
      ...query([bounds[0], bounds[1], 180, bounds[3]]), ...query([-180, bounds[1], bounds[2], bounds[3]]),
    ])];
    const locations = keys(bounds);
    const ids = locations ? new Set(large) : new Set(entries.keys());
    if (locations) for (const key of locations) for (const id of cells.get(key) || []) ids.add(id);
    return [...ids].map(id => entries.get(id)).filter(entry => overlaps(entry.bounds, bounds)).map(entry => entry.value);
  }
  return { insert, remove, query, values: () => [...entries.values()].map(entry => entry.value) };
}
