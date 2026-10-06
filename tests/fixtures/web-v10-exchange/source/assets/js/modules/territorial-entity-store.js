import { normalizeTerritorialEntities, normalizeTerritorialFeature, normalizeTerritorialIdentities,
  TERRITORIAL_IDENTITY_FIELDS } from './territorial-units.js';
import { createGeometryVersionStore } from './geometry-version-store.js';
import { restoreTimelineStorage } from './timeline-storage.js';
import { normalizeTimelineRecords } from './timeline-records.js';
import { assertStaticTimeline, staticTimelineViews } from './timeline-static-view.js';
import { normalizeColorValue } from './color-adapter.js';

const text = value => String(value ?? '').trim();

export function createEmptyTerritorialState() {
  return { territorialEntities: [], timelineRecords: { schemaVersion: 1, lifetimes: [], geometryBindings: [], parentRelations: [] },
    geometries: createGeometryVersionStore() };
}

/** Explicit new static project construction; persisted projects use restoreProject. */
export function createStaticTerritorialSnapshot(features) {
  const state = createEmptyTerritorialState();
  const store = createTerritorialEntityStore({ getState: () => state });
  store.replaceEntities(features);
  return { territorialEntities: store.identities(), timelineRecords: structuredClone(state.timelineRecords),
    geometries: state.geometries.snapshot() };
}

/** Sole writer of the unified Feature collection and entity metadata. */
export function createTerritorialEntityStore({ getState, onEntitiesReplaced = () => {} } = {}) {
  if (typeof getState !== 'function') throw new TypeError('영역 Store에는 상태 공급자가 필요합니다.');
  let cached = null;
  let staging = null;
  const state = () => {
    const value = getState();
    if (!Array.isArray(value?.territorialEntities)) throw new TypeError('territorialEntities 배열이 필요합니다.');
    if (!value.timelineRecords || typeof value.geometries?.snapshot !== 'function')
      throw new TypeError('영역 Store에는 timelineRecords와 geometry version store가 필요합니다.');
    return staging?.state || value;
  };
  const identity = feature => ({ type: 'Feature', id: feature.id, geometry: null,
    properties: Object.fromEntries(TERRITORIAL_IDENTITY_FIELDS.filter(key => Object.hasOwn(feature.properties, key))
      .map(key => [key, feature.properties[key]])) });
  const catalog = identities => identities.map(feature => ({ id: feature.id, entityKind: feature.properties.entityKind }));
  const activationError = () => { throw Object.assign(new Error('날짜별 편집은 T4 구현 후 지원합니다.'), { code: 'TIMELINE_ACTIVATION' }); };
  function candidateFor(next, current, validate = true) {
    assertStaticTimeline(current.timelineRecords, current.territorialEntities);
    const archive = current.geometries.snapshot();
    const geometries = createGeometryVersionStore(archive, { reuse: current.geometries });
    // Candidate-local lookup includes past/unreferenced versions. Read the archive
    // once instead of sorting every geometry again for each incoming entity.
    const highestGeometryVersions = new Map();
    for (const entry of archive) highestGeometryVersions.set(entry.id,
      Math.max(highestGeometryVersions.get(entry.id) || 0, entry.version));
    const records = structuredClone(current.timelineRecords);
    const ids = new Set(next.map(feature => feature.id));
    for (const key of ['lifetimes', 'geometryBindings', 'parentRelations'])
      records[key] = records[key].filter(record => ids.has(record.entityId));
    const recordIds = new Set(['lifetimes', 'geometryBindings', 'parentRelations']
      .flatMap(key => records[key].map(record => record.id)));
    const allocateRecordId = base => {
      let id = base, suffix = 0;
      while (recordIds.has(id)) id = `${base}:${++suffix}`;
      recordIds.add(id);
      return id;
    };
    for (const feature of next) {
      const p = feature.properties, id = feature.id;
      if (p.validFrom !== null || p.validTo !== null) activationError();
      let binding = records.geometryBindings.find(record => record.entityId === id);
      if (!binding || (geometries.get(binding.geometryRef) !== feature.geometry
        && JSON.stringify(geometries.get(binding.geometryRef)) !== JSON.stringify(feature.geometry))) {
        const geometryId = binding?.geometryRef.id || `territorial-geometry:${id}`;
        const version = (highestGeometryVersions.get(geometryId) || 0) + 1;
        const geometryRef = { id: geometryId, version };
        geometries.insert(geometryRef, feature.geometry);
        highestGeometryVersions.set(geometryId, version);
        if (binding) binding.geometryRef = geometryRef;
        else {
          binding = { id: allocateRecordId(`geometry:${id}`), entityId: id, validFrom: null, validTo: null, geometryRef };
          records.geometryBindings.push(binding);
        }
      }
      if (!records.lifetimes.some(record => record.entityId === id))
        records.lifetimes.push({ id: allocateRecordId(`lifetime:${id}`), entityId: id, validFrom: null, validTo: null });
      let parent = records.parentRelations.find(record => record.entityId === id);
      if (!parent) {
        parent = { id: allocateRecordId(`parent:${id}`), entityId: id, validFrom: null, validTo: null };
        records.parentRelations.push(parent);
      }
      parent.parentId = p.parentId;
      parent.coverageMode = p.coverageMode;
    }
    const identities = normalizeTerritorialIdentities(next.map(identity));
    const normalized = validate ? normalizeTimelineRecords(records, { entities: catalog(identities),
      geometryExists: ref => ['Polygon', 'MultiPolygon'].includes(geometries.get(ref)?.type) }) : records;
    const previous = new Map(current.territorialEntities.map(feature => [feature.id, feature]));
    const views = staticTimelineViews(identities, normalized, geometries).map(feature => {
      const before = previous.get(feature.id);
      return before && before.geometry === feature.geometry
        && JSON.stringify(before.properties) === JSON.stringify(feature.properties) ? before : feature;
    });
    return { timelineRecords: normalized, geometries, territorialEntities: views };
  }
  function normalize(entities, unchanged) {
    if (!staging) return normalizeTerritorialEntities(entities, { validatedUnchanged: unchanged, cloneGeometry: geometry => geometry });
    const ids = new Set();
    return entities.map(entity => {
      const next = unchanged?.has(entity) ? entity : normalizeTerritorialFeature(entity, { cloneGeometry: geometry => geometry });
      if (!next || ids.has(next.id)) throw new Error('영역 변경 ID 또는 형식이 올바르지 않습니다.');
      ids.add(next.id);
      return next;
    });
  }
  // Synchronous multi-entity edits have one validation/publication boundary.
  // Removed root anchors remain available to dependent-edit calculations only
  // inside the transaction. They never reach the published collection.
  function transaction(apply) {
    if (typeof apply !== 'function') throw new TypeError('영역 transaction 콜백이 필요합니다.');
    if (staging) return apply();
    const current = state();
    const previous = current.territorialEntities;
    staging = { state: { ...current, territorialEntities: previous,
      geometries: createGeometryVersionStore(current.geometries.snapshot(), { reuse: current.geometries }),
      timelineRecords: structuredClone(current.timelineRecords) }, removed: new Set() };
    try {
      const result = apply();
      if (result?.then) throw new TypeError('영역 transaction은 비동기 계산을 포함할 수 없습니다.');
      const pending = staging;
      const next = normalizeTerritorialEntities(pending.state.territorialEntities.filter(entity => !pending.removed.has(entity.id)), {
        validatedUnchanged: new Set(previous), cloneGeometry: geometry => geometry });
      const candidate = candidateFor(next, pending.state);
      staging = null;
      publishCandidate(candidate, previous);
      return result;
    } finally {
      staging = null;
      cached = null;
    }
  }
  function snapshot() {
    const current = state();
    if (cached?.source === current.territorialEntities && cached.revision === current.stateRevision) return cached.entities;
    const entities = current.territorialEntities.map(feature => ({ ...feature,
      properties: structuredClone(feature.properties), geometry: feature.geometry }));
    cached = { source: current.territorialEntities, revision: current.stateRevision, entities };
    return entities;
  }
  const entity = id => state().territorialEntities.find(feature => text(feature.id) === text(id));
  function hasField(id, field) {
    const properties = entity(id)?.properties;
    if (!properties) return false;
    return Object.hasOwn(field === 'color' ? properties.style : ['capital', 'flagDataUrl'].includes(field)
      ? properties.metadata : properties, field === 'color' ? 'color' : field);
  }
  function setField(id, field, value) {
    const feature = entity(id);
    if (!feature) return false;
    if (['validFrom', 'validTo'].includes(field) && value !== null) activationError();
    const properties = { ...feature.properties };
    if (field === 'color') {
      properties.style = { ...properties.style };
      if (value) properties.style.color = normalizeColorValue(value);
      else delete properties.style.color;
    } else if (['capital', 'flagDataUrl'].includes(field)) {
      properties.metadata = { ...properties.metadata };
      if (value === undefined) delete properties.metadata[field];
      else properties.metadata[field] = value;
    } else {
      if (!Object.hasOwn(properties, field)) throw new Error(`지원하지 않는 영역 필드: ${field}`);
      properties[field] = value;
    }
    applyChanges({ features: [{ ...feature, properties }] });
    return true;
  }
  function replaceEntities(entities) {
    if (!Array.isArray(entities)) throw new TypeError('영역 엔티티 배열이 필요합니다.');
    const previous = state().territorialEntities;
    const next = normalize(entities, new Set(previous));
    if (staging) staging.removed.clear();
    publish(next, previous);
    return snapshot();
  }
  function publish(next, previous) {
    const candidate = candidateFor(next, state(), !staging);
    if (staging) {
      Object.assign(staging.state, candidate);
      cached = null;
      return;
    }
    publishCandidate(candidate, previous);
  }
  function publishCandidate(candidate, previous) {
    const next = candidate.territorialEntities;
    const before = new Map(previous.map(feature => [text(feature.id), feature]));
    const after = new Map(next.map(feature => [text(feature.id), feature]));
    const changedIds = new Set([...before.keys(), ...after.keys()].filter(id => before.get(id) !== after.get(id)));
    Object.assign(state(), candidate);
    cached = null;
    state().historyDirtyEntityIds ||= new Set();
    for (const id of changedIds) state().historyDirtyEntityIds.add(id);
    onEntitiesReplaced(next, { changedIds, previous });
  }
  function applyChanges({ features = [], removedIds = [] }) {
    const previous = state().territorialEntities;
    const removed = new Set(removedIds.map(text));
    const changes = new Map();
    const previousById = new Map(previous.map(feature => [text(feature.id), feature]));
    for (const feature of features) {
      const id = text(feature?.id);
      if (!id || changes.has(id) || removed.has(id)) throw new Error(`영역 변경 ID가 올바르지 않습니다: ${id}`);
      const before = previousById.get(id);
      if (before && before.properties.entityKind !== feature.properties?.entityKind) throw new Error('객체 종류는 생성 후 변경할 수 없습니다. 독립 권역 복사를 사용하세요.');
      changes.set(id, feature);
    }
    if (staging) {
      for (const entity of previous) if (removed.has(entity.id) && entity.properties.entityKind === 'general' && !entity.properties.parentId) {
        staging.removed.add(entity.id);
        removed.delete(entity.id);
      }
      for (const id of changes.keys()) staging.removed.delete(id);
    }
    const unchanged = new Set();
    const candidate = previous.filter(feature => !removed.has(text(feature.id))).map(feature => {
      const replacement = changes.get(text(feature.id));
      changes.delete(text(feature.id));
      if (!replacement) unchanged.add(feature);
      return replacement || feature;
    }).concat([...changes.values()]);
    const next = normalize(candidate, unchanged);
    publish(next, previous);
    return snapshot();
  }
  function appendEntities(entities) {
    if (!Array.isArray(entities)) throw new TypeError('추가할 영역 배열이 필요합니다.');
    const ids = new Set(state().territorialEntities.map(feature => text(feature.id)));
    for (const feature of entities) {
      const id = text(feature?.id);
      if (!id || ids.has(id)) throw new Error(`영역 엔티티 ID가 중복되었거나 비어 있습니다: ${id}`);
      ids.add(id);
    }
    if (!entities.length) return [];
    const added = new Set(entities.map(feature => text(feature.id)));
    return applyChanges({ features: entities }).filter(feature => added.has(text(feature.id)));
  }
  function removeEntities(ids) {
    const wanted = new Set(ids.map(text));
    const deleted = snapshot().filter(feature => wanted.has(text(feature.id)));
    if (deleted.length) applyChanges({ removedIds: deleted.map(feature => feature.id) });
    return deleted;
  }
  function identities() { return normalizeTerritorialIdentities(state().territorialEntities.map(identity)); }
  function restoreProject(project) {
    const metadata = normalizeTerritorialIdentities(project.territorialEntities);
    const storage = restoreTimelineStorage({ schemaVersion: 1, records: project.timelineRecords,
      geometries: project.geometries }, catalog(metadata), { reuse: state().geometries });
    // This store drives the existing static editor: gate before replacing any owner.
    assertStaticTimeline(storage.records, metadata);
    publishCandidate({ territorialEntities: staticTimelineViews(metadata, storage.records, storage.geometries),
      timelineRecords: storage.records, geometries: storage.geometries }, state().territorialEntities);
    return snapshot();
  }
  return Object.freeze({ snapshot, identities, restoreProject, setField, hasField, replaceEntities, applyChanges, appendEntities, removeEntities, transaction,
    setLocked: (id, locked) => setField(id, 'locked', !!locked),
    isLocked: id => entity(id)?.properties.locked === true });
}
