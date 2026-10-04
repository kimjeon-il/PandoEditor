import { createDocumentMutationRunner } from './document-mutation-runner.js';
import {
  SOURCE_KINDS,
  normalizeSourceProvenance,
} from './source-provenance.js';
import { GENERIC_FEATURE_SCHEMA_VERSION } from './version-contract.js';

export { GENERIC_FEATURE_SCHEMA_VERSION };
const LEGACY_GENERIC_FEATURE_SCHEMA_VERSION = 1;

// Deprecated compatibility metadata. Generic Feature v2 has no territorial role semantics;
// these values only expose preserved v1 data to legacy callers until their UI paths are removed.
export const GENERIC_FEATURE_ROLE_RULES = Object.freeze({
  generic: Object.freeze({ role: 'generic', geometry: 'any', binding: 'none', label: '기타 객체' }),
  territory: Object.freeze({ role: 'territory', geometry: 'polygon', binding: 'none', label: '권역 객체' }),
  administrative: Object.freeze({ role: 'administrative', geometry: 'polygon', binding: 'none', label: '행정구역 객체' }),
});
export const GENERIC_FEATURE_ROLE_LABELS = Object.freeze({ generic: '기타 객체', territory: '권역 객체', administrative: '행정구역 객체' });
export const GENERIC_FEATURE_CANONICAL_PROPERTY_KEYS = Object.freeze([
  'schemaVersion', 'name', 'notes', 'color', 'locked', 'source',
]);

const CANONICAL_PROPERTY_KEYS = new Set(GENERIC_FEATURE_CANONICAL_PROPERTY_KEYS);
const LEGACY_SEMANTIC_KEYS = Object.freeze(['role', 'ownerId', 'parentId', 'landBinding', 'topologyGroup']);
const LEGACY_SEMANTIC_KEY_SET = new Set(LEGACY_SEMANTIC_KEYS);
const text = value => String(value ?? '').trim();
const plainObject = value => !!value && typeof value === 'object' && !Array.isArray(value);
const clone = value => value == null ? value : structuredClone(value);

export function genericFeatureGeometryKind(feature) {
  const type = feature?.geometry?.type || '';
  if (type === 'Polygon' || type === 'MultiPolygon') return 'polygon';
  if (type === 'LineString' || type === 'MultiLineString') return 'line';
  if (type === 'Point' || type === 'MultiPoint') return 'point';
  return 'unknown';
}

function legacyDetails(feature) {
  const details = feature?.properties?.source?.details;
  return plainObject(details?.legacyGenericSemantics) ? details.legacyGenericSemantics : {};
}

export function genericFeatureRole(feature) {
  const role = text(legacyDetails(feature).role);
  return GENERIC_FEATURE_ROLE_RULES[role] ? role : 'generic';
}

function legacySemanticsFrom(properties = {}) {
  const output = {};
  for (const key of LEGACY_SEMANTIC_KEYS) {
    if (properties[key] == null || properties[key] === '') continue;
    output[key] = clone(properties[key]);
  }
  return output;
}

function unmappedLegacyProperties(properties = {}) {
  const output = {};
  for (const [key, value] of Object.entries(properties || {})) {
    if (CANONICAL_PROPERTY_KEYS.has(key) || LEGACY_SEMANTIC_KEY_SET.has(key) || value === undefined) continue;
    output[key] = clone(value);
  }
  return output;
}

function normalizeGenericSource(properties = {}) {
  const inputVersion = Number(properties.schemaVersion || LEGACY_GENERIC_FEATURE_SCHEMA_VERSION);
  const legacySemantics = legacySemanticsFrom(properties);
  const legacyProperties = unmappedLegacyProperties(properties);
  const hasLegacyPayload = Object.keys(legacySemantics).length > 0 || Object.keys(legacyProperties).length > 0;
  const legacyInput = inputVersion <= LEGACY_GENERIC_FEATURE_SCHEMA_VERSION || hasLegacyPayload;
  const defaultKind = legacyInput ? SOURCE_KINDS.LEGACY : SOURCE_KINDS.UNSUPPORTED;
  const normalized = normalizeSourceProvenance(properties.source, {
    kind: defaultKind,
    sourceFormat: inputVersion <= LEGACY_GENERIC_FEATURE_SCHEMA_VERSION
      ? 'pandolab-generic-v1'
      : hasLegacyPayload ? 'pandolab-generic-compat' : '',
  });
  const details = { ...(normalized.details || {}) };
  if (Object.keys(legacySemantics).length) {
    details.legacyGenericSemantics = {
      ...(plainObject(details.legacyGenericSemantics) ? details.legacyGenericSemantics : {}),
      ...legacySemantics,
    };
  }
  if (Object.keys(legacyProperties).length) {
    details.legacyProperties = {
      ...(plainObject(details.legacyProperties) ? details.legacyProperties : {}),
      ...legacyProperties,
    };
  }
  return normalizeSourceProvenance({ ...normalized, details });
}

function canonicalProperties(properties = {}) {
  return {
    schemaVersion: GENERIC_FEATURE_SCHEMA_VERSION,
    name: text(properties.name),
    notes: text(properties.notes),
    color: text(properties.color) || '#8c68d8',
    locked: properties.locked === true,
    source: normalizeGenericSource(properties),
  };
}

export function normalizeGenericFeatureSemantics(feature) {
  if (!feature) return feature;
  feature.properties = canonicalProperties(feature.properties || {});
  return feature;
}

export function normalizeGenericFeatureCollection(genericFeatures, { cloneFeature = clone } = {}) {
  const output = [];
  const seen = new Set();
  for (const raw of Array.isArray(genericFeatures) ? genericFeatures : []) {
    const feature = normalizeGenericFeatureSemantics(cloneFeature(raw));
    const id = text(feature?.id);
    if (!id) throw new Error('기타 객체 ID가 비어 있습니다.');
    if (seen.has(id)) throw new Error(`기타 객체 ID가 중복되었습니다: ${id}`);
    if (!['Point', 'MultiPoint', 'LineString', 'MultiLineString', 'Polygon', 'MultiPolygon'].includes(feature?.geometry?.type)
      || !Array.isArray(feature.geometry.coordinates) || !feature.geometry.coordinates.length) {
      throw new Error(`${id}의 기타 객체 geometry가 비어 있거나 지원되지 않습니다.`);
    }
    seen.add(id);
    output.push(feature);
  }
  return output;
}

function applyGenericMetadata(feature, field, value, writeColor) {
  if (field === 'color') writeColor(feature, value);
  else if (field === 'source') feature.properties.source = normalizeSourceProvenance(value, { kind: SOURCE_KINDS.UNSUPPORTED });
  else if (['name', 'notes', 'locked'].includes(field)) feature.properties[field] = field === 'locked' ? value === true : text(value);
  else throw new Error(`기타 객체에 지원하지 않는 필드 ${field}가 있습니다.`);
}

export function createGenericFeatureService({ documentStore, commandPipeline, writeColor }) {
  const mutateDocument = createDocumentMutationRunner({ commandPipeline });
  const genericFeatures = () => documentStore.readFeatures();
  const get = id => genericFeatures().find(feature => text(feature.id) === text(id)) || null;

  function add(feature) {
    const normalized = normalizeGenericFeatureSemantics(clone(feature));
    if (get(normalized.id)) throw new Error(`기타 객체 ID가 중복되었습니다: ${normalized.id}`);
    mutateDocument({ type: 'generic-feature-create', affectedIds: [text(normalized.id)] }, () => {
      documentStore.replaceFeatures(normalizeGenericFeatureCollection([...genericFeatures(), normalized]));
    }, { renderDirty: { domain: 'generic', change: 'geometry' } });
    return get(normalized.id);
  }

  function addMany(features) {
    const normalized = normalizeGenericFeatureCollection(features);
    mutateDocument({ type: 'generic-feature-import', affectedIds: normalized.map(feature => text(feature.id)) }, () => {
      documentStore.replaceFeatures(normalizeGenericFeatureCollection([...genericFeatures(), ...normalized]));
    }, { renderDirty: { domain: 'generic', change: 'structure' } });
    return normalized.map(feature => get(feature.id));
  }

  function updateMetadata(id, field, value) {
    const current = get(id);
    if (!current) return { ok: false, code: 'not-found' };
    const candidate = clone(current);
    applyGenericMetadata(candidate, field, value, writeColor);
    if (JSON.stringify(candidate.properties) === JSON.stringify(current.properties)) {
      return { ok: true, changed: false, feature: current };
    }
    mutateDocument({ type: 'generic-feature-metadata', affectedIds: [text(current.id)] }, () => {
      applyGenericMetadata(current, field, value, writeColor);
    }, { renderDirty: { domain: 'generic', change: 'metadata' } });
    return { ok: true, changed: true, feature: current };
  }

  function remove(id, { beforeRemove = () => {} } = {}) {
    const feature = get(id);
    if (!feature) return { ok: false, code: 'not-found' };
    mutateDocument({ type: 'generic-feature-delete', affectedIds: [text(feature.id)] }, () => {
      beforeRemove(feature);
      documentStore.replaceFeatures(genericFeatures().filter(candidate => text(candidate.id) !== text(feature.id)));
    }, { renderDirty: { domain: 'generic', change: 'structure' } });
    return { ok: true, feature };
  }

  return Object.freeze({ get, list: () => genericFeatures(), add, addMany, updateMetadata, remove });
}
