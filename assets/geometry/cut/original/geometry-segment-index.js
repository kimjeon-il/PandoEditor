import { createBoundarySpatialIndex, segmentBounds } from './boundary-spatial-index.js';
import { geometryRevision } from './geometry-versions.js';

const indexes = new WeakMap();
const rawIndexes = new WeakMap();
export function geometrySegmentIndex(geometry, { wrap = true } = {}) {
  const cache = wrap ? indexes : rawIndexes;
  const revision = geometryRevision(geometry);
  const cached = cache.get(geometry);
  if (cached?.revision === revision) return cached.index;
  const index = createBoundarySpatialIndex();
  const polygons = geometry.type === 'Polygon' ? [geometry.coordinates] : geometry.type === 'MultiPolygon' ? geometry.coordinates : [];
  for (const [polygonIndex, polygon] of polygons.entries()) for (const [ringIndex, ring] of polygon.entries()) {
    for (let segmentIndex = 0; segmentIndex + 1 < ring.length; segmentIndex++) {
      const row = { a: ring[segmentIndex], b: ring[segmentIndex + 1], polygonIndex, ringIndex, segmentIndex };
      // Query all three longitude copies; retain original coordinates and references.
      let end = row.b[0];
      while (wrap && end - row.a[0] > 180) end -= 360;
      while (wrap && end - row.a[0] < -180) end += 360;
      const bounds = segmentBounds({ a: row.a, b: [end, row.b[1]] });
      index.insert(`${polygonIndex}:${ringIndex}:${segmentIndex}`, row, bounds);
    }
  }
  const query = bounds => wrap ? [...new Set([-360, 0, 360].flatMap(shift => index.query([bounds[0] + shift, bounds[1], bounds[2] + shift, bounds[3]])))] : index.query(bounds);
  const prepared = { query, values: index.values };
  cache.set(geometry, { revision, index: prepared });
  return prepared;
}

export function territorialSegmentCandidates(geometry, a, b) {
  const epsilon = 1e-7;
  return geometrySegmentIndex(geometry, { wrap: false }).query([
    Math.min(a[0], b[0]) - epsilon, Math.min(a[1], b[1]) - epsilon,
    Math.max(a[0], b[0]) + epsilon, Math.max(a[1], b[1]) + epsilon,
  ]);
}

export function segmentQueryBounds(a, b, epsilon = 1e-7) {
  let end = b[0];
  while (end - a[0] > 180) end -= 360;
  while (end - a[0] < -180) end += 360;
  return [Math.min(a[0], end) - epsilon, Math.min(a[1], b[1]) - epsilon,
    Math.max(a[0], end) + epsilon, Math.max(a[1], b[1]) + epsilon];
}
