import { normalizeTerritorialIdentities, TERRITORIAL_IDENTITY_FIELDS, TERRITORIAL_SCHEMA_VERSION, TERRITORIAL_ENTITY_KINDS } from './territorial-units.js';
import { createGeometryVersionStore } from './geometry-version-store.js';
import { restoreTimelineStorage } from './timeline-storage.js';
import { staticTimelineViews } from './timeline-static-view.js';
import { assertProjectReferenceIntegrity } from './project-invariants.js';
import { restoreEntitiesFromDelta } from './project-serializer.js';
import { normalizeDistributionLayers, normalizeDistributionEntries } from './distribution-model.js';
import { validateSourceProvenance } from './source-provenance.js';
import {
  PROJECT_SCHEMA_VERSION,
  LAND_OBJECT_SCHEMA_VERSION,
  SOURCE_PROVENANCE_SCHEMA_VERSION,
  TERRITORIAL_MODEL_SCHEMA_VERSION,
  DISTRIBUTION_MODEL_SCHEMA_VERSION,
  LAYER_PRESENTATION_SCHEMA_VERSION,
} from './version-contract.js';

export { PROJECT_SCHEMA_VERSION };
const PROJECT_FORMATS = Object.freeze(new Set([
  'pandolab-project-state',
  'pandolab-autosave-full',
  'pandolab-autosave-delta',
]));

const UUID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i;
const text = value => String(value ?? '').trim();
const LAYER_VISIBILITY_KEYS = new Set(['countries', 'subunits', 'regions', 'distributions', 'rivers', 'lakes', 'genericFeatures', 'labels', 'basemapLabels', 'countryFlags', 'subunitLabels', 'subunitFlags', 'regionLabels', 'regionFlags']);
const ITEM_VISIBILITY_KEYS = new Set(['countries', 'subunits', 'regions', 'distributions', 'hydro', 'genericFeatures', 'labels', 'countryLabels']);
const PRESENTATION_GROUP_KEYS = new Set(['countries', 'subunits', 'regions', 'distributions', 'rivers', 'lakes', 'hydro', 'genericFeatures', 'labels', 'countryLabels', 'terrain']);
const GENERIC_PROPERTY_KEYS = new Set(['schemaVersion', 'name', 'notes', 'color', 'locked', 'source']);

export function createProjectObjectId() {
  if (typeof globalThis.crypto?.randomUUID !== 'function') throw new Error('이 환경에서는 안전한 프로젝트 ID를 만들 수 없습니다.');
  return globalThis.crypto.randomUUID();
}

function isProjectObjectId(value) {
  return UUID_PATTERN.test(text(value));
}

function schemaError(message, code = 'PL-SCHEMA-001') {
  const error = new Error(message);
  error.code = code;
  return error;
}

function requireSchemaVersion(value, label, expected = PROJECT_SCHEMA_VERSION) {
  if (value == null) throw schemaError(`${label}에 schemaVersion이 없습니다.`, 'PL-SCHEMA-MISSING');
  if (Number(value) !== expected) {
    throw schemaError(`${label}의 schemaVersion ${value}은 지원하지 않습니다. 현재 버전은 ${expected}입니다.`, 'PL-SCHEMA-OLD');
  }
}

function assertUniqueProjectIds(rows, label, idOf = row => row?.id) {
  const seen = new Set();
  for (const row of rows || []) {
    const id = text(idOf(row));
    if (!id) throw schemaError(`${label} ID가 비어 있습니다.`, 'PL-SCHEMA-ID-MISSING');
    if (!isProjectObjectId(id)) throw schemaError(`${label} ID가 UUID 형식이 아닙니다: ${id}`, 'PL-SCHEMA-ID-FORMAT');
    if (seen.has(id)) throw schemaError(`${label} ID가 중복되었습니다: ${id}`, 'PL-SCHEMA-ID-DUPLICATE');
    seen.add(id);
  }
}

function assertAllowedKeys(value, allowed, label) {
  for (const key of Object.keys(value || {})) {
    if (!allowed.has(key)) {
      throw schemaError(`${label}에 지원하지 않는 필드 ${key}가 있습니다.`, 'PL-SCHEMA-FIELD');
    }
  }
}

function prepareProjectForValidation(project) {
  if (!project || typeof project !== 'object') throw schemaError('프로젝트 형식이 올바르지 않습니다.');
  requireSchemaVersion(project.schemaVersion, '프로젝트');
  return project;
}

/** Prepare a detached candidate before the current editor/session is touched. */
export function prepareProjectForStorage(input, { baseEntities = null, baseDataset = null, baseDatasetFingerprint = null } = {}) {
  assertCurrentProjectSchema(input, { baseEntities, baseDataset, baseDatasetFingerprint });
  const project = structuredClone(input);
  if (project.format === 'pandolab-autosave-delta') {
    project.territorialEntities = restoreEntitiesFromDelta(project, { base: baseEntities, baseDataset, baseDatasetFingerprint });
    delete project.entityDelta;
    delete project.baseDatasetFingerprint;
  }
  project.format = 'pandolab-project-state';
  project.territorialEntities = normalizeTerritorialIdentities(project.territorialEntities);
  const storage = restoreTimelineStorage({ schemaVersion: 1, records: project.timelineRecords,
    geometries: project.geometries }, project.territorialEntities.map(entity => ({ id: entity.id, entityKind: entity.properties.entityKind })));
  assertProjectReferenceIntegrity({ ...project, storageOnly: true });
  project.timelineRecords = storage.records;
  project.geometries = storage.geometries.snapshot();
  return project;
}

export function prepareProjectForActivation(input, options = {}) {
  const project = prepareProjectForStorage(input, options);
  const storage = restoreTimelineStorage({ schemaVersion: 1, records: project.timelineRecords,
    geometries: project.geometries }, project.territorialEntities.map(entity => ({ id: entity.id, entityKind: entity.properties.entityKind })));
  const views = staticTimelineViews(project.territorialEntities, storage.records, storage.geometries);
  assertProjectReferenceIntegrity({ ...project, territorialEntities: views, timelineRecords: null });
  return project;
}

export function assertCurrentProjectSchema(input, { baseEntities = null, baseDataset = null, baseDatasetFingerprint = null } = {}) {
  const project = prepareProjectForValidation(input);
  if (!PROJECT_FORMATS.has(text(project.format))) throw schemaError(`지원하지 않는 프로젝트 형식입니다: ${text(project.format) || '(없음)'}`, 'PL-SCHEMA-FORMAT');
  requireSchemaVersion(project.schemaVersion, '프로젝트');
  requireSchemaVersion(project.landObjectModel?.schemaVersion, '지형지물 모델', LAND_OBJECT_SCHEMA_VERSION);
  requireSchemaVersion(project.territorialModel?.schemaVersion, '영토 모델', TERRITORIAL_MODEL_SCHEMA_VERSION);
  requireSchemaVersion(project.distributionModel?.schemaVersion, '분포 모델', DISTRIBUTION_MODEL_SCHEMA_VERSION);
  requireSchemaVersion(project.layerPresentation?.schemaVersion, '레이어 표현', LAYER_PRESENTATION_SCHEMA_VERSION);
  assertAllowedKeys(project.territorialModel, new Set(['schemaVersion', 'coastlineAuthority', 'storage', 'kinds', 'coverageModes']), '영토 모델');
  const kinds = project.territorialModel.kinds;
  const expectedKinds = Object.values(TERRITORIAL_ENTITY_KINDS);
  if (kinds != null && (!Array.isArray(kinds) || kinds.length !== expectedKinds.length
    || expectedKinds.some(kind => !kinds.includes(kind)))) throw schemaError('영토 모델 kinds가 현재 객체 종류와 일치하지 않습니다.');
  assertAllowedKeys(project, new Set([
    'format', 'schemaVersion', 'version', 'savedAt', 'territorialEntities', 'entityDelta', 'baseDatasetFingerprint',
    'sourceInfo', 'labels', 'genericFeatures', 'hydroEdits',
    'timelineRecords', 'geometries', 'distributionLayers', 'distributionEntries',
    'labelSettings', 'distributionSettings', 'layerPresentation', 'physicalSettings',
    'layerVisibility', 'itemVisibility', 'baseDataset', 'landObjectModel', 'territorialModel',
    'distributionModel', 'physicalSourceInfo',
  ]), '프로젝트');
  if (project.format !== 'pandolab-autosave-delta' && (Object.hasOwn(project, 'entityDelta') || Object.hasOwn(project, 'baseDatasetFingerprint'))) {
    throw schemaError('변경분과 기준 fingerprint는 delta 자동저장에만 허용됩니다.', 'PL-SCHEMA-FIELD');
  }
  if (project.format === 'pandolab-autosave-delta' && Object.hasOwn(project, 'territorialEntities')) {
    throw schemaError('delta 자동저장에는 entityDelta만 저장합니다.', 'PL-SCHEMA-FIELD');
  }
  assertAllowedKeys(project.landObjectModel, new Set([
    'schemaVersion', 'coastlineAuthority', 'purpose', 'directCreation',
    'sourceProvenanceSchemaVersion', 'canonicalProperties',
  ]), '지형지물 모델');
  if (project.landObjectModel?.purpose !== 'lossless-fallback' || project.landObjectModel?.directCreation !== false) {
    throw schemaError('Generic Feature는 손실 방지 fallback 전용이어야 합니다.', 'PL-SCHEMA-GENERIC-PURPOSE');
  }
  if (Number(project.landObjectModel?.sourceProvenanceSchemaVersion) !== SOURCE_PROVENANCE_SCHEMA_VERSION) {
    throw schemaError('Generic Feature source provenance 버전이 올바르지 않습니다.', 'PL-SCHEMA-SOURCE');
  }
  assertAllowedKeys(project.distributionSettings, new Set(['renderMode', 'activeLayerId', 'boundaryVisible']), '분포 표시 설정');
  if (!['overlap', 'single'].includes(project.distributionSettings?.renderMode)) {
    throw schemaError('분포 표시 방식이 올바르지 않습니다.', 'PL-SCHEMA-DISTRIBUTION-MODE');
  }
  assertAllowedKeys(project.layerVisibility, LAYER_VISIBILITY_KEYS, '레이어 표시 상태');
  assertAllowedKeys(project.itemVisibility, ITEM_VISIBILITY_KEYS, '객체 표시 상태');
  assertAllowedKeys(project.layerPresentation, new Set(['schemaVersion', 'overlayOrder', 'styles', 'objectStyles', 'objectOrder']), '레이어 표현');
  assertAllowedKeys(project.layerPresentation?.styles, PRESENTATION_GROUP_KEYS, '레이어 표현 스타일');
  for (const [group, style] of Object.entries(project.layerPresentation?.styles || {})) {
    assertAllowedKeys(style, new Set(['opacity', 'colorVisible', 'boundaryVisible', 'boundaryWidth', 'labelsVisible', 'blendMode']), `${group} 레이어 스타일`);
  }
  for (const [key, style] of Object.entries(project.layerPresentation?.objectStyles || {})) {
    if (!/^territorial:entity:.+/.test(key)) throw schemaError(`객체 표현 key가 올바르지 않습니다: ${key}`);
    assertAllowedKeys(style, new Set(['opacity', 'colorVisible', 'boundaryVisible', 'boundaryWidth', 'labelsVisible', 'blendMode']), `${key} 객체 스타일`);
  }
  if (project.layerPresentation?.objectOrder != null && (!Array.isArray(project.layerPresentation.objectOrder)
    || project.layerPresentation.objectOrder.some(key => typeof key !== 'string' || !/^territorial:entity:.+/.test(key)))) {
    throw schemaError('객체 표현 순서가 올바르지 않습니다.');
  }

  const entities = project.format === 'pandolab-autosave-delta' ? project.entityDelta?.changed : project.territorialEntities;
  if (!Array.isArray(entities)) throw schemaError('territorialEntities 또는 entityDelta.changed 배열이 필요합니다.');
  const entityIds = new Set();
  for (const feature of entities) {
    const id = text(feature?.id);
    if (typeof feature?.id !== 'string' || !id || entityIds.has(id)) throw schemaError('영역 ID가 비어 있거나 중복되었습니다: ' + id, 'PL-SCHEMA-ID-DUPLICATE');
    entityIds.add(id);
    requireSchemaVersion(feature.properties?.schemaVersion, '영역 ' + id, TERRITORIAL_SCHEMA_VERSION);
    assertAllowedKeys(feature.properties, new Set(TERRITORIAL_IDENTITY_FIELDS), '영역 ' + id);
  }
  let allEntities = normalizeTerritorialIdentities(entities);
  if (project.format === 'pandolab-autosave-delta') {
    assertAllowedKeys(project.entityDelta, new Set(['changed','removedIds']), '영역 변경분');
    const removed = project.entityDelta.removedIds;
    if (!Array.isArray(removed) || new Set(removed).size !== removed.length
      || removed.some(id => typeof id !== 'string' || !text(id) || entityIds.has(id))) throw schemaError('영역 삭제 ID가 올바르지 않습니다.');
    if (!Array.isArray(baseEntities) || baseDataset !== project.baseDataset)
      throw schemaError('자동저장 기준 데이터가 없거나 저장된 기준과 다릅니다.', 'PL-SCHEMA-BASE');
    allEntities = restoreEntitiesFromDelta(project, { base: baseEntities, baseDataset, baseDatasetFingerprint });
  }

  restoreTimelineStorage({ schemaVersion: 1, records: project.timelineRecords, geometries: project.geometries },
    allEntities.map(feature => ({ id: feature.id, entityKind: feature.properties.entityKind })));

  assertUniqueProjectIds(project.distributionLayers, '분포 레이어');
  assertUniqueProjectIds(project.distributionEntries, '분포 엔트리');
  assertUniqueProjectIds(project.genericFeatures, '기타 객체');
  assertUniqueProjectIds(project.hydroEdits, '편집 수계');
  assertUniqueProjectIds(project.labels, '지명');

  for (const layer of project.distributionLayers || []) {
    requireSchemaVersion(layer?.schemaVersion, `분포 레이어 ${text(layer?.id)}`, DISTRIBUTION_MODEL_SCHEMA_VERSION);
    assertAllowedKeys(layer, new Set(['id', 'schemaVersion', 'name', 'unit', 'valueScale', 'color', 'locked', 'parentId', 'groups', 'validFrom', 'validTo', 'metadata']), `분포 레이어 ${text(layer?.id)}`);
    if (typeof layer.unit !== 'string' || !['auto', 'manual'].includes(layer.valueScale?.mode)
      || (layer.valueScale.mode === 'manual' && (typeof layer.valueScale.min !== 'number'
        || typeof layer.valueScale.max !== 'number' || !Number.isFinite(layer.valueScale.min)
        || !Number.isFinite(layer.valueScale.max) || layer.valueScale.min >= layer.valueScale.max))) {
      throw schemaError(`분포 레이어 ${text(layer?.id)}의 단위 또는 색 농도 범위가 올바르지 않습니다.`, 'PL-SCHEMA-DISTRIBUTION-SCALE');
    }
  }
  for (const entry of project.distributionEntries || []) {
    requireSchemaVersion(entry?.schemaVersion, `분포 엔트리 ${text(entry?.id)}`, DISTRIBUTION_MODEL_SCHEMA_VERSION);
    assertAllowedKeys(entry, new Set(['id', 'schemaVersion', 'layerId', 'mode', 'territorialUnitId', 'geometry', 'value', 'certainty', 'validFrom', 'validTo', 'metadata']), `분포 엔트리 ${text(entry?.id)}`);
    if (typeof entry.value !== 'number' || !Number.isFinite(entry.value)) {
      throw schemaError(`분포 엔트리 ${text(entry?.id)}의 값이 올바르지 않습니다.`, 'PL-SCHEMA-DISTRIBUTION-VALUE');
    }
  }
  try {
    const layers = normalizeDistributionLayers(project.distributionLayers);
    const ids = new Set(layers.map(layer => layer.id));
    normalizeDistributionEntries(project.distributionEntries, { layerExists: id => ids.has(id), cloneGeometry: geometry => geometry });
  } catch (error) {
    throw schemaError(error.message, 'PL-SCHEMA-DISTRIBUTION');
  }
  const objectGeometries = createGeometryVersionStore();
  function validateObjectGeometry(feature, label) {
    if (feature?.type !== 'Feature') throw schemaError(`${label}의 Feature 형식이 올바르지 않습니다.`);
    assertAllowedKeys(feature, new Set(['type', 'id', 'properties', 'geometry']), label);
    try { objectGeometries.insert({ id: `${label}:${feature.id}`, version: 1 }, feature.geometry); }
    catch (error) { throw schemaError(`${label}의 형상이 올바르지 않습니다. ${error.message}`, 'PL-SCHEMA-GEOMETRY'); }
  }
  for (const feature of project.genericFeatures || []) {
    const label = `기타 객체 ${text(feature?.id)}`;
    validateObjectGeometry(feature, label);
    requireSchemaVersion(feature?.properties?.schemaVersion, label, LAND_OBJECT_SCHEMA_VERSION);
    assertAllowedKeys(feature?.properties, GENERIC_PROPERTY_KEYS, label);
    const sourceValidation = validateSourceProvenance(feature?.properties?.source);
    if (!sourceValidation.ok) throw schemaError(`${label}의 source provenance가 올바르지 않습니다. ${sourceValidation.issues[0]}`, 'PL-SCHEMA-SOURCE');
  }
  for (const feature of project.hydroEdits || []) {
    validateObjectGeometry(feature, `편집 수계 ${text(feature?.id)}`);
    const expectedCategory = ['Polygon', 'MultiPolygon'].includes(feature.geometry.type) ? 'lake'
      : ['LineString', 'MultiLineString'].includes(feature.geometry.type) ? 'river' : null;
    if (!expectedCategory || feature.properties?.category !== expectedCategory || Object.hasOwn(feature.properties, 'visible'))
      throw schemaError(`편집 수계 ${text(feature?.id)}의 종류 또는 필드가 올바르지 않습니다.`, 'PL-SCHEMA-FIELD');
    requireSchemaVersion(feature?.properties?.pandolab_schema_version, `편집 수계 ${text(feature?.id)}`, 1);
  }
  return project;
}

export const PROJECT_STATE_FIELDS = Object.freeze([
  Object.freeze({ name: 'sourceInfo', scope: 'document', fallback: () => null }),
  Object.freeze({ name: 'labels', scope: 'document', fallback: () => [] }),
  Object.freeze({ name: 'genericFeatures', scope: 'document', fallback: () => [] }),
  Object.freeze({ name: 'hydroEdits', scope: 'document', fallback: () => [] }),
  Object.freeze({ name: 'timelineRecords', scope: 'document', fallback: () => { throw schemaError('timelineRecords가 필요합니다.'); } }),
  Object.freeze({ name: 'geometries', scope: 'document', fallback: () => { throw schemaError('geometries가 필요합니다.'); } }),
  Object.freeze({ name: 'distributionLayers', scope: 'document', fallback: () => [] }),
  Object.freeze({ name: 'distributionEntries', scope: 'document', fallback: () => [] }),
  Object.freeze({ name: 'labelSettings', scope: 'presentation', fallback: () => ({}) }),
  Object.freeze({ name: 'distributionSettings', scope: 'presentation', fallback: current => current || { renderMode: 'overlap', activeLayerId: '', boundaryVisible: true } }),
  Object.freeze({ name: 'layerPresentation', scope: 'presentation', fallback: () => ({}) }),
  Object.freeze({ name: 'physicalSettings', scope: 'presentation', fallback: current => current || {} }),
  Object.freeze({ name: 'layerVisibility', scope: 'presentation', fallback: current => current || {} }),
  Object.freeze({ name: 'itemVisibility', scope: 'presentation', fallback: () => ({}) }),
  Object.freeze({ name: 'projection', scope: 'session', fallback: () => 'globe' }),
  Object.freeze({ name: 'layerFolders', scope: 'session', fallback: () => ({}) }),
  Object.freeze({ name: 'view', scope: 'session', fallback: current => current || {} }),
]);

const fieldsFor = scope => {
  if (scope === 'project') return PROJECT_STATE_FIELDS.filter(field => ['document', 'presentation'].includes(field.scope));
  if (scope === 'history' || scope === 'document') return PROJECT_STATE_FIELDS.filter(field => field.scope === 'document');
  if (scope === 'presentation') return PROJECT_STATE_FIELDS.filter(field => field.scope === 'presentation');
  if (scope === 'session') return PROJECT_STATE_FIELDS.filter(field => field.scope === 'session');
  throw new Error(`알 수 없는 프로젝트 상태 범위입니다: ${scope}`);
};

export function pickProjectFields(state, { scope = 'project', clone = structuredClone } = {}) {
  return Object.fromEntries(fieldsFor(scope).map(field => [field.name,
    clone(field.name === 'geometries' ? state.geometries.snapshot() : state[field.name])]));
}

export function applyProjectFields(target, source, {
  scope = 'project',
  clone = structuredClone,
  normalizers = {},
} = {}) {
  for (const field of fieldsFor(scope)) {
    const current = target[field.name];
    const raw = Object.prototype.hasOwnProperty.call(source || {}, field.name) && source[field.name] !== undefined
      ? source[field.name]
      : field.fallback(current);
    const normalize = normalizers[field.name];
    target[field.name] = normalize ? normalize(raw, current, source || {}) : clone(raw);
  }
  return target;
}
