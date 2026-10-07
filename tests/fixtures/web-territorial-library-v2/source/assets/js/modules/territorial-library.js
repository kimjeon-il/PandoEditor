import { compareTemporal, normalizeTemporalInterval, parseTemporal, temporalIntervalsOverlap } from './temporal.js';
import {createGeometryVersionStore} from './geometry-version-store.js';

export const TERRITORIAL_LIBRARY_SCHEMA_VERSION = 2;
export const LIBRARY_ENTITY_TYPES = Object.freeze({ GENERAL: 'general', REGIONAL: 'regional' });
const text = value => String(value ?? '').trim();

function freeze(value) {
  if (value && typeof value === 'object') {
    for (const child of Object.values(value)) freeze(child);
    Object.freeze(value);
  }
  return value;
}

function interval(raw) {
  if (!raw || !Object.hasOwn(raw, 'validFrom') || !Object.hasOwn(raw, 'validTo')) throw new Error('Territorial interval requires both endpoints');
  for(const value of [raw.validFrom,raw.validTo]) if(value!==null) {
    if(typeof value!=='string')throw new Error('Territorial endpoints must be strings or null');
    parseTemporal(value,{nullable:false});
  }
  const { validFrom, validTo } = normalizeTemporalInterval(raw.validFrom, raw.validTo);
  return { validFrom, validTo };
}

function names(raw) {
  if (!raw || typeof raw !== 'object' || Array.isArray(raw) || !Object.keys(raw).length
    || Object.values(raw).some(value => typeof value !== 'string' || !value.trim())) throw new Error('Invalid territorial names');
  return structuredClone(raw);
}

function relations(raw) {
  if (!Array.isArray(raw)) throw new Error('Missing lineage relations');
  const seen = new Set();
  for (const relation of raw) {
    const key = JSON.stringify([relation.type, relation.from, relation.to]);
    if (relation.type !== 'successor' || !relation.from || !relation.to || relation.from === relation.to || seen.has(key)) throw new Error('Invalid lineage relation');
    seen.add(key);
  }
  return structuredClone(raw);
}

export function normalizeTerritorialLineage(raw) {
  if (raw?.schemaVersion !== 1 || !/^[a-z0-9][a-z0-9_-]*$/.test(raw.lineageId) || !Array.isArray(raw.entities) || !raw.entities.length) throw new Error('Invalid lineage schema/identity');
  const entities = raw.entities.map(normalizeTerritorialLibraryEntity);
  if (new Set(entities.map(e => e.entityId)).size !== entities.length) throw new Error('Duplicate lineage entity');
  return freeze({schemaVersion: 1, lineageId: raw.lineageId, names: names(raw.names), entities, relations: relations(raw.relations)});
}

export function normalizeTerritorialLibraryIndex(raw) {
  if(raw?.schemaVersion!==2 || !Array.isArray(raw.entities) || !Array.isArray(raw.lineages) || !Array.isArray(raw.snapshots))throw new Error('Invalid territorial index schema');
  const ids=new Set();
  for(const entity of raw.entities){
    if(entity.schemaVersion!==2 || !/^[a-z]+:[A-Za-z0-9_-]+$/.test(entity.entityId) || ids.has(entity.entityId) || !Object.values(LIBRARY_ENTITY_TYPES).includes(entity.entityKind)
      || entity.file!==`${entity.entityId.replace(':','-')}.json.gz` || !/^[a-f0-9]{64}$/.test(entity.sha256) || !(entity.compressedBytes>0 && entity.decodedBytes>0)
      || !entity.geometryVersions?.length || entity.geometryVersionCount!==entity.geometryVersions.length || !Array.isArray(entity.bbox) || entity.bbox.length!==4 || entity.bbox.some(v=>!Number.isFinite(v)))throw new Error('Invalid territorial index entry');
    names(entity.names);
    if (['canonicalName','displayNames','libraryId'].some(key=>Object.hasOwn(entity,key))) throw new Error('Retired index names/identity');
    ids.add(entity.entityId);interval(entity.lifetime);
    if(entity.validFrom!==entity.lifetime.validFrom || entity.validTo!==entity.lifetime.validTo)throw new Error('Inconsistent indexed lifetime');
    const versionIds = new Set();
    for(const v of entity.geometryVersions){if(Object.hasOwn(v,'geometry') || Object.hasOwn(v,'id') || !v.versionId || versionIds.has(v.versionId))throw new Error('Invalid indexed geometry version');versionIds.add(v.versionId);interval(v);}
    for(let i=0;i<entity.geometryVersions.length;i++)for(let j=i+1;j<entity.geometryVersions.length;j++)if(temporalIntervalsOverlap(entity.geometryVersions[i],entity.geometryVersions[j]))throw new Error('Overlapping indexed geometry versions');
  }
  const byId=new Map(raw.entities.map(e=>[e.entityId,e]));
  const lineageIds = new Set(), membership = new Set();
  for (const lineage of raw.lineages) {
    if (!/^[a-z0-9][a-z0-9_-]*$/.test(lineage.lineageId) || lineageIds.has(lineage.lineageId) || !Array.isArray(lineage.entityRefs) || !lineage.entityRefs.length) throw new Error('Invalid indexed lineage');
    lineageIds.add(lineage.lineageId); names(lineage.names); relations(lineage.relations);
    for (const id of lineage.entityRefs) {
      if (!ids.has(id) || membership.has(id) || byId.get(id).lineageId !== lineage.lineageId) throw new Error('Invalid lineage membership');
      membership.add(id);
    }
    for (const relation of lineage.relations) if (!ids.has(relation.from) || !ids.has(relation.to)) throw new Error('Missing lineage relation endpoint');
  }
  if (membership.size !== ids.size) throw new Error('Missing lineage membership');
  for(const entity of raw.entities){let parent=entity.parentEntityId;const seen=new Set([entity.entityId]);while(parent){if(!ids.has(parent)||seen.has(parent))throw new Error('Invalid catalog parent');seen.add(parent);parent=byId.get(parent).parentEntityId;}}
  const snapshots=new Set();
  for(const s of raw.snapshots){parseTemporal(s.referenceDate,{nullable:false});if(s.schemaVersion!==1 || !s.id || snapshots.has(s.id) || !Array.isArray(s.entityRefs) || new Set(s.entityRefs).size!==s.entityRefs.length || s.entityRefs.some(id=>!ids.has(id)))throw new Error('Invalid territorial snapshot');snapshots.add(s.id);}
  return freeze(structuredClone(raw));
}

export function normalizeTerritorialLibraryEntity(raw) {
  if (raw?.schemaVersion !== TERRITORIAL_LIBRARY_SCHEMA_VERSION) throw new Error('Territorial entity schemaVersion mismatch');
  const entityId = text(raw.entityId);
  if (!/^[a-z]+:[A-Za-z0-9_-]+$/.test(entityId) || !Object.values(LIBRARY_ENTITY_TYPES).includes(raw.entityKind)) throw new Error('Invalid territorial entity identity/kind');
  if (['isHistorical', 'isCurrent', 'libraryId', 'startDate', 'endDate', 'canonicalName', 'displayNames'].some(key => Object.hasOwn(raw, key))) throw new Error('Retired territorial entity fields');
  const lifetime = interval(raw.lifetime);
  if (!Array.isArray(raw.geometryVersions) || !raw.geometryVersions.length) throw new Error(`${entityId}: missing geometry versions`);
  const ids = new Set();
  const geometryVersions = raw.geometryVersions.map(version => {
    if (Object.hasOwn(version, 'id')) throw new Error('Retired geometry version ID');
    const id = text(version.versionId);
    if (!id || ids.has(id)) throw new Error(`${entityId}: duplicate/empty geometry version ID`);
    ids.add(id);
    if (!['Polygon', 'MultiPolygon'].includes(version.geometry?.type) || !Array.isArray(version.geometry.coordinates) || !version.geometry.coordinates.length) throw new Error(`${entityId}: expected Polygon/MultiPolygon`);
    // Use the existing archive's structural validator. This isolated registry
    // validates a snapshot; its reference never becomes a project GeometryRef.
    const validated=createGeometryVersionStore([{id,version:1,geojson:version.geometry}]);
    return { ...structuredClone(version), geometry:validated.get({id,version:1}), versionId:id, ...interval(version) };
  });
  for (let i = 0; i < geometryVersions.length; i++) {
    for (let j = i + 1; j < geometryVersions.length; j++) {
      if (temporalIntervalsOverlap(geometryVersions[i], geometryVersions[j])) throw new Error(`${entityId}: overlapping geometry versions`);
    }
  }
  const instantiation = structuredClone(raw.instantiation || { mode: 'independent', countryUpdates: {} });
  if (!['independent', 'territory-replacement'].includes(instantiation.mode)) throw new Error('Invalid territorial instantiation mode');
  return freeze({
    schemaVersion: TERRITORIAL_LIBRARY_SCHEMA_VERSION, entityId, entityKind: raw.entityKind,
    ...(raw.lineageId ? {lineageId:raw.lineageId} : {}),
    names: names(raw.names), alternateNames: [...new Set((raw.alternateNames || []).map(text).filter(Boolean))],
    lifetime, parentEntityId: text(raw.parentEntityId), geometryVersions, instantiation,
    metadata: structuredClone(raw.metadata || {}), sourceInfo: structuredClone(raw.sourceInfo || {}),
  });
}

// A coarse cursor denotes the end of its year/month. Stored endpoints retain
// their precision; inclusive bounds expand toward the appropriate direction.
export function territorialEntityExistsAt(entity, referenceDate) {
  const point = parseTemporal(referenceDate, { nullable: false });
  const { validFrom, validTo } = entity.lifetime;
  return (!validFrom || compareTemporal(validFrom, point, {rightBoundary: 'end'}) <= 0)
    && (!validTo || compareTemporal(validTo, point, {leftBoundary: 'end', rightBoundary: 'end'}) >= 0);
}

export function selectGeometryVersion(entity, referenceDate = null) {
  if (!referenceDate) {
    if (entity.geometryVersions.length === 1 && !entity.lifetime.validFrom && !entity.lifetime.validTo
      && !entity.geometryVersions[0].validFrom && !entity.geometryVersions[0].validTo) return entity.geometryVersions[0];
    throw new Error('A reference date is required for dated territorial geometry');
  }
  if (!territorialEntityExistsAt(entity, referenceDate)) return null;
  const candidates = entity.geometryVersions.filter(version => territorialEntityExistsAt({lifetime: version}, referenceDate));
  if (candidates.length > 1) throw new Error('Ambiguous territorial geometry selection');
  return candidates[0] || null;
}

export function instantiateLibraryEntity(entity, referenceDate) {
  const version = selectGeometryVersion(entity, referenceDate);
  if (!version) throw new Error('선택한 시점에 사용할 경계 버전이 없습니다.');
  return {
    entityId: entity.entityId, geometryVersionId: version.versionId, entityKind: entity.entityKind,
    name: entity.names.ko || entity.names.en || Object.values(entity.names)[0], parentEntityId: entity.parentEntityId,
    geometry: structuredClone(version.geometry), validFrom: null, validTo: null,
    metadata: {...structuredClone(entity.metadata), sourceInfo: structuredClone(entity.sourceInfo), sourceLifetime:structuredClone(entity.lifetime), sourceGeometryValidity:{validFrom:version.validFrom,validTo:version.validTo}, sourceReferenceDate:referenceDate, geometryCertainty: version.certainty, geometryDatePrecision: version.datePrecision},
    instantiation: structuredClone(entity.instantiation),
  };
}
