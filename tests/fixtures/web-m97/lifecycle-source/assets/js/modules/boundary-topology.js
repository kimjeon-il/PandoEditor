import { buildRenderableBoundarySegments } from './geographic-boundary.js';
import { createBoundarySpatialIndex, segmentBounds } from './boundary-spatial-index.js';
import { territorialRootId } from './territorial-units.js';

const cloneCoordinate = coordinate => [Number(coordinate[0]), Number(coordinate[1])];

function quantized(value, precision) {
  const factor = 10 ** precision;
  return Math.round(Number(value) * factor) / factor;
}

export function topologyNodeKey(coordinate, precision = 7) {
  return `${quantized(coordinate[0], precision)},${quantized(coordinate[1], precision)}`;
}

function segmentKey(a, b, precision) {
  const left = topologyNodeKey(a, precision);
  const right = topologyNodeKey(b, precision);
  return left < right ? `${left}|${right}` : `${right}|${left}`;
}

function polygonRings(feature) {
  if (feature?.geometry?.type === 'Polygon') return [{ polygonIndex: 0, rings: feature.geometry.coordinates || [] }];
  if (feature?.geometry?.type === 'MultiPolygon') return (feature.geometry.coordinates || []).map((rings, polygonIndex) => ({ polygonIndex, rings }));
  return [];
}

function featureId(feature, index = 0) {
  return String(feature?.id || index);
}

function pointOnSegment(point, a, b, epsilon) {
  const dx = Number(b[0]) - Number(a[0]);
  const dy = Number(b[1]) - Number(a[1]);
  const length2 = dx * dx + dy * dy;
  if (!length2) return null;
  const t = ((Number(point[0]) - Number(a[0])) * dx + (Number(point[1]) - Number(a[1])) * dy) / length2;
  if (t < -epsilon || t > 1 + epsilon) return null;
  const x = Number(a[0]) + dx * t;
  const y = Number(a[1]) + dy * t;
  const distance = Math.hypot(Number(point[0]) - x, Number(point[1]) - y);
  return distance <= epsilon ? Math.max(0, Math.min(1, t)) : null;
}

export function boundarySourceSegments(feature, fallbackId = 0) {
  const rows = [];
  const id = featureId(feature, fallbackId);
  for (const polygon of polygonRings(feature)) {
    (polygon.rings || []).forEach((ring, ringIndex) => {
      const limit = Math.max(0, ring.length - 1);
      for (let index = 0; index < limit; index++) rows.push({
        featureId: id, polygonIndex: polygon.polygonIndex, ringIndex,
        segmentIndex: index, endVertexIndex: (index + 1) % limit,
        a: ring[index], b: ring[index + 1],
      });
    });
  }
  return rows;
}

export function buildBoundaryTopology(features = [], options = {}) {
  return buildBoundaryTopologyFromSegments(features.flatMap(boundarySourceSegments), options);
}

// Display-only geometry. The edit topology remains the single authority for
// shared/coast classification; geographic boundary handling drops pole and
// artificial antimeridian closing edges before either renderer sees them.
export function buildCountrySharedBoundarySegments(features = []) {
  const boundsOf = feature => {
    const bounds = [Infinity, Infinity, -Infinity, -Infinity];
    const visit = value => {
      if (!Array.isArray(value)) return;
      if (typeof value[0] === 'number' && typeof value[1] === 'number') {
        bounds[0] = Math.min(bounds[0], value[0]); bounds[1] = Math.min(bounds[1], value[1]);
        bounds[2] = Math.max(bounds[2], value[0]); bounds[3] = Math.max(bounds[3], value[1]);
      } else for (const item of value) visit(item);
    };
    visit(feature?.geometry?.coordinates);
    return bounds;
  };
  const rows = features.map(feature => ({ feature, bounds: boundsOf(feature) }));
  const shared = new Map();
  for (let leftIndex = 0; leftIndex < rows.length; leftIndex++) {
    const left = rows[leftIndex];
    for (let rightIndex = leftIndex + 1; rightIndex < rows.length; rightIndex++) {
      const right = rows[rightIndex];
      const overlap = [Math.max(left.bounds[0], right.bounds[0]), Math.max(left.bounds[1], right.bounds[1]),
        Math.min(left.bounds[2], right.bounds[2]), Math.min(left.bounds[3], right.bounds[3])];
      if (overlap[0] > overlap[2] + 1e-7 || overlap[1] > overlap[3] + 1e-7) continue;
      const touches = row => Math.max(row.a[0], row.b[0]) >= overlap[0] - 1e-7
        && Math.min(row.a[0], row.b[0]) <= overlap[2] + 1e-7
        && Math.max(row.a[1], row.b[1]) >= overlap[1] - 1e-7
        && Math.min(row.a[1], row.b[1]) <= overlap[3] + 1e-7;
      const rightSegments = boundarySourceSegments(right.feature).filter(touches);
      if (!rightSegments.length) continue;
      const contactIndex = createBoundarySpatialIndex();
      rightSegments.forEach((row, index) => contactIndex.insert(index, row, segmentBounds(row)));
      const candidates = new Set();
      for (const row of boundarySourceSegments(left.feature).filter(touches)) {
        const bounds = segmentBounds(row);
        const nearby = contactIndex.query([
          bounds[0] - 1e-7, bounds[1] - 1e-7, bounds[2] + 1e-7, bounds[3] + 1e-7,
        ]);
        if (!nearby.length) continue;
        candidates.add(row);
        for (const other of nearby) candidates.add(other);
      }
      if (!candidates.size) continue;
      const topology = buildBoundaryTopologyFromSegments([...candidates]);
      for (const segment of topology.segments.values()) {
        if (segment.kind !== 'shared') continue;
        const key = segmentKey(segment.a, segment.b, topology.precision);
        const previous = shared.get(key);
        if (previous) for (const id of segment.ownerIds) previous.ownerIds.add(id);
        else shared.set(key, { a: segment.a, b: segment.b, ownerIds: new Set(segment.ownerIds) });
      }
    }
  }
  return [...shared.values()].flatMap(segment => {
    const ownerIds = [...segment.ownerIds].sort();
    return buildRenderableBoundarySegments({ type: 'LineString', coordinates: [segment.a, segment.b] })
      .map(([start, end]) => ({ start, end, ownerIds }));
  });
}

export function buildBoundaryTopologyFromSegments(rawSegments, { precision = 7, epsilon = 1e-7 } = {}) {
  const nodes = new Map();
  for (const raw of rawSegments) {
    for (const [coordinate, vertexIndex] of [[raw.a, raw.segmentIndex], [raw.b, raw.endVertexIndex]]) {
      const key = topologyNodeKey(coordinate, precision);
      if (!nodes.has(key)) nodes.set(key, { key, coordinate: cloneCoordinate(coordinate), ownerIds: new Set(), refs: [], virtualRefs: [] });
      const node = nodes.get(key);
      node.ownerIds.add(raw.featureId);
      if (!node.refs.some(ref => ref.featureId === raw.featureId && ref.polygonIndex === raw.polygonIndex
        && ref.ringIndex === raw.ringIndex && ref.vertexIndex === vertexIndex)) node.refs.push({
        featureId: raw.featureId, polygonIndex: raw.polygonIndex, ringIndex: raw.ringIndex, vertexIndex,
      });
    }
  }

  const nodeValues = [...nodes.values()];
  // A shared-border vertex may lie inside the opposite owner's longer segment.
  // Looking at every node for every segment turns detailed country geometry into
  // O(n²) work, so query only coarse geographic buckets crossed by the segment.
  const bucketSize = 0.25;
  const bucketKey = (x, y) => `${x}:${y}`;
  const nodeBuckets = new Map();
  for (const node of nodeValues) {
    const key = bucketKey(Math.floor(node.coordinate[0] / bucketSize), Math.floor(node.coordinate[1] / bucketSize));
    if (!nodeBuckets.has(key)) nodeBuckets.set(key, []);
    nodeBuckets.get(key).push(node);
  }
  const nodesNearSegment = raw => {
    const minX = Math.floor((Math.min(raw.a[0], raw.b[0]) - epsilon) / bucketSize);
    const maxX = Math.floor((Math.max(raw.a[0], raw.b[0]) + epsilon) / bucketSize);
    const minY = Math.floor((Math.min(raw.a[1], raw.b[1]) - epsilon) / bucketSize);
    const maxY = Math.floor((Math.max(raw.a[1], raw.b[1]) + epsilon) / bucketSize);
    const cellCount = (maxX - minX + 1) * (maxY - minY + 1);
    if (cellCount > 4096) return nodeValues;
    const output = [];
    const seen = new Set();
    for (let x = minX; x <= maxX; x += 1) {
      for (let y = minY; y <= maxY; y += 1) {
        for (const node of nodeBuckets.get(bucketKey(x, y)) || []) {
          if (seen.has(node.key)) continue;
          seen.add(node.key);
          output.push(node);
        }
      }
    }
    return output;
  };
  const segments = new Map();
  for (const raw of rawSegments) {
    const split = nodesNearSegment(raw)
      .map(node => ({ node, t: pointOnSegment(node.coordinate, raw.a, raw.b, epsilon) }))
      .filter(item => item.t !== null)
      .sort((left, right) => left.t - right.t);
    for (const item of split) {
      item.node.ownerIds.add(raw.featureId);
      if (item.t > epsilon && item.t < 1 - epsilon && !item.node.virtualRefs.some(ref => ref.featureId === raw.featureId
        && ref.polygonIndex === raw.polygonIndex && ref.ringIndex === raw.ringIndex && ref.segmentIndex === raw.segmentIndex)) {
        item.node.virtualRefs.push({
          featureId: raw.featureId,
          polygonIndex: raw.polygonIndex,
          ringIndex: raw.ringIndex,
          segmentIndex: raw.segmentIndex,
          t: item.t,
        });
      }
    }
    for (let index = 1; index < split.length; index += 1) {
      const start = split[index - 1];
      const end = split[index];
      if (end.t - start.t <= epsilon) continue;
      const key = segmentKey(start.node.coordinate, end.node.coordinate, precision);
      if (!segments.has(key)) segments.set(key, {
        key,
        a: cloneCoordinate(start.node.coordinate),
        b: cloneCoordinate(end.node.coordinate),
        ownerIds: new Set(),
        refs: [],
      });
      const segment = segments.get(key);
      segment.ownerIds.add(raw.featureId);
      segment.refs.push({ ...raw, startT: start.t, endT: end.t });
    }
  }

  for (const node of nodes.values()) node.kind = node.ownerIds.size >= 3 ? 'multi-owner' : node.ownerIds.size === 2 ? 'shared' : 'coast';
  for (const segment of segments.values()) segment.kind = segment.ownerIds.size >= 2 ? 'shared' : 'coast';
  return { precision, epsilon, nodes, segments };
}

function ringForRef(feature, ref) {
  if (feature?.geometry?.type === 'Polygon') return feature.geometry.coordinates?.[ref.ringIndex] || null;
  return feature?.geometry?.coordinates?.[ref.polygonIndex]?.[ref.ringIndex] || null;
}

export function moveTopologyNode(featureMap, node, nextCoordinate, { precision = 7 } = {}) {
  if (!node || !Array.isArray(nextCoordinate)) return new Set();
  const changed = new Set();
  const oldKey = topologyNodeKey(node.coordinate, precision);
  const virtualByRing = new Map();
  for (const ref of node.virtualRefs || []) {
    const key = `${ref.featureId}:${ref.polygonIndex}:${ref.ringIndex}`;
    if (!virtualByRing.has(key)) virtualByRing.set(key, []);
    virtualByRing.get(key).push(ref);
  }
  for (const refs of virtualByRing.values()) {
    refs.sort((left, right) => right.segmentIndex - left.segmentIndex || right.t - left.t);
    for (const ref of refs) {
      const feature = featureMap.get(String(ref.featureId));
      const ring = ringForRef(feature, ref);
      if (!ring) continue;
      const exists = ring.some(coordinate => topologyNodeKey(coordinate, precision) === oldKey);
      if (!exists) ring.splice(ref.segmentIndex + 1, 0, cloneCoordinate(node.coordinate));
      changed.add(String(ref.featureId));
    }
  }
  for (const ownerId of node.ownerIds || []) {
    const feature = featureMap.get(String(ownerId));
    if (!feature) continue;
    for (const polygon of polygonRings(feature)) {
      for (const ring of polygon.rings || []) {
        let ringChanged = false;
        for (let index = 0; index < ring.length; index += 1) {
          if (topologyNodeKey(ring[index], precision) !== oldKey) continue;
          ring[index] = cloneCoordinate(nextCoordinate);
          ringChanged = true;
        }
        if (ringChanged && ring.length > 1) {
          const firstKey = topologyNodeKey(ring[0], precision);
          const lastKey = topologyNodeKey(ring[ring.length - 1], precision);
          if (firstKey !== lastKey && (firstKey === topologyNodeKey(nextCoordinate, precision) || lastKey === topologyNodeKey(nextCoordinate, precision))) {
            ring[ring.length - 1] = cloneCoordinate(ring[0]);
          }
          changed.add(String(ownerId));
        }
      }
    }
  }
  return changed;
}

function selectedTopologySegmentKeys(topology, predicate) {
  const keys = new Set();
  for (const segment of topology?.segments?.values?.() || []) {
    if (predicate(segment)) keys.add(segment.key);
  }
  return keys;
}

function endpointNodeKeys(topology, segmentKeys) {
  const keys = new Set();
  for (const key of segmentKeys) {
    const segment = topology?.segments?.get?.(key);
    if (!segment) continue;
    keys.add(topologyNodeKey(segment.a, topology.precision));
    keys.add(topologyNodeKey(segment.b, topology.precision));
  }
  return keys;
}

export function planSharedBoundaryEdit(topology, selectedCountryIds = []) {
  const selectedIds = [...new Set(selectedCountryIds.map(String).filter(Boolean))];
  const selected = new Set(selectedIds);
  const segmentKeys = selectedTopologySegmentKeys(topology, segment => segment.kind === 'shared'
    && segment.ownerIds.size >= 2
    && [...segment.ownerIds].every(ownerId => selected.has(String(ownerId))));
  const nodeKeys = endpointNodeKeys(topology, segmentKeys);
  const editableNodeKeys = new Set();
  const fixedNodeKeys = new Set();
  const participantIds = new Set();

  for (const key of segmentKeys) {
    for (const ownerId of topology.segments.get(key)?.ownerIds || []) participantIds.add(String(ownerId));
  }
  for (const key of nodeKeys) {
    const node = topology?.nodes?.get?.(key);
    const editable = node?.ownerIds?.size >= 2 && [...node.ownerIds].every(ownerId => selected.has(String(ownerId)));
    (editable ? editableNodeKeys : fixedNodeKeys).add(key);
  }

  const isolatedIds = selectedIds.filter(id => !participantIds.has(id));
  return {
    selectedIds,
    segmentKeys,
    editableNodeKeys,
    fixedNodeKeys,
    participantIds,
    isolatedIds,
    valid: selectedIds.length >= 2 && segmentKeys.size > 0 && isolatedIds.length === 0,
  };
}

export function planCoastEdit(topology, countryId) {
  const ownerId = String(countryId || '');
  const segmentKeys = selectedTopologySegmentKeys(topology, segment => segment.kind === 'coast'
    && segment.ownerIds.size === 1
    && segment.ownerIds.has(ownerId));
  const nodeKeys = endpointNodeKeys(topology, segmentKeys);
  const editableNodeKeys = new Set();
  const fixedNodeKeys = new Set();
  for (const key of nodeKeys) {
    const node = topology?.nodes?.get?.(key);
    const editable = node?.kind === 'coast' && node.ownerIds.size === 1 && node.ownerIds.has(ownerId);
    (editable ? editableNodeKeys : fixedNodeKeys).add(key);
  }
  return { countryId: ownerId, segmentKeys, editableNodeKeys, fixedNodeKeys };
}

function topologyFeature(feature, prefix, index) {
  const id = featureId(feature, index);
  return {
    ...feature,
    id: `${prefix}:${id}`,
  };
}

function pointSegmentDistance(point, a, b) {
  const dx = Number(b[0]) - Number(a[0]);
  const dy = Number(b[1]) - Number(a[1]);
  const length2 = dx * dx + dy * dy;
  if (!length2) return Math.hypot(Number(point[0]) - Number(a[0]), Number(point[1]) - Number(a[1]));
  const t = Math.max(0, Math.min(1, ((Number(point[0]) - Number(a[0])) * dx + (Number(point[1]) - Number(a[1])) * dy) / length2));
  return Math.hypot(Number(point[0]) - (Number(a[0]) + dx * t), Number(point[1]) - (Number(a[1]) + dy * t));
}

// Coincident edges on the same filled side are an exterior shared by an
// overlapping parent/child, rather than a boundary separating their land.
export function hasOpposingBoundaryInteriors(segment, features) {
  const middle = [(segment.start[0] + segment.end[0]) / 2, (segment.start[1] + segment.end[1]) / 2];
  const dx = segment.end[0] - segment.start[0], dy = segment.end[1] - segment.start[1];
  const sides = features.map(feature => {
    const edge = boundarySourceSegments(feature).find(row => pointSegmentDistance(middle, row.a, row.b) <= 1e-7);
    if (!edge) throw new Error(`공유 경계의 원본 구간이 없습니다: ${feature.id}`);
    const ring = ringForRef(feature, edge);
    let area = 0;
    for (let index = 1; index < ring.length; index++) {
      const a = ring[index - 1], b = ring[index], origin = ring[0];
      area += (a[0] - origin[0]) * (b[1] - origin[1]) - (b[0] - origin[0]) * (a[1] - origin[1]);
    }
    const direction = Math.sign((edge.b[0] - edge.a[0]) * dx + (edge.b[1] - edge.a[1]) * dy);
    return direction * Math.sign(area) * (edge.ringIndex ? -1 : 1);
  });
  return new Set(sides).size > 1;
}

export function buildTerritorialInternalBoundarySegments(countries = [], units = [], { precision = 7, epsilon = 1e-7 } = {}) {
  // No unit can own an internal boundary: do not even inspect country geometry.
  if (!(units || []).some(feature => ['Polygon', 'MultiPolygon'].includes(feature?.geometry?.type))) return [];
  const rootIds = new Set((countries || [])
    .filter(feature => feature?.geometry?.type === 'Polygon' || feature?.geometry?.type === 'MultiPolygon')
    .map((feature, index) => featureId(feature, index)));
  const countryFeatures = (countries || [])
    .filter(feature => feature?.geometry?.type === 'Polygon' || feature?.geometry?.type === 'MultiPolygon')
    .map((feature, index) => topologyFeature(feature, 'country', index));
  const unitFeatures = (units || [])
    .filter(feature => feature?.geometry?.type === 'Polygon' || feature?.geometry?.type === 'MultiPolygon')
    .map((feature, index) => topologyFeature(feature, 'unit', index));
  const entities = new Map([...countries, ...units].map(feature => [String(feature.id), feature]));
  const unitMeta = new Map(unitFeatures.map(feature => [feature.id, {
    id: featureId(feature).replace(/^unit:/, ''),
    entityKind: feature.properties?.entityKind || '',
    rootId: territorialRootId(entities.get(featureId(feature).replace(/^unit:/, '')), id => entities.get(id)),
  }]));
  const topology = buildBoundaryTopology([...countryFeatures, ...unitFeatures], { precision, epsilon });
  const countryEdges = [...topology.segments.values()].filter(segment => [...segment.ownerIds].some(ownerId => ownerId.startsWith('country:')));
  const boundaryTolerance = Math.max(1e-4, epsilon * 1000);
  const bucketSize = 1;
  const countryBuckets = new Map();
  const bucketKey = (x, y) => `${x}:${y}`;
  for (const edge of countryEdges) {
    const minX = Math.floor(Math.min(edge.a[0], edge.b[0]) / bucketSize);
    const maxX = Math.floor(Math.max(edge.a[0], edge.b[0]) / bucketSize);
    const minY = Math.floor(Math.min(edge.a[1], edge.b[1]) / bucketSize);
    const maxY = Math.floor(Math.max(edge.a[1], edge.b[1]) / bucketSize);
    if ((maxX - minX + 1) * (maxY - minY + 1) > 4096) continue;
    for (let x = minX; x <= maxX; x += 1) for (let y = minY; y <= maxY; y += 1) {
      const key = bucketKey(x, y);
      if (!countryBuckets.has(key)) countryBuckets.set(key, []);
      countryBuckets.get(key).push(edge);
    }
  }
  const nearCountryExterior = segment => {
    const minX = Math.floor((Math.min(segment.a[0], segment.b[0]) - boundaryTolerance) / bucketSize);
    const maxX = Math.floor((Math.max(segment.a[0], segment.b[0]) + boundaryTolerance) / bucketSize);
    const minY = Math.floor((Math.min(segment.a[1], segment.b[1]) - boundaryTolerance) / bucketSize);
    const maxY = Math.floor((Math.max(segment.a[1], segment.b[1]) + boundaryTolerance) / bucketSize);
    const candidates = [];
    const seen = new Set();
    for (let x = minX; x <= maxX; x += 1) for (let y = minY; y <= maxY; y += 1) {
      for (const edge of countryBuckets.get(bucketKey(x, y)) || []) {
        if (seen.has(edge.key)) continue;
        seen.add(edge.key);
        candidates.push(edge);
      }
    }
    return candidates.some(edge => pointSegmentDistance(segment.a, edge.a, edge.b) <= boundaryTolerance
      && pointSegmentDistance(segment.b, edge.a, edge.b) <= boundaryTolerance);
  };
  const output = [];
  for (const segment of topology.segments.values()) {
    const unitOwners = [...segment.ownerIds].filter(ownerId => ownerId.startsWith('unit:'));
    if (!unitOwners.length || [...segment.ownerIds].some(ownerId => ownerId.startsWith('country:')) || nearCountryExterior(segment)) continue;
    const metadata = unitOwners.map(ownerId => unitMeta.get(ownerId)).filter(Boolean);
    if (!metadata.length) continue;
    const validRootIds = new Set(metadata
      .map(item => item.rootId)
      .filter(rootId => rootId && rootIds.has(rootId)));
    if (validRootIds.size > 1) continue;
    const allRootsValid = metadata.every(item => item.rootId && rootIds.has(item.rootId));
    const sameRootChildren = metadata.every(item => item.entityKind === 'general')
      && allRootsValid
      && validRootIds.size === 1;
    const styleType = metadata.some(item => item.entityKind === 'regional')
      ? 'region'
      : sameRootChildren ? 'subunit-internal' : 'subunit';
    output.push({
      key: segment.key,
      a: segment.a,
      b: segment.b,
      unitOwners: metadata.map(item => ({ id: item.id, entityKind: item.entityKind, rootId: item.rootId })),
      styleType,
    });
  }
  return output;
}
