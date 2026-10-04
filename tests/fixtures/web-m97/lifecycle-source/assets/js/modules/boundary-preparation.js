import { prepareBoundaryDisplay } from './boundary-display.js';
import { boundarySourceSegments, buildBoundaryTopologyFromSegments, planSharedBoundaryEdit, planCoastEdit, moveTopologyNode, topologyNodeKey } from './boundary-topology.js';
import { createBoundarySpatialIndex, segmentBounds } from './boundary-spatial-index.js';

const locked = feature => !!(feature.boundaryLocked ?? feature.properties?.locked);
const metadata = feature => JSON.stringify([feature.properties?.entityKind, feature.properties?.parentId, undefined, locked(feature)]);
const padded = bounds => {
  // pointOnSegment also allows an epsilon of the segment parameter at its ends.
  const margin = 1e-7 * (1 + Math.hypot(bounds[2] - bounds[0], bounds[3] - bounds[1]));
  return [bounds[0] - margin, bounds[1] - margin, bounds[2] + margin, bounds[3] + margin];
};

export function createBoundaryPreparation() {
  const sources = new Map();
  const index = createBoundarySpatialIndex();
  const cache = new Map();
  let sequence = 0;
  const namespace = globalThis.crypto?.randomUUID?.() || `${Date.now()}:${Math.random()}`;

  async function sync(features, checkpoint = async () => {}) {
    const incoming = new Map(features.map(feature => [String(feature.id), feature]));
    const changed = new Set();
    const dirty = [];
    try {
      for (const [id, previous] of sources) {
        const feature = incoming.get(id);
        if (feature?.geometry === previous.feature.geometry && metadata(feature) === previous.metadata) continue;
        changed.add(id);
        for (const row of previous.rows) { dirty.push(row.bounds); index.remove(row.id); }
        sources.delete(id);
      }
      for (const [id, feature] of incoming) {
        if (sources.has(id)) continue;
        await checkpoint();
        changed.add(id);
        const rows = boundarySourceSegments(feature).map((row, i) => ({ ...row, id: `${id}:${i}`, bounds: segmentBounds(row) }));
        for (const row of rows) { index.insert(row.id, row, row.bounds); dirty.push(row.bounds); }
        sources.set(id, { feature, rows, metadata: metadata(feature) });
      }
    } finally {
      if (changed.size) for (const [key, entry] of cache) {
        if ([...changed].some(id => entry.dependencies.has(id)) || dirty.some(bounds => entry.targetIndex.query(padded(bounds)).length)) cache.delete(key);
      }
    }
  }

  async function prepare({ targetIds, mode, neighborsOnly = false, autoSeedId = null }, checkpoint = async () => {}) {
    const ids = [...new Set(targetIds.map(String))].sort();
    const targets = ids.map(id => sources.get(id)?.feature);
    if (!targets.length || targets.some(feature => !feature)) throw new Error('경계 편집 대상을 찾을 수 없습니다.');
    if (targets.some(feature => locked(feature))) throw new Error('잠긴 객체의 경계를 편집할 수 없습니다.');
    const unit = targets.find(feature => (feature.properties?.entityKind === 'general' && !!feature.properties?.parentId));
    if (unit && targets.some(feature => !(feature.properties?.entityKind === 'general' && !!feature.properties?.parentId)
      || feature.properties.parentId !== unit.properties.parentId)) throw new Error('같은 상위 단위 안의 하위단위만 편집할 수 있습니다.');
    const parentId = unit ? String(unit.properties.parentId) : null;
    if (unit && !sources.has(parentId)) throw new Error('상위 단위를 찾을 수 없습니다.');
    for (const target of targets) {
      let ancestor = target;
      const visited = new Set();
      while (ancestor?.properties?.parentId) {
        const id = String(ancestor.properties.parentId);
        if (visited.has(id)) throw new Error('상위 단위 관계가 순환합니다.');
        visited.add(id);
        ancestor = sources.get(id)?.feature;
        if (!ancestor) throw new Error('상위 단위를 찾을 수 없습니다.');
        if (locked(ancestor)) throw new Error('상위 단위가 잠겨 있습니다.');
      }
    }
    const allowed = feature => !!feature && (unit ? (feature.properties?.entityKind === 'general' && !!feature.properties?.parentId)
      && feature.properties.parentId === unit.properties.parentId
      : !(feature.properties?.entityKind === 'general' && !!feature.properties?.parentId) && !(feature.properties?.entityKind === 'regional'));
    const key = JSON.stringify([mode, ids, parentId, autoSeedId]);
    let entry = cache.get(key);
    if (!entry) {
      const selected = new Set(ids);
      const rows = new Map();
      const targetIndex = createBoundarySpatialIndex();
      for (const id of ids) for (const row of sources.get(id).rows) {
        await checkpoint();
        rows.set(row.id, row); targetIndex.insert(row.id, row, padded(row.bounds));
        for (const candidate of index.query(padded(row.bounds))) {
          const feature = sources.get(candidate.featureId)?.feature;
          if (allowed(feature) || candidate.featureId === parentId) rows.set(candidate.id, candidate);
        }
      }
      // Parent ownership marks its exterior as an unselected/fixed boundary.
      const topology = buildBoundaryTopologyFromSegments([...rows.values()]);
      const neighbors = new Set();
      for (const segment of topology.segments.values()) if (segment.ownerIds.size >= 2 && [...segment.ownerIds].some(id => selected.has(id))) {
        for (const id of segment.ownerIds) if (!selected.has(id) && id !== parentId && allowed(sources.get(id)?.feature)) neighbors.add(id);
      }
      let editableIds = ids;
      if (unit && autoSeedId && selected.has(String(autoSeedId))) {
        const adjacent = new Map(ids.map(id => [id, new Set()]));
        for (const segment of topology.segments.values()) if (segment.ownerIds.size >= 2 && [...segment.ownerIds].every(id => selected.has(id))) {
          for (const a of segment.ownerIds) for (const b of segment.ownerIds) adjacent.get(a).add(b);
        }
        const connected = new Set([String(autoSeedId)]), queue = [...connected];
        for (let i = 0; i < queue.length; i++) for (const id of adjacent.get(queue[i]) || []) if (!connected.has(id)) { connected.add(id); queue.push(id); }
        editableIds = ids.filter(id => connected.has(id));
      }
      const plan = mode === 'coast' ? planCoastEdit(topology, ids[0]) : planSharedBoundaryEdit(topology, editableIds);
      entry = { id: `boundary:${namespace}:${++sequence}`, key, ids: editableIds, mode, topology, plan, neighbors: [...neighbors], handles: null, segments: null,
        dependencies: new Set([...rows.values()].map(row => row.featureId)), targetIndex };
      cache.set(key, entry);
      while (cache.size > 8) cache.delete(cache.keys().next().value);
    }
    if (!neighborsOnly && !entry.handles) {
      // A locked unselected owner always stays fixed; selected locked targets were rejected above.
      const { topology, plan } = entry;
      const selected = new Set(ids);
      const nodeSegments = new Map();
      for (const segment of topology.segments.values()) for (const nodeKey of [
        topologyNodeKey(segment.a), topologyNodeKey(segment.b),
      ]) {
        if (!nodeSegments.has(nodeKey)) nodeSegments.set(nodeKey, []);
        nodeSegments.get(nodeKey).push({ start: segment.a, end: segment.b });
      }
      const handles = [...new Set([...plan.editableNodeKeys, ...plan.fixedNodeKeys])].map(nodeKey => {
        const node = topology.nodes.get(nodeKey);
        const ref = node.refs.find(row => selected.has(row.featureId)) || node.virtualRefs.find(row => selected.has(row.featureId));
        return { key: nodeKey, nodeKey, coordinate: node.coordinate, coord: node.coordinate,
          polygonIndex: ref?.polygonIndex, ringIndex: ref?.ringIndex, index: ref?.vertexIndex,
          refs: node.refs.filter(row => selected.has(row.featureId)),
          virtualRefs: node.virtualRefs.filter(row => selected.has(row.featureId)),
          ownerIds: [...node.ownerIds], boundaryKind: mode === 'coast' ? 'coast' : 'shared',
          fixed: plan.fixedNodeKeys.has(nodeKey), segments: nodeSegments.get(nodeKey) || [] };
      });
      const segments = [...plan.segmentKeys].map(segmentKey => {
        const row = topology.segments.get(segmentKey);
        return { key: row.key, kind: mode === 'coast' ? 'coast' : 'shared', start: row.a, end: row.b,
          geometry: { type: 'LineString', coordinates: [row.a, row.b] } };
      });
      entry.handles = handles;
      entry.segments = segments;
      entry.displayIndex = prepareBoundaryDisplay(handles, segments);
    }
    return { preparationId: entry.id, mode, selectedIds: entry.ids, neighbors: entry.neighbors,
      valid: mode === 'coast' ? entry.plan.segmentKeys.size > 0 : entry.plan.valid,
      isolatedIds: entry.plan.isolatedIds || [],
      displayIndex: neighborsOnly ? null : entry.displayIndex,
      handles: neighborsOnly ? [] : entry.handles, segments: neighborsOnly ? [] : entry.segments };
  }

  function move({ preparationId, nodeKey, coordinate }) {
    const entry = [...cache.values()].find(row => row.id === preparationId);
    if (!entry) throw new Error('경계 준비 결과가 변경되었습니다. 다시 시작하세요.');
    const node = entry.topology.nodes.get(nodeKey);
    if (!node || !entry.plan.editableNodeKeys.has(nodeKey)) throw new Error('고정된 경계점은 이동할 수 없습니다.');
    if (coordinate?.length !== 2 || !coordinate.every(Number.isFinite) || Math.abs(coordinate[1]) > 90) throw new Error('유효하지 않은 위치입니다.');
    const features = new Map([...node.ownerIds].map(id => {
      const source = sources.get(id)?.feature;
      if (!source || locked(source)) throw new Error('경계 소유 객체가 없거나 잠겨 있습니다.');
      const { boundaryLocked, ...feature } = source;
      return [id, structuredClone(feature)];
    }));
    moveTopologyNode(features, node, coordinate);
    return { features: [...features.values()], affectedIds: [...features.keys()] };
  }
  return { sync, prepare, move };
}
