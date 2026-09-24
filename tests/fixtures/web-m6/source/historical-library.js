import {
  normalizeTemporal,
  normalizeTemporalInterval,
  parseTemporal,
  temporalContains,
} from './temporal.js';
import { currentCountryFlagUrl } from './country-flags.js';

export const HISTORICAL_LIBRARY_SCHEMA_VERSION = 2;

export const LIBRARY_ENTITY_TYPES = Object.freeze({
  COUNTRY: 'country',
  SUBUNIT: 'subunit',
  REGION: 'region',
});

const TYPES = new Set(Object.values(LIBRARY_ENTITY_TYPES));
const POLYGON_TYPES = new Set(['Polygon', 'MultiPolygon']);
const INSTANTIATION_MODES = new Set(['independent', 'territory-replacement']);
const text = value => String(value ?? '').trim();
const clone = value => structuredClone(value);
function dateContains(version, referenceDate) {
  const point = parseTemporal(referenceDate);
  return !point || temporalContains(version, point);
}

const startYear = value => parseTemporal(value)?.year ?? null;

function normalizeInstantiation(raw) {
  const requestedMode = text(raw?.mode) || 'independent';
  const mode = requestedMode === 'country-territory-priority'
    ? 'territory-replacement'
    : requestedMode;
  if (!INSTANTIATION_MODES.has(mode)) throw new Error(`지원하지 않는 라이브러리 추가 방식입니다: ${mode}`);
  const countryUpdates = {};
  for (const [countryId, update] of Object.entries(raw?.countryUpdates || {})) {
    const id = text(countryId);
    if (!id || !update || typeof update !== 'object') continue;
    const normalized = {};
    if (text(update.name)) normalized.name = text(update.name);
    if (Object.keys(normalized).length) countryUpdates[id] = normalized;
  }
  return { mode, countryUpdates };
}

function normalizeGeometryVersion(raw) {
  const geometry = POLYGON_TYPES.has(raw?.geometry?.type)
    && Array.isArray(raw.geometry.coordinates)
    && raw.geometry.coordinates.length
    ? clone(raw.geometry)
    : null;
  if (!geometry) return null;
  const id = text(raw.id);
  if (!id) return null;
  const interval = normalizeTemporalInterval(raw.validFrom, raw.validTo);
  return {
    id,
    validFrom: interval.validFrom,
    validTo: interval.validTo,
    geometry,
    datePrecision: text(raw.datePrecision) || 'unknown',
    certainty: text(raw.certainty) || 'unknown',
    sourceId: text(raw.sourceId),
    notes: text(raw.notes),
  };
}

export function normalizeHistoricalLibraryEntity(raw) {
  const inputType = text(raw?.type).toLowerCase();
  const type = ['territory', 'admin'].includes(inputType) ? 'subunit' : inputType;
  const libraryId = text(raw?.libraryId);
  if (!libraryId || !TYPES.has(type)) return null;
  const interval = normalizeTemporalInterval(raw.startDate, raw.endDate);
  const geometryVersions = [];
  const versionIds = new Set();
  for (const rawVersion of raw.geometryVersions || []) {
    const version = normalizeGeometryVersion(rawVersion);
    if (!version) throw new Error(`${libraryId}의 경계 버전 형식이 올바르지 않습니다.`);
    if (versionIds.has(version.id)) throw new Error(`${libraryId}의 경계 버전 ID가 중복되었습니다: ${version.id}`);
    versionIds.add(version.id);
    geometryVersions.push(version);
  }
  return {
    libraryId,
    schemaVersion: HISTORICAL_LIBRARY_SCHEMA_VERSION,
    type,
    canonicalName: text(raw.canonicalName) || libraryId,
    displayNames: raw.displayNames && typeof raw.displayNames === 'object' ? clone(raw.displayNames) : {},
    alternateNames: [...new Set((raw.alternateNames || []).map(text).filter(Boolean))],
    startDate: interval.validFrom,
    endDate: interval.validTo,
    parentLibraryId: text(raw.parentLibraryId),
    sovereignLibraryId: text(raw.sovereignLibraryId),
    geometryVersions,
    instantiation: normalizeInstantiation(raw.instantiation),
    metadata: raw.metadata && typeof raw.metadata === 'object' ? clone(raw.metadata) : {},
    sourceInfo: raw.sourceInfo && typeof raw.sourceInfo === 'object' ? clone(raw.sourceInfo) : {},
  };
}

export function selectGeometryVersion(entity, referenceDate = null) {
  const versions = entity?.geometryVersions || [];
  if (!versions.length) return null;
  const matching = versions.filter(version => dateContains(version, referenceDate));
  if (matching.length) return matching.sort((left, right) => (startYear(right.validFrom) ?? -Infinity) - (startYear(left.validFrom) ?? -Infinity))[0];
  const referenceYear = startYear(referenceDate);
  if (referenceYear == null) return versions[versions.length - 1];
  return [...versions].sort((left, right) => {
    const leftYear = startYear(left.validFrom) ?? startYear(left.validTo) ?? referenceYear;
    const rightYear = startYear(right.validFrom) ?? startYear(right.validTo) ?? referenceYear;
    return Math.abs(leftYear - referenceYear) - Math.abs(rightYear - referenceYear);
  })[0];
}

export function createCurrentCountryLibraryEntities(countriesData, { displayName = feature => feature?.properties?.name } = {}) {
  return (countriesData?.features || []).map(feature => {
    const id = text(feature?.id);
    if (!id || !POLYGON_TYPES.has(feature?.geometry?.type)) return null;
    const canonicalName = text(displayName(feature)) || id;
    const defaultFlagDataUrl = currentCountryFlagUrl(id);
    return normalizeHistoricalLibraryEntity({
      libraryId: `current-country:${id}`,
      type: LIBRARY_ENTITY_TYPES.COUNTRY,
      canonicalName,
      displayNames: { ko: canonicalName },
      alternateNames: [],
      startDate: null,
      endDate: null,
      geometryVersions: [{
        id: `current-country:${id}:natural-earth-5.1.1`,
        geometry: feature.geometry,
        datePrecision: 'current',
        certainty: 'high',
        sourceId: 'natural-earth-5.1.1',
      }],
      metadata: {
        currentCountryId: id,
        ...(defaultFlagDataUrl ? { defaultFlagDataUrl } : {}),
      },
      sourceInfo: { title: 'Natural Earth 5.1.1 Admin 0 Countries', license: 'Public domain' },
    });
  }).filter(Boolean);
}

export function materializePilotEntities(definitions, countriesData, combineGeometries, subtractGeometries = null) {
  const countryGeometry = new Map((countriesData?.features || []).map(feature => [
    text(feature?.id),
    feature.geometry,
  ]));
  return (definitions || []).map(definition => {
    const versions = (definition.geometryVersions || []).map(version => {
      let geometry = POLYGON_TYPES.has(version?.geometry?.type)
        && Array.isArray(version.geometry.coordinates)
        && version.geometry.coordinates.length
        ? clone(version.geometry)
        : null;
      if (!geometry) {
        const memberGeometries = (version.memberCountryIds || []).map(id => countryGeometry.get(text(id))).filter(Boolean);
        if (!memberGeometries.length) return null;
        geometry = memberGeometries.length === 1
          ? clone(memberGeometries[0])
          : (typeof combineGeometries === 'function' ? combineGeometries(memberGeometries) : memberGeometries[0]);
      }
      if (POLYGON_TYPES.has(version?.includeGeometry?.type) && Array.isArray(version.includeGeometry.coordinates)) {
        geometry = typeof combineGeometries === 'function'
          ? combineGeometries([geometry, clone(version.includeGeometry)])
          : geometry;
      }
      if (POLYGON_TYPES.has(version?.excludeGeometry?.type) && Array.isArray(version.excludeGeometry.coordinates)) {
        if (typeof subtractGeometries !== 'function') throw new Error(`${definition.libraryId}의 제외 경계를 처리할 수 없습니다.`);
        geometry = subtractGeometries(geometry, clone(version.excludeGeometry));
      }
      return geometry ? { ...version, geometry } : null;
    }).filter(Boolean);
    return normalizeHistoricalLibraryEntity({ ...definition, geometryVersions: versions });
  }).filter(entity => entity?.geometryVersions?.length);
}

function normalizeWorldSnapshot(raw) {
  const id = text(raw?.id);
  if (!id) return null;
  return {
    id,
    name: text(raw.name) || id,
    referenceDate: normalizeTemporal(raw.referenceDate),
    entityRefs: [...new Set((raw.entityRefs || []).map(text).filter(Boolean))],
    metadata: raw.metadata && typeof raw.metadata === 'object' ? clone(raw.metadata) : {},
    sourceInfo: raw.sourceInfo && typeof raw.sourceInfo === 'object' ? clone(raw.sourceInfo) : {},
  };
}

export function createHistoricalLibrary({ schemaVersion, entities = [], snapshots = [] } = {}) {
  if (Number(schemaVersion) !== HISTORICAL_LIBRARY_SCHEMA_VERSION) throw new Error('역사 라이브러리 schemaVersion이 현재 형식과 일치하지 않습니다.');
  const entityMap = new Map();
  for (const raw of entities) {
    const entity = normalizeHistoricalLibraryEntity(raw);
    if (!entity) throw new Error('역사 라이브러리 객체 형식이 올바르지 않습니다.');
    if (entityMap.has(entity.libraryId)) throw new Error(`역사 라이브러리 ID가 중복되었습니다: ${entity.libraryId}`);
    entityMap.set(entity.libraryId, entity);
  }
  const snapshotMap = new Map();
  for (const raw of snapshots || []) {
    const snapshot = normalizeWorldSnapshot(raw);
    if (!snapshot) throw new Error('세계 스냅샷 ID가 비어 있습니다.');
    if (snapshotMap.has(snapshot.id)) throw new Error(`세계 스냅샷 ID가 중복되었습니다: ${snapshot.id}`);
    snapshotMap.set(snapshot.id, snapshot);
  }
  return Object.freeze({
    get: id => entityMap.get(text(id)) || null,
    list: () => [...entityMap.values()],
    snapshots: () => [...snapshotMap.values()],
    getSnapshot: id => snapshotMap.get(text(id)) || null,
    search({ query = '', type = '', status = 'all', referenceDate = '', geographicRegion = '' } = {}) {
      const needle = text(query).toLocaleLowerCase('ko');
      const referencePoint = parseTemporal(referenceDate);
      return [...entityMap.values()].filter(entity => {
        if (type && entity.type !== type) return false;
        if (status === 'current' && entity.endDate) return false;
        if (status === 'past' && !entity.endDate) return false;
        if (geographicRegion && text(entity.metadata?.geographicRegion) !== text(geographicRegion)) return false;
        if (referencePoint && !temporalContains({ validFrom: entity.startDate, validTo: entity.endDate }, referencePoint)) return false;
        if (!needle) return true;
        const names = [entity.canonicalName, ...Object.values(entity.displayNames || {}), ...(entity.alternateNames || [])];
        return names.some(name => text(name).toLocaleLowerCase('ko').includes(needle));
      });
    },
  });
}

export function instantiateLibraryEntity(entity, referenceDate = null, geometryVersionId = '') {
  const requestedVersionId = text(geometryVersionId);
  const version = requestedVersionId
    ? (entity?.geometryVersions || []).find(candidate => candidate.id === requestedVersionId)
    : selectGeometryVersion(entity, referenceDate);
  if (!entity || !version) throw new Error('선택한 시점에 사용할 경계 버전이 없습니다.');
  return {
    libraryId: entity.libraryId,
    geometryVersionId: version.id,
    type: entity.type,
    name: entity.displayNames?.ko || entity.canonicalName,
    parentLibraryId: entity.parentLibraryId,
    sovereignLibraryId: entity.sovereignLibraryId,
    geometry: clone(version.geometry),
    validFrom: entity.startDate || version.validFrom,
    validTo: entity.endDate || version.validTo,
    metadata: {
      ...clone(entity.metadata || {}),
      librarySourceInfo: clone(entity.sourceInfo || {}),
      geometryCertainty: version.certainty,
      geometryDatePrecision: version.datePrecision,
    },
    instantiation: clone(entity.instantiation || { mode: 'independent', countryUpdates: {} }),
  };
}
