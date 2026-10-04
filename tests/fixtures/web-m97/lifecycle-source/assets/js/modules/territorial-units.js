import './territorial-edit-plan.js';
import {
  normalizeTemporalInterval,
} from './temporal.js';

export const TERRITORIAL_SCHEMA_VERSION = 5;

export const TERRITORIAL_ENTITY_KINDS = Object.freeze({ GENERAL: 'general', REGIONAL: 'regional' });

export const TERRITORIAL_COVERAGE_MODES = Object.freeze({
  PARTITION: 'partition',
  EXPLICIT: 'explicit',
});

const POLYGON_TYPES = new Set(['Polygon', 'MultiPolygon']);
const ENTITY_KINDS = new Set(Object.values(TERRITORIAL_ENTITY_KINDS));
const text = value => String(value ?? '').trim();
const clone = value => structuredClone(value);

export const TERRITORIAL_IDENTITY_FIELDS = Object.freeze(['schemaVersion', 'entityKind', 'name',
  'style', 'locked', 'notes', 'metadata', 'sourceFolderId', 'sourceLibraryId', 'sourceGeometryVersion']);

/** Persisted identity. Geometry, lifetime and administrative relationships live in records. */
export function normalizeTerritorialIdentity(feature) {
  const fail = message => { throw Object.assign(new Error(message), { code: 'PL-SCHEMA-FIELD' }); };
  if (!feature || feature.type !== 'Feature' || typeof feature.id !== 'string' || !feature.id.length
    || feature.geometry !== null || Object.keys(feature).some(key => !['type', 'id', 'properties', 'geometry'].includes(key)))
    fail('영역 정체성에는 문자열 ID와 null geometry가 필요합니다.');
  const p = feature.properties;
  if (!p || typeof p !== 'object' || Array.isArray(p) || p.schemaVersion !== TERRITORIAL_SCHEMA_VERSION
    || !ENTITY_KINDS.has(p.entityKind) || Object.keys(p).some(key => !TERRITORIAL_IDENTITY_FIELDS.includes(key)))
    fail('영역 정체성 필드 또는 schemaVersion이 올바르지 않습니다.');
  for (const key of ['name', 'notes', 'sourceFolderId', 'sourceLibraryId', 'sourceGeometryVersion']) {
    if (Object.hasOwn(p, key) && typeof p[key] !== 'string') fail(`${key} 문자열이 필요합니다.`);
  }
  if (Object.hasOwn(p, 'locked') && typeof p.locked !== 'boolean') fail('locked 불리언이 필요합니다.');
  for (const key of ['metadata', 'style']) {
    if (Object.hasOwn(p, key) && (!p[key] || typeof p[key] !== 'object' || Array.isArray(p[key]))) fail(`${key} 객체가 필요합니다.`);
  }
  return { type: 'Feature', id: feature.id, geometry: null, properties: {
    schemaVersion: TERRITORIAL_SCHEMA_VERSION, entityKind: p.entityKind,
    name: text(p.name), style: clone(p.style || {}), locked: p.locked === true,
    notes: text(p.notes), metadata: clone(p.metadata || {}), sourceFolderId: text(p.sourceFolderId),
    sourceLibraryId: text(p.sourceLibraryId), sourceGeometryVersion: text(p.sourceGeometryVersion),
  } };
}

export function normalizeTerritorialIdentities(entities) {
  if (!Array.isArray(entities)) throw new TypeError('영역 정체성 배열이 필요합니다.');
  const ids = new Set();
  return entities.map(feature => {
    const identity = normalizeTerritorialIdentity(feature);
    if (ids.has(identity.id)) throw new Error(`영역 ID가 중복되었습니다: ${identity.id}`);
    ids.add(identity.id);
    return identity;
  });
}

function territorialEntityKind(feature) {
  const properties = feature?.properties || {};
  const value = text(properties.entityKind).toLowerCase();
  return ENTITY_KINDS.has(value) ? value : '';
}

function isTerritorialFeature(feature) {
  return !!territorialEntityKind(feature)
    && POLYGON_TYPES.has(feature?.geometry?.type)
    && Array.isArray(feature.geometry.coordinates)
    && feature.geometry.coordinates.length > 0;
}

function normalizedProperties(feature, type) {
  const source = feature?.properties || {};
  if (Number(source.schemaVersion) !== TERRITORIAL_SCHEMA_VERSION) throw new Error('영역 schemaVersion이 현재 형식과 일치하지 않습니다.');
  const parentId = text(source.parentId);
  for (const field of ['unitType', 'associatedCountryId', 'sovereignId']) {
    if (Object.hasOwn(source, field)) throw new Error(`${field}는 현재 객체 모델의 필드가 아닙니다.`);
  }
  const coverageMode = text(source.coverageMode);
  if (![TERRITORIAL_COVERAGE_MODES.EXPLICIT, TERRITORIAL_COVERAGE_MODES.PARTITION].includes(coverageMode)) {
    throw new Error('영역 coverageMode가 올바르지 않습니다.');
  }
  const interval = normalizeTemporalInterval(source.validFrom, source.validTo);
  const sourceStyle = source.style && typeof source.style === 'object' ? source.style : {};
  const color = text(sourceStyle.color);
  const properties = {
    schemaVersion: TERRITORIAL_SCHEMA_VERSION,
    entityKind: type,
    name: text(source.name),
    parentId,
    coverageMode,
    style: color ? { ...sourceStyle, color } : { ...sourceStyle },
    locked: source.locked === true,
    validFrom: interval.validFrom,
    validTo: interval.validTo,
    notes: text(source.notes),
    metadata: source.metadata && typeof source.metadata === 'object' ? clone(source.metadata) : {},
    sourceFolderId: text(source.sourceFolderId),
    sourceLibraryId: text(source.sourceLibraryId),
    sourceGeometryVersion: text(source.sourceGeometryVersion),
  };
  if (!color) delete properties.style.color;
  return properties;
}

export function normalizeTerritorialFeature(feature, { cloneGeometry = clone } = {}) {
  const type = territorialEntityKind(feature);
  if (!type || !isTerritorialFeature(feature)) return null;
  const id = text(feature.id);
  if (!id) throw new Error('영역 ID가 비어 있습니다.');
  return {
    type: 'Feature',
    id,
    properties: normalizedProperties(feature, type),
    geometry: cloneGeometry(feature.geometry),
  };
}

export const territorialRootId = globalThis.PandoLabTerritorialEdit.territorialRootId;

export function normalizeTerritorialEntities(value, {
  getEntity = () => null,
  validatedUnchanged = null,
  cloneGeometry = clone,
} = {}) {
  const normalized = [];
  const seen = new Set();
  for (const raw of Array.isArray(value) ? value : []) {
    const feature = validatedUnchanged?.has(raw) ? raw : normalizeTerritorialFeature(raw, { cloneGeometry });
    if (!feature) throw new Error('영역 형식이 올바르지 않습니다.');
    if (seen.has(feature.id)) throw new Error(`영역 ID가 중복되었습니다: ${feature.id}`);
    seen.add(feature.id);
    normalized.push(feature);
  }

  const byId = new Map(normalized.map(feature => [feature.id, feature]));
  const resolve = id => byId.get(text(id)) || getEntity(text(id));
  for (const feature of normalized) {
    const properties = feature.properties;
    if (properties.entityKind === 'regional' && properties.parentId) throw new Error('독립 권역에는 부모를 지정할 수 없습니다.');
    if (!properties.parentId && properties.coverageMode !== 'explicit') throw new Error('최상위 객체와 독립 권역은 explicit 형상이어야 합니다.');
    territorialRootId(feature, resolve);
  }
  return normalized;
}

export function createTerritorialFeature({
  id,
  entityKind,
  name = '',
  geometry,
  parentId = '',
  coverageMode,
  color = '',
  locked = false,
  validFrom = null,
  validTo = null,
  notes = '',
  metadata = {},
  sourceFolderId = '',
  sourceLibraryId = '',
  sourceGeometryVersion = '',
}) {
  const resolvedCoverageMode = coverageMode || (entityKind === 'general' && text(parentId) ? 'partition' : 'explicit');
  const feature = normalizeTerritorialFeature({
    type: 'Feature',
    id,
    properties: {
      schemaVersion: TERRITORIAL_SCHEMA_VERSION,
      entityKind,
      name,
      parentId,
      coverageMode: resolvedCoverageMode,
      style: color ? { color } : {},
      locked,
      validFrom,
      validTo,
      notes,
      metadata,
      sourceFolderId,
      sourceLibraryId,
      sourceGeometryVersion,
    },
    geometry,
  });
  if (!feature) throw new Error('영역 형식이 올바르지 않습니다.');
  return feature;
}

export async function runTerritorialTransaction({
  snapshot,
  calculate,
  validate = () => ({ ok: true }),
  apply,
  restore,
  recordHistory,
  autosave,
}) {
  const before = snapshot();
  try {
    const result = await calculate();
    const validation = await validate(result);
    if (validation === false || validation?.ok === false) {
      throw new Error(validation?.message || validation?.issues?.[0] || '영역 관계가 올바르지 않습니다.');
    }
    await apply(result);
    recordHistory(before);
    autosave();
    return result;
  } catch (error) {
    await restore(before);
    throw error;
  }
}
