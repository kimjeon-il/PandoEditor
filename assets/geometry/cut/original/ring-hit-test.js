export function createRingHitTester(normalizeRing) {
  let cache = new WeakMap();
  let preparationCount = 0;
  function prepared(raw) {
    if (!Array.isArray(raw)) return [];
    if (cache.has(raw)) return cache.get(raw);
    const finite = raw.every(p => Array.isArray(p) && Number.isFinite(p[0]) && Number.isFinite(p[1]));
    const near = (a, b) => Math.abs(a[0] - b[0]) <= 1e-10 && Math.abs(a[1] - b[1]) <= 1e-10;
    const canonical = finite && raw.length > 1 && near(raw[0], raw.at(-1))
      && raw.every((p, i) => i === 0 || !near(raw[i - 1], p));
    const ring = canonical ? raw : normalizeRing(raw);
    cache.set(raw, ring);
    preparationCount += 1;
    return ring;
  }
  return {
    contains(point, raw) {
      const ring = prepared(raw);
      let inside = false;
      for (let i = 0, j = ring.length - 2; i < ring.length - 1; j = i++) {
        const a = ring[j], b = ring[i];
        const cross = (point[1] - a[1]) * (b[0] - a[0]) - (point[0] - a[0]) * (b[1] - a[1]);
        const dot = (point[0] - a[0]) * (point[0] - b[0]) + (point[1] - a[1]) * (point[1] - b[1]);
        if (Math.abs(cross) <= 1e-6 && dot <= 1e-6) return true;
        if (((b[1] > point[1]) !== (a[1] > point[1]))
          && point[0] < (a[0] - b[0]) * (point[1] - b[1]) / ((a[1] - b[1]) || 1e-12) + b[0]) inside = !inside;
      }
      return inside;
    },
    invalidate(geometry) {
      const visit = value => { if (!Array.isArray(value)) return; cache.delete(value); if (!Number.isFinite(value[0]?.[0])) value.forEach(visit); };
      visit(geometry?.coordinates);
    },
    clear() { cache = new WeakMap(); },
    stats: () => ({ preparationCount }),
  };
}
