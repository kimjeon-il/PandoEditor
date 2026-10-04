import { normalizeTerritorialIdentities, TERRITORIAL_ENTITY_KINDS } from './territorial-units.js';
import { normalizeTerritorialEntities } from './territorial-units.js';
import { assertCurrentProjectSchema } from './project-state.js';
import { assertProjectReferenceIntegrity } from './project-invariants.js';
import { restoreTimelineStorage } from './timeline-storage.js';
import { createGeometrySnapshotPool } from './geometry-versions.js';
import { TERRAIN_RASTER_VERSION } from './terrain-manifest.js';
import { PROJECT_SCHEMA_VERSION, SOURCE_PROVENANCE_SCHEMA_VERSION, GENERIC_FEATURE_SCHEMA_VERSION,
  TERRITORIAL_MODEL_SCHEMA_VERSION, DISTRIBUTION_MODEL_SCHEMA_VERSION } from './version-contract.js';

function modelContracts({ genericFeatureSchemaVersion, distributionSchemaVersion, distributionModes }) {
  return {
    landObjectModel: { schemaVersion: genericFeatureSchemaVersion, coastlineAuthority: 'territorialEntities',
      purpose: 'lossless-fallback', directCreation: false, sourceProvenanceSchemaVersion: SOURCE_PROVENANCE_SCHEMA_VERSION,
      canonicalProperties: ['name','notes','color','locked','source'] },
    territorialModel: { schemaVersion: TERRITORIAL_MODEL_SCHEMA_VERSION, coastlineAuthority: 'territorialEntities',
      storage: 'territorialEntities', kinds: Object.values(TERRITORIAL_ENTITY_KINDS), coverageModes: ['partition','explicit'] },
    distributionModel: { schemaVersion: distributionSchemaVersion, sourceModes: [...distributionModes], valueKind: 'finite-number' },
  };
}
export function createProjectSerializer({ schemaVersion = PROJECT_SCHEMA_VERSION, appVersion, baseDataset,
  genericFeatureSchemaVersion = GENERIC_FEATURE_SCHEMA_VERSION, distributionSchemaVersion = DISTRIBUTION_MODEL_SCHEMA_VERSION,
  distributionModes, terrainDataset, hydroDataset, readSnapshot, now = () => new Date() }) {
  const copies = createGeometrySnapshotPool();
  const contracts = { genericFeatureSchemaVersion, distributionSchemaVersion, distributionModes };
  const header = snapshot => ({ schemaVersion, version: appVersion, savedAt: now().toISOString(),
    baseDataset: snapshot.fullAutosave ? 'external-territorial-entities' : baseDataset, ...modelContracts(contracts) });
  function content(snapshot, clone) {
    const identities = normalizeTerritorialIdentities(snapshot.territorialEntities);
    const fields = snapshot.projectFields;
    const storage = restoreTimelineStorage({ schemaVersion: 1, records: fields?.timelineRecords,
      geometries: fields?.geometries }, identities.map(feature => ({ id: feature.id, entityKind: feature.properties.entityKind })));
    const result = { ...clone(fields), territorialEntities: identities,
      timelineRecords: storage.records, geometries: storage.geometries.snapshot() };
    assertProjectReferenceIntegrity({ ...result, storageOnly: true });
    return result;
  }
  function buildProject(snapshot = readSnapshot(), clone = structuredClone) {
    const result = { format: 'pandolab-project-state', ...header(snapshot),
      ...content(snapshot, clone),
      physicalSourceInfo: {
        terrain: { dataset: snapshot.terrainSourceInfo?.dataset || snapshot.terrainManifest?.dataset || terrainDataset,
          version: snapshot.terrainSourceInfo?.version || snapshot.terrainManifest?.version || TERRAIN_RASTER_VERSION },
        hydro: { dataset: snapshot.hydroManifest?.dataset || hydroDataset, version: snapshot.hydroManifest?.version || appVersion,
          coordinatePolicy: snapshot.hydroManifest?.coordinatePolicy || 'selected source coordinates retained without simplification',
          selection: structuredClone(snapshot.hydroManifest?.selection || {}) },
      } };
    assertCurrentProjectSchema(result);
    return result;
  }
  function buildAutosave() {
    const snapshot = readSnapshot();
    if (snapshot.fullAutosave) return { ...buildProject(snapshot, copies.clone), format: 'pandolab-autosave-full' };
    assertBaselineFingerprint(snapshot.baseDatasetFingerprint);
    const { territorialEntities, ...fields } = content(snapshot, copies.clone);
    assertCurrentProjectSchema({ format:'pandolab-project-state', ...header(snapshot), ...fields, territorialEntities });
    return { format: 'pandolab-autosave-delta', ...header(snapshot), ...fields, baseDatasetFingerprint: snapshot.baseDatasetFingerprint,
      entityDelta: { changed: normalizeTerritorialIdentities(snapshot.entityDelta?.changed || []), removedIds: [...(snapshot.entityDelta?.removedIds || [])] } };
  }
  return Object.freeze({ buildProject, buildAutosave });
}
export function restoreEntitiesFromDelta(project, { base, baseDataset, baseDatasetFingerprint, clone = structuredClone }) {
  const delta = project.entityDelta;
  if (!Array.isArray(base) || !Array.isArray(delta?.changed) || !Array.isArray(delta?.removedIds)) throw new TypeError('영역 변경분 또는 기본 자료가 올바르지 않습니다.');
  assertBaselineFingerprint(project.baseDatasetFingerprint);
  if (typeof baseDataset !== 'string' || !baseDataset || baseDataset !== project.baseDataset
    || baseDatasetFingerprint !== project.baseDatasetFingerprint)
    throw Object.assign(new Error('자동저장 기준 데이터가 없거나 저장된 기준과 다릅니다.'), { code: 'PL-SCHEMA-BASE' });
  const changes = new Map();
  const removed = new Set(delta.removedIds);
  if (removed.size !== delta.removedIds.length || delta.removedIds.some(id => typeof id !== 'string' || !id.trim()))
    throw new TypeError('영역 삭제 ID가 올바르지 않습니다.');
  const identities = normalizeTerritorialIdentities(base);
  for (const feature of normalizeTerritorialIdentities(delta.changed)) {
    const id = String(feature.id);
    if (changes.has(id) || removed.has(id)) throw new Error('영역 변경 ID 중복: ' + id);
    changes.set(id, feature);
  }
  const entities = identities.filter(feature => !removed.has(String(feature.id))).map(feature => {
    const replacement = changes.get(String(feature.id)); changes.delete(String(feature.id));
    return clone(replacement || feature);
  }).concat([...changes.values()].map(entity => clone(entity)));
  const result = normalizeTerritorialIdentities(entities);
  restoreTimelineStorage({ schemaVersion: 1, records: project.timelineRecords, geometries: project.geometries },
    result.map(entity => ({ id: entity.id, entityKind: entity.properties.entityKind })));
  return result;
}

function assertBaselineFingerprint(value) {
  if (typeof value !== 'string' || !/^[a-f0-9]{64}$/.test(value))
    throw Object.assign(new Error('자동저장 기준 데이터 fingerprint가 필요합니다.'), { code: 'PL-SCHEMA-BASE' });
}

/** Hash the canonical baseline, including every omitted identity and exact shape. */
export async function fingerprintProjectBaseline(features) {
  const stable = value => {
    if (Array.isArray(value)) return value.map(stable);
    if (value && typeof value === 'object') return Object.fromEntries(Object.keys(value).sort().map(key => [key, stable(value[key])]));
    return value;
  };
  const baseline = normalizeTerritorialEntities(features, { cloneGeometry: geometry => geometry })
    .sort((a, b) => a.id < b.id ? -1 : a.id > b.id ? 1 : 0);
  const bytes = new TextEncoder().encode(JSON.stringify(stable(baseline)));
  const digest = await crypto.subtle.digest('SHA-256', bytes);
  return [...new Uint8Array(digest)].map(value => value.toString(16).padStart(2, '0')).join('');
}
