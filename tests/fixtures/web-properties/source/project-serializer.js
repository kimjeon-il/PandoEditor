import { normalizeCountryFeature, pruneCountryOverrides } from './country-feature.js';
import { createGeometrySnapshotPool } from './geometry-versions.js';
import {
  PROJECT_SCHEMA_VERSION,
  SOURCE_PROVENANCE_SCHEMA_VERSION,
  GENERIC_FEATURE_SCHEMA_VERSION,
  TERRITORIAL_MODEL_SCHEMA_VERSION,
  DISTRIBUTION_MODEL_SCHEMA_VERSION,
} from './version-contract.js';

function cloneCountryFeature(feature, clone = structuredClone) {
  return normalizeCountryFeature(feature, { clone });
}

function normalizeCountriesData(collection, clone = structuredClone) {
  return {
    type: 'FeatureCollection',
    features: (collection?.features || []).map(feature => cloneCountryFeature(feature, clone)),
  };
}

function normalizeProjectFields(fields, countriesData) {
  const ids = new Set((countriesData.features || []).map(feature => String(feature.id)));
  return {
    ...(fields || {}),
    countryOverrides: pruneCountryOverrides(fields?.countryOverrides, ids),
  };
}

function landObjectContract(genericFeatureSchemaVersion, compact = false) {
  if (Number(genericFeatureSchemaVersion) >= 2) {
    return {
      schemaVersion: genericFeatureSchemaVersion,
      coastlineAuthority: 'countries',
      purpose: 'lossless-fallback',
      directCreation: false,
      sourceProvenanceSchemaVersion: SOURCE_PROVENANCE_SCHEMA_VERSION,
      ...(compact ? {} : { canonicalProperties: ['name', 'notes', 'color', 'locked', 'source'] }),
    };
  }
  return {
    schemaVersion: genericFeatureSchemaVersion,
    coastlineAuthority: 'countries',
    ...(compact ? {} : { roles: ['hydro', 'thematic', 'generic'] }),
  };
}

function modelContracts({ genericFeatureSchemaVersion, distributionSchemaVersion, distributionTypes, distributionModes }, compact = false) {
  return {
    landObjectModel: landObjectContract(genericFeatureSchemaVersion, compact),
    territorialModel: {
      schemaVersion: TERRITORIAL_MODEL_SCHEMA_VERSION,
      coastlineAuthority: 'countriesData',
      countryStorage: 'countriesData-adapter',
      types: ['country', 'subunit', 'region'],
      coverageModes: ['partition', 'explicit'],
    },
    distributionModel: {
      schemaVersion: distributionSchemaVersion,
      types: [...distributionTypes],
      sourceModes: [...distributionModes],
      shareRange: [0, 100],
      sharesAreIndependent: true,
    },
  };
}

export function createProjectSerializer({
  schemaVersion = PROJECT_SCHEMA_VERSION,
  appVersion,
  baseDataset,
  genericFeatureSchemaVersion = GENERIC_FEATURE_SCHEMA_VERSION,
  distributionSchemaVersion = DISTRIBUTION_MODEL_SCHEMA_VERSION,
  distributionTypes,
  distributionModes,
  terrainDataset,
  hydroDataset,
  readSnapshot,
  now = () => new Date(),
}) {
  const contracts = { genericFeatureSchemaVersion, distributionSchemaVersion, distributionTypes, distributionModes };
  const autosaveCopies = createGeometrySnapshotPool();

  function buildProject(snapshot = readSnapshot(), clone = structuredClone) {
    const countriesData = normalizeCountriesData(snapshot.countriesData, clone);
    return {
      format: 'pandolab-project-state',
      schemaVersion,
      version: appVersion,
      savedAt: now().toISOString(),
      countriesData,
      ...normalizeProjectFields(clone(snapshot.projectFields), countriesData),
      baseDataset,
      ...modelContracts(contracts),
      physicalSourceInfo: {
        terrain: {
          dataset: snapshot.terrainManifest?.dataset || terrainDataset,
          version: snapshot.terrainManifest?.version || '0.12.6',
        },
        hydro: {
          dataset: snapshot.hydroManifest?.dataset || hydroDataset,
          version: snapshot.hydroManifest?.version || appVersion,
          coordinatePolicy: snapshot.hydroManifest?.coordinatePolicy || 'selected source coordinates retained without simplification',
          selection: structuredClone(snapshot.hydroManifest?.selection || {}),
        },
      },
    };
  }

  function buildAutosave() {
    const snapshot = readSnapshot();
    if (snapshot.fullAutosave) return { ...buildProject(snapshot, autosaveCopies.clone), format: 'pandolab-autosave-full' };
    const changed = normalizeCountriesData({ features: snapshot.countryDelta?.changed || [] }, autosaveCopies.clone).features;
    const currentIds = new Set((snapshot.countriesData?.features || []).map(feature => String(feature.id)));
    return {
      format: 'pandolab-autosave-delta',
      schemaVersion,
      version: appVersion,
      savedAt: now().toISOString(),
      countryDelta: { changed, removedIds: [...(snapshot.countryDelta?.removedIds || [])].map(String) },
      ...{
        ...autosaveCopies.clone(snapshot.projectFields || {}),
        countryOverrides: pruneCountryOverrides(snapshot.projectFields?.countryOverrides, currentIds),
      },
      baseDataset,
      ...modelContracts(contracts, true),
    };
  }

  return Object.freeze({ buildProject, buildAutosave });
}

export function restoreCountriesFromDelta(project, {
  base,
  clone = structuredClone,
  reindex,
  applyPristineLabelAnchors,
}) {
  const delta = project?.countryDelta || { changed: [], removedIds: [] };
  const changed = new Map((delta.changed || []).map(feature => [String(feature?.id || ''), feature]));
  const removed = new Set((delta.removedIds || []).map(String));
  const seen = new Set();
  base.features = (base.features || []).filter(feature => {
    const id = String(feature?.id || '');
    return !removed.has(id);
  }).map(feature => {
    const id = String(feature?.id || '');
    if (!changed.has(id)) return feature;
    seen.add(id);
    return cloneCountryFeature(changed.get(id), clone);
  });
  for (const [id, feature] of changed) if (!seen.has(id) && !removed.has(id)) base.features.push(cloneCountryFeature(feature, clone));
  const result = reindex(base);
  const unchangedIds = (result.features || []).map(feature => String(feature?.id || '')).filter(id => !changed.has(id));
  applyPristineLabelAnchors(result, unchangedIds);
  return result;
}
