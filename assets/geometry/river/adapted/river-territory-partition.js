import { tracePlanarGraphFaces } from './planar-graph-faces.js';
const EARTH_RADIUS_M = 6371008.8;
const DEG_TO_RAD = Math.PI / 180;

export const RIVER_TERRITORY_PARTITION_ALGORITHM_REVISION = 'river-partitions-v2';
export const RIVER_TERRITORY_PARTITION_CONFIG = Object.freeze({
  minRiverEdgeM: 10,
  riverEndpointSnapM: 50,
  nodeMergeToleranceM: 0.5,
  boundaryCoincidenceToleranceM: 5,
  spatialGridCellM: 25000,
  minCandidateAreaM2: 1000,
  coverageToleranceM2: 1000,
});

const clone = value => value == null ? value : structuredClone(value);

export function composeRiverBoundaryTerritoryComponents({
  components = [],
  candidates = [],
  donorResults = [],
} = {}) {
  const invalidDonorIds = [...new Set((donorResults || [])
    .filter(result => result?.status === 'invalid')
    .map(result => String(result.donorCountryId || ''))
    .filter(Boolean))].sort();
  const invalid = new Set(invalidDonorIds);
  const candidatesByComponent = new Map();
  for (const candidate of candidates || []) {
    const donorCountryId = String(candidate?.donorCountryId || '');
    const componentKey = String(candidate?.componentKey || '');
    if (!donorCountryId || !componentKey || invalid.has(donorCountryId)) continue;
    if (!candidatesByComponent.has(componentKey)) candidatesByComponent.set(componentKey, []);
    candidatesByComponent.get(componentKey).push(candidate);
  }
  for (const rows of candidatesByComponent.values()) rows.sort((left, right) => String(left.key).localeCompare(String(right.key)));

  const items = [];
  let splitComponentCount = 0;
  for (const component of components || []) {
    const countryId = String(component?.countryId || component?.donorCountryId || '');
    if (!countryId || invalid.has(countryId)) continue;
    const polygonIndex = Number(component?.polygonIndex || 0);
    const sourcePolygonIndex = Number(component?.sourcePolygonIndex ?? polygonIndex);
    const componentKey = String(component?.componentKey || `${countryId}:${polygonIndex}`);
    const partitions = candidatesByComponent.get(componentKey) || [];
    if (partitions.length) {
      splitComponentCount += 1;
      for (const candidate of partitions) items.push(__riverOwnDataMerge({}, candidate, {
        countryId,
        polygonIndex,
        sourcePolygonIndex,
        componentKey,
        partitionKind: 'river',
      }));
    } else {
      items.push(__riverOwnDataMerge({}, component, { countryId, polygonIndex, sourcePolygonIndex, componentKey, partitionKind: 'original' }));
    }
  }
  return {
    items,
    invalidDonorIds,
    splitComponentCount,
    riverCandidateCount: items.filter(item => item.partitionKind === 'river').length,
  };
}

function finiteCoordinate(point) {
  return Array.isArray(point)
    && point.length >= 2
    && Number.isFinite(Number(point[0]))
    && Number.isFinite(Number(point[1]));
}

function coordinateDistance(a, b) {
  return Math.hypot(a[0] - b[0], a[1] - b[1]);
}

function polygonCoordinates(geometry) {
  if (geometry?.type === 'Polygon') return [geometry.coordinates || []];
  if (geometry?.type === 'MultiPolygon') return geometry.coordinates || [];
  return [];
}

function lineParts(geometry) {
  if (geometry?.type === 'LineString') return [geometry.coordinates || []];
  if (geometry?.type === 'MultiLineString') return geometry.coordinates || [];
  return [];
}

function ensureClosedRing(ring) {
  const clean = (ring || []).filter(finiteCoordinate).map(point => [Number(point[0]), Number(point[1])]);
  if (clean.length < 3) return [];
  const first = clean[0];
  const last = clean[clean.length - 1];
  if (first[0] !== last[0] || first[1] !== last[1]) clean.push([...first]);
  return clean;
}

function normalizeLongitude(value) {
  let longitude = Number(value);
  while (longitude > 180) longitude -= 360;
  while (longitude < -180) longitude += 360;
  return longitude;
}

function unwrapLongitude(value, reference) {
  let longitude = Number(value);
  while (longitude - reference > 180) longitude -= 360;
  while (longitude - reference < -180) longitude += 360;
  return longitude;
}

export function createRiverPartitionWorkspace(component) {
  const outer = ensureClosedRing(component?.[0]);
  const referenceLongitude = Number(outer[0]?.[0] || 0);
  const unwrapped = outer.slice(0, -1).map(point => [unwrapLongitude(point[0], referenceLongitude), point[1]]);
  const centerLongitude = unwrapped.length
    ? unwrapped.reduce((sum, point) => sum + point[0], 0) / unwrapped.length
    : referenceLongitude;
  const centerLatitude = unwrapped.length
    ? unwrapped.reduce((sum, point) => sum + point[1], 0) / unwrapped.length
    : 0;
  const cosLatitude = Math.max(1e-6, Math.cos(centerLatitude * DEG_TO_RAD));
  return {
    centerLongitude,
    centerLatitude,
    toMeters(point) {
      const longitude = unwrapLongitude(Number(point[0]), centerLongitude);
      return [
        (longitude - centerLongitude) * DEG_TO_RAD * EARTH_RADIUS_M * cosLatitude,
        (Number(point[1]) - centerLatitude) * DEG_TO_RAD * EARTH_RADIUS_M,
      ];
    },
    toLonLat(point) {
      return [
        normalizeLongitude(centerLongitude + point[0] / (DEG_TO_RAD * EARTH_RADIUS_M * cosLatitude)),
        centerLatitude + point[1] / (DEG_TO_RAD * EARTH_RADIUS_M),
      ];
    },
  };
}

function signedRingArea(ring) {
  let area = 0;
  for (let index = 0; index < ring.length - 1; index += 1) {
    area += ring[index][0] * ring[index + 1][1] - ring[index + 1][0] * ring[index][1];
  }
  return area / 2;
}

function metricGeometryArea(geometry, workspace) {
  let total = 0;
  for (const polygon of polygonCoordinates(geometry)) {
    const rings = (polygon || []).map(ring => ensureClosedRing(ring).map(workspace.toMeters));
    if (!rings[0]?.length) continue;
    total += Math.abs(signedRingArea(rings[0]));
    for (let index = 1; index < rings.length; index += 1) total -= Math.abs(signedRingArea(rings[index]));
  }
  return Math.max(0, total);
}

function pointOnSegment(point, start, end, tolerance = 1e-6) {
  const dx = end[0] - start[0];
  const dy = end[1] - start[1];
  const lengthSquared = dx * dx + dy * dy;
  if (lengthSquared <= tolerance * tolerance) return coordinateDistance(point, start) <= tolerance;
  const t = ((point[0] - start[0]) * dx + (point[1] - start[1]) * dy) / lengthSquared;
  if (t < -1e-9 || t > 1 + 1e-9) return false;
  const projected = [start[0] + dx * Math.max(0, Math.min(1, t)), start[1] + dy * Math.max(0, Math.min(1, t))];
  return coordinateDistance(point, projected) <= tolerance;
}

function pointInRing(point, ring) {
  let inside = false;
  for (let left = 0, right = ring.length - 1; left < ring.length; right = left, left += 1) {
    const a = ring[left];
    const b = ring[right];
    if (pointOnSegment(point, a, b, 1e-5)) return true;
    const intersects = ((a[1] > point[1]) !== (b[1] > point[1]))
      && point[0] < ((b[0] - a[0]) * (point[1] - a[1])) / ((b[1] - a[1]) || 1e-20) + a[0];
    if (intersects) inside = !inside;
  }
  return inside;
}

function pointInComponent(point, rings) {
  if (!rings[0]?.length || !pointInRing(point, rings[0])) return false;
  for (let index = 1; index < rings.length; index += 1) if (pointInRing(point, rings[index])) return false;
  return true;
}

function segmentBounds(segment) {
  return [
    Math.min(segment.a[0], segment.b[0]), Math.min(segment.a[1], segment.b[1]),
    Math.max(segment.a[0], segment.b[0]), Math.max(segment.a[1], segment.b[1]),
  ];
}

function boundsOverlap(left, right) {
  return left[0] <= right[2] && left[2] >= right[0] && left[1] <= right[3] && left[3] >= right[1];
}

function componentBounds(rings) {
  const bounds = [Infinity, Infinity, -Infinity, -Infinity];
  for (const ring of rings) for (const point of ring) {
    bounds[0] = Math.min(bounds[0], point[0]);
    bounds[1] = Math.min(bounds[1], point[1]);
    bounds[2] = Math.max(bounds[2], point[0]);
    bounds[3] = Math.max(bounds[3], point[1]);
  }
  return bounds.every(Number.isFinite) ? bounds : null;
}

function createSegmentGrid(segments, cellSize) {
  const grid = new Map();
  segments.forEach((segment, index) => {
    const bounds = segment.bounds || segmentBounds(segment);
    segment.bounds = bounds;
    const minX = Math.floor(bounds[0] / cellSize);
    const maxX = Math.floor(bounds[2] / cellSize);
    const minY = Math.floor(bounds[1] / cellSize);
    const maxY = Math.floor(bounds[3] / cellSize);
    for (let x = minX; x <= maxX; x += 1) for (let y = minY; y <= maxY; y += 1) {
      const key = `${x}:${y}`;
      if (!grid.has(key)) grid.set(key, []);
      grid.get(key).push(index);
    }
  });
  return {
    query(bounds) {
      const output = new Set();
      const minX = Math.floor(bounds[0] / cellSize);
      const maxX = Math.floor(bounds[2] / cellSize);
      const minY = Math.floor(bounds[1] / cellSize);
      const maxY = Math.floor(bounds[3] / cellSize);
      for (let x = minX; x <= maxX; x += 1) for (let y = minY; y <= maxY; y += 1) {
        for (const index of grid.get(`${x}:${y}`) || []) output.add(index);
      }
      return [...output].filter(index => boundsOverlap(segments[index].bounds, bounds));
    },
  };
}

function segmentParameter(point, segment) {
  const dx = segment.b[0] - segment.a[0];
  const dy = segment.b[1] - segment.a[1];
  const denominator = dx * dx + dy * dy;
  if (denominator <= 1e-20) return 0;
  return ((point[0] - segment.a[0]) * dx + (point[1] - segment.a[1]) * dy) / denominator;
}

function interpolateSegment(segment, t) {
  return [
    segment.a[0] + (segment.b[0] - segment.a[0]) * t,
    segment.a[1] + (segment.b[1] - segment.a[1]) * t,
  ];
}

function segmentIntersections(left, right) {
  const p = left.a;
  const r = [left.b[0] - left.a[0], left.b[1] - left.a[1]];
  const q = right.a;
  const s = [right.b[0] - right.a[0], right.b[1] - right.a[1]];
  const cross = (a, b) => a[0] * b[1] - a[1] * b[0];
  const qmp = [q[0] - p[0], q[1] - p[1]];
  const denominator = cross(r, s);
  const epsilon = 1e-8;
  if (Math.abs(denominator) > epsilon) {
    const t = cross(qmp, s) / denominator;
    const u = cross(qmp, r) / denominator;
    if (t < -1e-9 || t > 1 + 1e-9 || u < -1e-9 || u > 1 + 1e-9) return [];
    const normalizedT = Math.max(0, Math.min(1, t));
    const normalizedU = Math.max(0, Math.min(1, u));
    return [{ t: normalizedT, u: normalizedU, point: interpolateSegment(left, normalizedT) }];
  }
  if (Math.abs(cross(qmp, r)) > epsilon) return [];
  const output = [];
  const add = (point, t, u) => {
    if (t < -1e-9 || t > 1 + 1e-9 || u < -1e-9 || u > 1 + 1e-9) return;
    if (output.some(item => coordinateDistance(item.point, point) < 1e-5)) return;
    output.push({ point, t: Math.max(0, Math.min(1, t)), u: Math.max(0, Math.min(1, u)) });
  };
  add(left.a, 0, segmentParameter(left.a, right));
  add(left.b, 1, segmentParameter(left.b, right));
  add(right.a, segmentParameter(right.a, left), 0);
  add(right.b, segmentParameter(right.b, left), 1);
  return output;
}

function uniqueParameters(values) {
  return [...values]
    .filter(Number.isFinite)
    .map(value => Math.max(0, Math.min(1, value)))
    .sort((a, b) => a - b)
    .filter((value, index, array) => index === 0 || Math.abs(value - array[index - 1]) > 1e-9);
}

function splitSegment(segment, parameters) {
  const sorted = uniqueParameters(parameters);
  const output = [];
  for (let index = 0; index < sorted.length - 1; index += 1) {
    const a = interpolateSegment(segment, sorted[index]);
    const b = interpolateSegment(segment, sorted[index + 1]);
    if (coordinateDistance(a, b) > 1e-5) output.push(__riverOwnDataMerge({}, segment, { a, b, bounds: null }));
  }
  return output;
}

function distancePointToSegment(point, segment) {
  const t = Math.max(0, Math.min(1, segmentParameter(point, segment)));
  return coordinateDistance(point, interpolateSegment(segment, t));
}

function stableRiverId(feature, index) {
  return String(
    feature?.properties?.pandolab_id
    ?? feature?.properties?.source_logical_id
    ?? feature?.properties?.__logicalFid
    ?? feature?.id
    ?? `river-${index}`,
  );
}

function collectRiverParts(features, workspace, metricBounds, minLength) {
  const output = [];
  features.forEach((feature, featureIndex) => {
    if (feature?.properties?.category && feature.properties.category !== 'river') return;
    const sourceId = stableRiverId(feature, featureIndex);
    lineParts(feature?.geometry).forEach((part, partIndex) => {
      const points = (part || []).filter(finiteCoordinate).map(workspace.toMeters);
      const clean = [];
      for (const point of points) {
        if (!clean.length || coordinateDistance(clean[clean.length - 1], point) >= minLength) clean.push(point);
      }
      if (clean.length < 2) return;
      const bounds = [Infinity, Infinity, -Infinity, -Infinity];
      for (const point of clean) {
        bounds[0] = Math.min(bounds[0], point[0]);
        bounds[1] = Math.min(bounds[1], point[1]);
        bounds[2] = Math.max(bounds[2], point[0]);
        bounds[3] = Math.max(bounds[3], point[1]);
      }
      if (!boundsOverlap(bounds, metricBounds)) return;
      output.push({ sourceId, partIndex, points: clean });
    });
  });
  return output;
}

function snapRiverEndpoints(parts, tolerance) {
  if (!(tolerance > 0)) return;
  const endpoints = [];
  parts.forEach((part, partIndex) => {
    endpoints.push({ partIndex, pointIndex: 0, point: part.points[0] });
    endpoints.push({ partIndex, pointIndex: part.points.length - 1, point: part.points[part.points.length - 1] });
  });
  const parent = endpoints.map((_, index) => index);
  const find = index => {
    while (parent[index] !== index) {
      parent[index] = parent[parent[index]];
      index = parent[index];
    }
    return index;
  };
  const unite = (left, right) => {
    const a = find(left);
    const b = find(right);
    if (a !== b) parent[Math.max(a, b)] = Math.min(a, b);
  };
  const cells = new Map();
  endpoints.forEach((endpoint, index) => {
    const x = Math.floor(endpoint.point[0] / tolerance);
    const y = Math.floor(endpoint.point[1] / tolerance);
    for (let dx = -1; dx <= 1; dx += 1) for (let dy = -1; dy <= 1; dy += 1) {
      for (const candidate of cells.get(`${x + dx}:${y + dy}`) || []) {
        if (coordinateDistance(endpoint.point, endpoints[candidate].point) <= tolerance) unite(index, candidate);
      }
    }
    const key = `${x}:${y}`;
    if (!cells.has(key)) cells.set(key, []);
    cells.get(key).push(index);
  });
  const groups = new Map();
  endpoints.forEach((endpoint, index) => {
    const root = find(index);
    if (!groups.has(root)) groups.set(root, []);
    groups.get(root).push(endpoint);
  });
  for (const group of groups.values()) {
    if (group.length < 2) continue;
    const point = [
      group.reduce((sum, item) => sum + item.point[0], 0) / group.length,
      group.reduce((sum, item) => sum + item.point[1], 0) / group.length,
    ];
    for (const endpoint of group) parts[endpoint.partIndex].points[endpoint.pointIndex] = [...point];
  }
}

function buildInputSegments(component, riverFeatures, workspace, config, diagnostics) {
  const metricRings = component.map(ring => ensureClosedRing(ring).map(workspace.toMeters));
  const boundarySegments = [];
  metricRings.forEach((ring, ringIndex) => {
    for (let index = 0; index < ring.length - 1; index += 1) {
      if (coordinateDistance(ring[index], ring[index + 1]) <= 1e-5) continue;
      boundarySegments.push({ a: ring[index], b: ring[index + 1], kind: 'boundary', ringIndex, split: [0, 1] });
    }
  });
  const bounds = componentBounds(metricRings);
  const parts = collectRiverParts(riverFeatures, workspace, bounds, config.minRiverEdgeM);
  snapRiverEndpoints(parts, config.riverEndpointSnapM);
  const riverSegments = [];
  for (const part of parts) for (let index = 0; index < part.points.length - 1; index += 1) {
    const a = part.points[index];
    const b = part.points[index + 1];
    if (coordinateDistance(a, b) < config.minRiverEdgeM) continue;
    const segment = { a, b, kind: 'river', sourceIds: new Set([part.sourceId]), split: [0, 1] };
    segment.bounds = segmentBounds(segment);
    if (boundsOverlap(segment.bounds, bounds)) riverSegments.push(segment);
  }
  diagnostics.scannedRiverParts += parts.length;
  diagnostics.scannedRiverSegments += riverSegments.length;
  return { metricRings, boundarySegments, riverSegments };
}

function nodeSegments(boundarySegments, riverSegments, metricRings, config, diagnostics) {
  const boundaryGrid = createSegmentGrid(boundarySegments, config.spatialGridCellM);
  // Quantized hydro endpoints can miss the interior of a boundary edge by a few
  // centimetres. Node both edges, not just the river, before pruning the graph.
  const tolerance = Math.max(0, Number(config.nodeMergeToleranceM) || 0);
  for (const river of riverSegments) {
    for (const endpoint of ['a', 'b']) {
      const point = river[endpoint];
      let nearest = null;
      for (const index of boundaryGrid.query([
        point[0] - tolerance, point[1] - tolerance,
        point[0] + tolerance, point[1] + tolerance,
      ])) {
        const boundary = boundarySegments[index];
        const parameter = Math.max(0, Math.min(1, segmentParameter(point, boundary)));
        const projected = interpolateSegment(boundary, parameter);
        const distance = coordinateDistance(point, projected);
        if (distance > tolerance) continue;
        if (!nearest || distance < nearest.distance || (distance === nearest.distance && index < nearest.index)) {
          nearest = { index, distance, parameter, projected };
        }
      }
      if (!nearest) continue;
      river[endpoint] = nearest.projected;
      boundarySegments[nearest.index].split.push(nearest.parameter);
    }
    river.bounds = segmentBounds(river);
  }
  for (const river of riverSegments) {
    for (const boundaryIndex of boundaryGrid.query(river.bounds)) {
      const boundary = boundarySegments[boundaryIndex];
      for (const hit of segmentIntersections(river, boundary)) {
        river.split.push(hit.t);
        boundary.split.push(hit.u);
        diagnostics.boundaryIntersectionCount += 1;
      }
    }
  }
  const riverGrid = createSegmentGrid(riverSegments, config.spatialGridCellM);
  const visitedPairs = new Set();
  riverSegments.forEach((left, leftIndex) => {
    for (const rightIndex of riverGrid.query(left.bounds)) {
      if (rightIndex <= leftIndex) continue;
      const key = `${leftIndex}:${rightIndex}`;
      if (visitedPairs.has(key)) continue;
      visitedPairs.add(key);
      const right = riverSegments[rightIndex];
      for (const hit of segmentIntersections(left, right)) {
        left.split.push(hit.t);
        right.split.push(hit.u);
        diagnostics.riverIntersectionCount += 1;
      }
    }
  });
  const boundaryPieces = boundarySegments.flatMap(segment => splitSegment(segment, segment.split));
  const pieceGrid = createSegmentGrid(boundaryPieces, config.spatialGridCellM);
  const riverPieces = [];
  for (const segment of riverSegments) for (const piece of splitSegment(segment, segment.split)) {
    const midpoint = [(piece.a[0] + piece.b[0]) / 2, (piece.a[1] + piece.b[1]) / 2];
    if (!pointInComponent(midpoint, metricRings)) continue;
    const onBoundary = pieceGrid.query(segmentBounds(piece)).some(index => (
      distancePointToSegment(midpoint, boundaryPieces[index]) <= config.boundaryCoincidenceToleranceM
    ));
    if (onBoundary) {
      diagnostics.boundaryFollowingEdges += 1;
      continue;
    }
    riverPieces.push(piece);
  }
  return { boundaryPieces, riverPieces };
}

function createGraph(boundaryPieces, riverPieces, config) {
  const nodes = [];
  const nodeGrid = new Map();
  const tolerance = Math.max(1e-5, config.nodeMergeToleranceM);
  const resolveNode = (point, boundary = false) => {
    const x = Math.floor(point[0] / tolerance);
    const y = Math.floor(point[1] / tolerance);
    for (let dx = -1; dx <= 1; dx += 1) for (let dy = -1; dy <= 1; dy += 1) {
      for (const nodeId of nodeGrid.get(`${x + dx}:${y + dy}`) || []) {
        if (coordinateDistance(point, nodes[nodeId].point) <= tolerance) {
          if (boundary) nodes[nodeId].boundary = true;
          return nodeId;
        }
      }
    }
    const id = nodes.length;
    nodes.push({ id, point: [...point], boundary, edgeIds: new Set() });
    const key = `${x}:${y}`;
    if (!nodeGrid.has(key)) nodeGrid.set(key, []);
    nodeGrid.get(key).push(id);
    return id;
  };
  const edges = [];
  const edgeByNodes = new Map();
  const addEdge = (piece, boundary) => {
    const a = resolveNode(piece.a, boundary);
    const b = resolveNode(piece.b, boundary);
    if (a === b) return;
    const key = a < b ? `${a}:${b}` : `${b}:${a}`;
    let edge = edgeByNodes.get(key);
    if (!edge) {
      edge = { id: edges.length, a, b, boundary: false, sourceIds: new Set(), activeRiver: false };
      edges.push(edge);
      edgeByNodes.set(key, edge);
      nodes[a].edgeIds.add(edge.id);
      nodes[b].edgeIds.add(edge.id);
    }
    if (boundary) edge.boundary = true;
    else for (const sourceId of piece.sourceIds || []) edge.sourceIds.add(sourceId);
  };
  boundaryPieces.forEach(piece => addEdge(piece, true));
  riverPieces.forEach(piece => addEdge(piece, false));
  for (const edge of edges) edge.activeRiver = !edge.boundary && edge.sourceIds.size > 0;
  return { nodes, edges };
}

function riverComponents(graph, activeEdgeIds) {
  const remaining = new Set(activeEdgeIds);
  const output = [];
  while (remaining.size) {
    const first = remaining.values().next().value;
    const edgeIds = new Set();
    const nodeIds = new Set();
    const queue = [graph.edges[first].a, graph.edges[first].b];
    while (queue.length) {
      const nodeId = queue.pop();
      if (nodeIds.has(nodeId)) continue;
      nodeIds.add(nodeId);
      for (const edgeId of graph.nodes[nodeId].edgeIds) {
        if (!activeEdgeIds.has(edgeId)) continue;
        edgeIds.add(edgeId);
        remaining.delete(edgeId);
        const edge = graph.edges[edgeId];
        queue.push(edge.a === nodeId ? edge.b : edge.a);
      }
    }
    output.push({ edgeIds, nodeIds });
  }
  return output;
}

function pruneRiverGraph(graph, diagnostics) {
  const active = new Set(graph.edges.filter(edge => edge.activeRiver).map(edge => edge.id));
  for (const component of riverComponents(graph, active)) {
    const boundaryNodes = [...component.nodeIds].filter(nodeId => graph.nodes[nodeId].boundary);
    if (boundaryNodes.length >= 2) continue;
    for (const edgeId of component.edgeIds) active.delete(edgeId);
  }
  let changed = true;
  while (changed) {
    changed = false;
    const degree = new Map();
    for (const edgeId of active) {
      const edge = graph.edges[edgeId];
      degree.set(edge.a, (degree.get(edge.a) || 0) + 1);
      degree.set(edge.b, (degree.get(edge.b) || 0) + 1);
    }
    for (const [nodeId, count] of degree) {
      if (count > 1 || graph.nodes[nodeId].boundary) continue;
      for (const edgeId of graph.nodes[nodeId].edgeIds) if (active.delete(edgeId)) changed = true;
    }
  }
  for (const component of riverComponents(graph, active)) {
    const boundaryNodes = [...component.nodeIds].filter(nodeId => graph.nodes[nodeId].boundary);
    if (boundaryNodes.length >= 2) continue;
    for (const edgeId of component.edgeIds) active.delete(edgeId);
  }
  diagnostics.retainedRiverEdges += active.size;
  diagnostics.prunedRiverEdges += graph.edges.filter(edge => edge.activeRiver).length - active.size;
  return active;
}

function canonicalRingKey(ring) {
  const values = ensureClosedRing(ring).slice(0, -1).map(point => (
    `${Math.round(point[0] * 1e7)},${Math.round(point[1] * 1e7)}`
  ));
  if (!values.length) return '';
  const variants = [];
  for (const sequence of [values, [...values].reverse()]) {
    let bestIndex = 0;
    for (let index = 1; index < sequence.length; index += 1) if (sequence[index] < sequence[bestIndex]) bestIndex = index;
    variants.push([...sequence.slice(bestIndex), ...sequence.slice(0, bestIndex)].join(';'));
  }
  return variants.sort()[0];
}

function canonicalGeometryKey(geometry) {
  return polygonCoordinates(geometry)
    .map(polygon => (polygon || []).map(canonicalRingKey).sort().join('|'))
    .sort()
    .join('||');
}

function hashText(value) {
  let hash = 2166136261;
  for (let index = 0; index < value.length; index += 1) {
    hash ^= value.charCodeAt(index);
    hash = Math.imul(hash, 16777619);
  }
  return (hash >>> 0).toString(16).padStart(8, '0');
}

function geometryFromPolygonCoordinates(coordinates) {
  return { type: 'Polygon', coordinates: clone(coordinates) };
}

function multipolygonInput(geometry) {
  return polygonCoordinates(geometry);
}

function clipFaceToComponent(face, component, workspace, graph, clipper, config) {
  const ring = face.ring.map(workspace.toLonLat);
  const clipped = clipper.intersection([component], [[ring]]) || [];
  const riverBoundarySegments = face.riverEdges.map(edgeId => {
    const edge = graph.edges[edgeId];
    return [workspace.toLonLat(graph.nodes[edge.a].point), workspace.toLonLat(graph.nodes[edge.b].point)];
  });
  const sourceRiverIds = [...new Set(face.riverEdges.flatMap(edgeId => [...graph.edges[edgeId].sourceIds]))].sort();
  return clipped.map(polygon => geometryFromPolygonCoordinates(polygon)).filter(geometry => (
    metricGeometryArea(geometry, workspace) >= config.minCandidateAreaM2
  )).map(geometry => ({ geometry, riverBoundarySegments: clone(riverBoundarySegments), sourceRiverIds }));
}

function validatePartition(component, rows, workspace, clipper, config) {
  if (rows.length < 2) return { valid: false, reason: '하천이 영토를 둘 이상의 영역으로 나누지 않습니다.' };
  for (let left = 0; left < rows.length; left += 1) for (let right = left + 1; right < rows.length; right += 1) {
    const overlap = clipper.intersection(multipolygonInput(rows[left].geometry), multipolygonInput(rows[right].geometry));
    const overlapGeometry = overlap?.length ? { type: 'MultiPolygon', coordinates: overlap } : null;
    if (overlapGeometry && metricGeometryArea(overlapGeometry, workspace) > config.coverageToleranceM2) {
      return { valid: false, reason: '하천 분할 영역끼리 겹칩니다.' };
    }
  }
  const union = clipper.union(...rows.map(row => multipolygonInput(row.geometry))) || [];
  const missing = clipper.difference([component], union) || [];
  const extra = clipper.difference(union, [component]) || [];
  const missingArea = missing.length ? metricGeometryArea({ type: 'MultiPolygon', coordinates: missing }, workspace) : 0;
  const extraArea = extra.length ? metricGeometryArea({ type: 'MultiPolygon', coordinates: extra }, workspace) : 0;
  if (missingArea > config.coverageToleranceM2 || extraArea > config.coverageToleranceM2) {
    return { valid: false, reason: '하천 분할 결과가 원본 영토를 정확히 덮지 못합니다.', missingArea, extraArea };
  }
  return { valid: true, missingArea, extraArea };
}

function partitionComponent({ donorId, donorRevision, component, componentIndex, riverFeatures, clipper, config, algorithmRevision, diagnostics }) {
  const workspace = createRiverPartitionWorkspace(component);
  const { metricRings, boundarySegments, riverSegments } = buildInputSegments(component, riverFeatures, workspace, config, diagnostics);
  if (!riverSegments.length) return { status: 'empty', candidates: [], reason: '영토 안을 지나는 강이 없습니다.' };
  const { boundaryPieces, riverPieces } = nodeSegments(boundarySegments, riverSegments, metricRings, config, diagnostics);
  const graph = createGraph(boundaryPieces, riverPieces, config);
  const activeRiverEdges = pruneRiverGraph(graph, diagnostics);
  if (!activeRiverEdges.size) return { status: 'empty', candidates: [], reason: '국경의 서로 다른 두 지점을 연결하는 강이 없습니다.' };
  const faces = tracePlanarGraphFaces(graph, graph.edges.filter(edge => edge.boundary || activeRiverEdges.has(edge.id)))
    .map(face => (__riverOwnDataMerge({}, face, { riverEdges: face.edgeIds.filter(id => activeRiverEdges.has(id)) })));
  diagnostics.tracedFaceCount += faces.length;
  const rows = [];
  const seen = new Set();
  for (const face of faces) {
    if (!face.riverEdges.length || face.area < config.minCandidateAreaM2) continue;
    for (const row of clipFaceToComponent(face, component, workspace, graph, clipper, config)) {
      const geometryKey = canonicalGeometryKey(row.geometry);
      if (!geometryKey || seen.has(geometryKey)) continue;
      seen.add(geometryKey);
      rows.push(__riverOwnDataMerge({}, row, { geometryKey, areaM2: metricGeometryArea(row.geometry, workspace) }));
    }
  }
  const validation = validatePartition(component, rows, workspace, clipper, config);
  if (!validation.valid) return { status: rows.length ? 'invalid' : 'empty', candidates: [], reason: validation.reason };
  const componentKey = `${donorId}:${componentIndex}`;
  const candidates = rows.map(row => {
    const keySource = [donorId, donorRevision, componentKey, row.sourceRiverIds.join(','), row.geometryKey, algorithmRevision].join('|');
    return {
      key: `${donorId}:river-cell:${hashText(keySource)}`,
      donorCountryId: donorId,
      componentKey,
      geometry: row.geometry,
      areaM2: row.areaM2,
      area: row.areaM2,
      sourceRiverIds: row.sourceRiverIds,
      riverBoundarySegments: row.riverBoundarySegments,
      algorithmRevision,
    };
  }).sort((left, right) => left.key.localeCompare(right.key));
  return { status: 'ready', candidates, reason: '' };
}

export function riverTerritoryPartitionConfigFingerprint(config = RIVER_TERRITORY_PARTITION_CONFIG) {
  return Object.keys(config).sort().map(key => `${key}:${Number(config[key])}`).join('|');
}

export function buildRiverTerritoryPartitions({
  donors = [],
  riverFeatures = [],
  clipper = null,
  config = null,
  algorithmRevision = RIVER_TERRITORY_PARTITION_ALGORITHM_REVISION,
  hydroRevision = '',
} = {}) {
  if (!clipper?.intersection || !clipper?.union || !clipper?.difference) {
    throw new Error('하천 영토 분할에 필요한 polygon-clipping을 사용할 수 없습니다.');
  }
  const startedAt = globalThis.performance?.now?.() ?? Date.now();
  const resolvedConfig = __riverOwnDataMerge({}, RIVER_TERRITORY_PARTITION_CONFIG, (config || {}));
  const diagnostics = {
    algorithmRevision,
    hydroRevision,
    scannedDonors: 0,
    scannedRiverFeatures: riverFeatures.length,
    scannedRiverParts: 0,
    scannedRiverSegments: 0,
    boundaryIntersectionCount: 0,
    riverIntersectionCount: 0,
    boundaryFollowingEdges: 0,
    retainedRiverEdges: 0,
    prunedRiverEdges: 0,
    tracedFaceCount: 0,
    candidateCount: 0,
    computeMs: 0,
  };
  const candidates = [];
  const donorResults = [];
  for (const donor of donors || []) {
    const donorId = String(donor?.countryId ?? donor?.id ?? '');
    const geometry = donor?.geometry;
    const donorRevision = String(donor?.geometryRevision ?? donor?.properties?.geometryRevision ?? '');
    const components = polygonCoordinates(geometry);
    if (!donorId || !components.length) continue;
    diagnostics.scannedDonors += 1;
    const donorCandidates = [];
    let invalidReason = '';
    const emptyReasons = [];
    for (let componentIndex = 0; componentIndex < components.length; componentIndex += 1) {
      const result = partitionComponent({
        donorId,
        donorRevision,
        component: components[componentIndex],
        componentIndex,
        riverFeatures,
        clipper,
        config: resolvedConfig,
        algorithmRevision,
        diagnostics,
      });
      if (result.status === 'invalid') {
        invalidReason = result.reason || '하천 분할 결과가 유효하지 않습니다.';
        break;
      }
      if (result.status === 'ready') donorCandidates.push(...result.candidates);
      else if (result.reason) emptyReasons.push(result.reason);
    }
    if (invalidReason) {
      donorResults.push({ donorCountryId: donorId, status: 'invalid', candidateCount: 0, reason: invalidReason });
      continue;
    }
    candidates.push(...donorCandidates);
    donorResults.push({
      donorCountryId: donorId,
      status: donorCandidates.length ? 'ready' : 'empty',
      candidateCount: donorCandidates.length,
      reason: donorCandidates.length ? '' : (emptyReasons[0] || '영토를 분리하는 강이 없습니다.'),
    });
  }
  diagnostics.candidateCount = candidates.length;
  diagnostics.computeMs = (globalThis.performance?.now?.() ?? Date.now()) - startedAt;
  return { candidates, donorResults, diagnostics };
}
