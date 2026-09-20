import { normalizeTemporalInterval } from './temporal.js';

export const DISTRIBUTION_SCHEMA_VERSION = 2;

export const DISTRIBUTION_TYPES = Object.freeze({
  LANGUAGE: 'language',
  ETHNICITY: 'ethnicity',
  RELIGION: 'religion',
});

export const DISTRIBUTION_MODES = Object.freeze({
  TERRITORIAL: 'territorial',
  GEOMETRY: 'geometry',
});

export const DISTRIBUTION_RENDER_MODES = Object.freeze({
  DOMINANT: 'dominant',
  INTENSITY: 'intensity',
});

const TYPES = new Set(Object.values(DISTRIBUTION_TYPES));
const MODES = new Set(Object.values(DISTRIBUTION_MODES));
const POLYGON_TYPES = new Set(['Polygon', 'MultiPolygon']);
const LAYER_KEYS = new Set(['id', 'schemaVersion', 'type', 'name', 'color', 'locked', 'parentId', 'groups', 'validFrom', 'validTo', 'metadata']);
const ENTRY_KEYS = new Set(['id', 'schemaVersion', 'layerId', 'mode', 'territorialUnitId', 'geometry', 'share', 'certainty', 'validFrom', 'validTo', 'metadata']);
const text = value => String(value ?? '').trim();
const clone = value => structuredClone(value);
function assertAllowedKeys(value, allowed, label) {
  const unsupported = Object.keys(value || {}).find(key => !allowed.has(key));
  if (unsupported) throw new Error(`${label}에 지원하지 않는 필드 ${unsupported}가 있습니다.`);
}
function shareValue(value) {
  const share = Number(value);
  if (!Number.isFinite(share)) throw new Error('분포 비율은 유한한 숫자여야 합니다.');
  if (share < 0 || share > 100) throw new Error('분포 비율은 0~100 범위여야 합니다.');
  return share;
}

function normalizeDistributionLayer(raw) {
  if (Number(raw?.schemaVersion) !== DISTRIBUTION_SCHEMA_VERSION) throw new Error('분포 레이어 schemaVersion이 현재 형식과 일치하지 않습니다.');
  assertAllowedKeys(raw, LAYER_KEYS, '분포 레이어');
  const type = text(raw?.type).toLowerCase();
  if (!TYPES.has(type)) return null;
  const id = text(raw?.id);
  if (!id) throw new Error('분포 레이어 ID가 비어 있습니다.');
  const interval = normalizeTemporalInterval(raw.validFrom, raw.validTo);
  return {
    id,
    schemaVersion: DISTRIBUTION_SCHEMA_VERSION,
    type,
    name: text(raw.name) || id,
    color: text(raw.color) || '#8c68d8',
    locked: raw.locked === true,
    parentId: text(raw.parentId),
    groups: Array.isArray(raw.groups) ? [...new Set(raw.groups.map(text).filter(Boolean))] : [],
    validFrom: interval.validFrom,
    validTo: interval.validTo,
    metadata: raw.metadata && typeof raw.metadata === 'object' ? clone(raw.metadata) : {},
  };
}

export function normalizeDistributionLayers(value) {
  const output = [];
  const seen = new Set();
  for (const raw of Array.isArray(value) ? value : []) {
    const layer = normalizeDistributionLayer(raw);
    if (!layer) throw new Error('분포 레이어 형식이 올바르지 않습니다.');
    if (seen.has(layer.id)) throw new Error(`분포 레이어 ID가 중복되었습니다: ${layer.id}`);
    seen.add(layer.id);
    output.push(layer);
  }
  const byId = new Map(output.map(layer => [layer.id, layer]));
  for (const layer of output) {
    const parent = byId.get(layer.parentId);
    if (layer.parentId && (!parent || parent.type !== layer.type || parent.id === layer.id)) {
      throw new Error(`${layer.id}의 상위 분포 레이어가 존재하지 않거나 유형이 다릅니다.`);
    }
    let cursor = parent;
    const visited = new Set([layer.id]);
    while (cursor) {
      if (visited.has(cursor.id)) {
        throw new Error(`${layer.id}의 상위 분포 레이어 관계가 순환합니다.`);
      }
      visited.add(cursor.id);
      cursor = byId.get(cursor.parentId);
    }
  }
  return output;
}

function normalizeDistributionEntry(raw, { cloneGeometry = clone } = {}) {
  if (Number(raw?.schemaVersion) !== DISTRIBUTION_SCHEMA_VERSION) throw new Error('분포 엔트리 schemaVersion이 현재 형식과 일치하지 않습니다.');
  assertAllowedKeys(raw, ENTRY_KEYS, '분포 엔트리');
  const layerId = text(raw?.layerId);
  if (!layerId) throw new Error('분포 엔트리의 레이어 ID가 비어 있습니다.');
  const mode = text(raw?.mode);
  if (!MODES.has(mode)) return null;
  const territorialUnitId = mode === DISTRIBUTION_MODES.TERRITORIAL ? text(raw.territorialUnitId) : '';
  const geometry = mode === DISTRIBUTION_MODES.GEOMETRY && POLYGON_TYPES.has(raw?.geometry?.type)
    ? cloneGeometry(raw.geometry)
    : null;
  if ((mode === DISTRIBUTION_MODES.TERRITORIAL && !territorialUnitId)
    || (mode === DISTRIBUTION_MODES.GEOMETRY && (!geometry || !Array.isArray(geometry.coordinates) || !geometry.coordinates.length))) return null;
  const id = text(raw.id);
  if (!id) throw new Error('분포 엔트리 ID가 비어 있습니다.');
  const interval = normalizeTemporalInterval(raw.validFrom, raw.validTo);
  return {
    id,
    schemaVersion: DISTRIBUTION_SCHEMA_VERSION,
    layerId,
    mode,
    territorialUnitId,
    geometry,
    share: shareValue(raw.share ?? 100),
    certainty: text(raw.certainty) || 'unknown',
    validFrom: interval.validFrom,
    validTo: interval.validTo,
    metadata: raw.metadata && typeof raw.metadata === 'object' ? clone(raw.metadata) : {},
  };
}

export function normalizeDistributionEntries(value, { layerExists = () => true, cloneGeometry = clone } = {}) {
  const output = [];
  const seen = new Set();
  for (const raw of Array.isArray(value) ? value : []) {
    const entry = normalizeDistributionEntry(raw, { cloneGeometry });
    if (!entry) throw new Error('분포 엔트리 형식이 올바르지 않습니다.');
    if (seen.has(entry.id)) throw new Error(`분포 엔트리 ID가 중복되었습니다: ${entry.id}`);
    if (!layerExists(entry.layerId)) throw new Error(`${entry.id}의 분포 레이어가 존재하지 않습니다.`);
    seen.add(entry.id);
    output.push(entry);
  }
  return output;
}

export function createDistributionLayer(options) {
  const layer = normalizeDistributionLayer({ ...options, schemaVersion: DISTRIBUTION_SCHEMA_VERSION });
  if (!layer) throw new Error('분포 레이어 형식이 올바르지 않습니다.');
  return layer;
}

export function createDistributionEntry(options) {
  const entry = normalizeDistributionEntry({ ...options, schemaVersion: DISTRIBUTION_SCHEMA_VERSION });
  if (!entry) throw new Error('분포 엔트리 형식이 올바르지 않습니다.');
  return entry;
}

export function distributionEntriesForLayer(entries, layerId) {
  const key = text(layerId);
  return (entries || []).filter(entry => text(entry.layerId) === key);
}

export function validateDistributionModel(layers, entries, { territorialExists = () => true } = {}) {
  const issues = [];
  const layerIds = new Set((layers || []).map(layer => text(layer.id)));
  for (const entry of entries || []) {
    if (!layerIds.has(text(entry.layerId))) issues.push(`${entry.id}의 분포 항목이 존재하지 않습니다.`);
    if (entry.mode === DISTRIBUTION_MODES.TERRITORIAL && !territorialExists(entry.territorialUnitId)) issues.push(`${entry.id}의 참조 영역이 존재하지 않습니다.`);
    if (entry.share < 0 || entry.share > 100) issues.push(`${entry.id}의 비율이 0~100 범위를 벗어났습니다.`);
  }
  return { ok: issues.length === 0, issues };
}

export function dominantDistributionEntries(layers, entries) {
  const visible = new Set((layers || []).map(layer => layer.id));
  const byTerritorialUnit = new Map();
  const geometryEntries = [];
  for (const entry of entries || []) {
    if (!visible.has(entry.layerId)) continue;
    if (entry.mode === DISTRIBUTION_MODES.GEOMETRY) {
      geometryEntries.push(entry);
      continue;
    }
    const current = byTerritorialUnit.get(entry.territorialUnitId);
    if (!current || entry.share > current.share) byTerritorialUnit.set(entry.territorialUnitId, entry);
  }
  return [...byTerritorialUnit.values(), ...geometryEntries];
}
