import { geometryBounds, boundsOverlap, featureId } from './map-edit-geometry.js';
import { geometrySegmentIndex } from './geometry-segment-index.js';
import { createCutGeometry } from './app-cut-geometry.js';
import { createCountryValidation } from './app-country-validation.js';
import { createBoundarySpatialIndex, segmentBounds } from './boundary-spatial-index.js';

export async function calculateSnapCandidates({ coordinate, margin, activeOwnerIds = [], sourceKey }, sourceFeatures, sourceGeometry, checkpoint) {
  const bounds = [coordinate[0] - margin, coordinate[1] - margin, coordinate[0] + margin, coordinate[1] + margin];
  const candidates = [], seenVertices = new Set();
  const active = new Set(activeOwnerIds);
  const features = [...sourceFeatures];
  if (sourceGeometry) features.push({ id: sourceKey, geometry: sourceGeometry });
  for (const feature of features) {
    if (!['Polygon', 'MultiPolygon'].includes(feature.geometry?.type)) continue;
    const featureBounds = geometryBounds(feature.geometry);
    const wrapsLongitude = featureBounds && featureBounds[2] - featureBounds[0] > 180;
    if (wrapsLongitude) {
      if (featureBounds[3] < bounds[1] || featureBounds[1] > bounds[3]) continue;
    } else if (![-360, 0, 360].some(shift => boundsOverlap([bounds[0] + shift, bounds[1], bounds[2] + shift, bounds[3]], featureBounds))) continue;
    await checkpoint();
    const ownerIds = feature.id === sourceKey ? activeOwnerIds : [featureId(feature)];
    for (const edge of geometrySegmentIndex(feature.geometry).query(bounds)) {
      for (const vertex of [edge.a, edge.b]) {
        const nodeKey = vertex.map(value => Number(value).toFixed(7)).join(',');
        if (seenVertices.has(nodeKey)) continue;
        seenVertices.add(nodeKey);
        candidates.push({ kind: 'vertex', coordinate: vertex, ownerIds, nodeKey });
      }
      candidates.push({ kind: feature.id === sourceKey ? 'boundary' : active.size && !active.has(featureId(feature)) ? 'neighbor' : 'edge',
        a: edge.a, b: edge.b, ownerIds, segmentKey: `${feature.id}:${edge.polygonIndex}:${edge.ringIndex}:${edge.segmentIndex}` });
    }
  }
  const cut = createCutGeometry(), validation = createCountryValidation();
  cut.connect({ platform: { clamp: (v, min, max) => Math.max(min, Math.min(max, v)) }, geometryValidation: { interpolateCoordinate: validation.interpolateCoordinate } });
  const edges = candidates.filter(candidate => candidate.a && candidate.b);
  const index = createBoundarySpatialIndex();
  edges.forEach((edge, id) => index.insert(id, { edge, id }, segmentBounds(edge)));
  for (const [id, edge] of edges.entries()) for (const other of index.query(segmentBounds(edge))) {
    if (other.id <= id || edge.segmentKey === other.edge.segmentKey) continue;
    const hit = cut.segmentIntersectionDetail(edge.a, edge.b, other.edge.a, other.edge.b);
    if (!hit || hit.overlap || hit.lineT <= 1e-7 || hit.lineT >= 1 - 1e-7 || hit.boundaryT <= 1e-7 || hit.boundaryT >= 1 - 1e-7) continue;
    candidates.push({ kind: 'intersection', coordinate: hit.coord, ownerIds: [...new Set([...edge.ownerIds, ...other.edge.ownerIds])] });
  }
  return { candidates };
}
