import { normalizeCountryFeature } from './country-feature.js';
import { migrateProjectV4ToV5 } from './subunit-migration.js';
import { normalizeLayerPresentation } from './layer-presentation.js';
export { migrateProjectV4ToV5 } from './subunit-migration.js';
import {
  GENERIC_FEATURE_CANONICAL_PROPERTY_KEYS,
  normalizeGenericFeatureCollection,
} from './generic-feature-service.js';
import {
  PROJECT_SCHEMA_VERSION,
  MIN_SUPPORTED_PROJECT_SCHEMA_VERSION,
  LAND_OBJECT_SCHEMA_VERSION,
  SOURCE_PROVENANCE_SCHEMA_VERSION,
} from './version-contract.js';

const text = value => String(value ?? '').trim();
const clone = value => value == null ? value : structuredClone(value);
const GENERIC_GROUP_ALIASES = Object.freeze(['drawings', 'userDrawings']);

function migrationError(message, code = 'PL-MIGRATION-001') {
  const error = new Error(message);
  error.code = code;
  return error;
}

function legacyCountryIdentity(feature, index) {
  const properties = feature?.properties || {};
  const id = text(feature?.id || properties.editor_id || properties.iso_a3 || properties.ISO_A3 || properties.ADM0_A3)
    || `country_${index + 1}`;
  const name = text(properties.name || properties.editor_name || properties.editor_original_name || properties.ADMIN || properties.NAME || properties.NAME_LONG)
    || id;
  return { id, name };
}

function legacyCountryOverride(feature) {
  const properties = feature?.properties || {};
  const output = {};
  const copyText = (target, ...keys) => {
    const value = keys.map(key => text(properties[key])).find(Boolean);
    if (value) output[target] = value;
  };
  copyText('name', 'editor_name');
  copyText('color', 'editor_color');
  copyText('capital', 'editor_capital');
  copyText('notes', 'editor_notes');
  copyText('flagDataUrl', 'editor_flag_data_url');
  if (properties.editor_locked === true || properties.locked === true) output.locked = true;
  return output;
}

function migrateCountryCollection(collection, overrides = {}) {
  if (!collection?.features) return { collection, overrides };
  const nextOverrides = clone(overrides || {});
  const features = collection.features.map((feature, index) => {
    const identity = legacyCountryIdentity(feature, index);
    const legacyOverride = legacyCountryOverride(feature);
    if (Object.keys(legacyOverride).length) {
      nextOverrides[identity.id] = { ...legacyOverride, ...(nextOverrides[identity.id] || {}) };
    }
    return normalizeCountryFeature(feature, identity);
  });
  return {
    collection: { type: 'FeatureCollection', features },
    overrides: nextOverrides,
  };
}

function renameGenericGroupAliases(project) {
  for (const field of ['layerVisibility', 'itemVisibility']) {
    const container = project[field];
    if (!container || typeof container !== 'object') continue;
    for (const alias of GENERIC_GROUP_ALIASES) {
      if (container.genericFeatures == null && container[alias] != null) container.genericFeatures = clone(container[alias]);
      delete container[alias];
    }
  }

  const presentation = project.layerPresentation;
  if (presentation?.styles && typeof presentation.styles === 'object') {
    for (const alias of GENERIC_GROUP_ALIASES) {
      if (presentation.styles.genericFeatures == null && presentation.styles[alias] != null) {
        presentation.styles.genericFeatures = clone(presentation.styles[alias]);
      }
      delete presentation.styles[alias];
    }
  }
  if (Array.isArray(presentation?.overlayOrder)) {
    presentation.overlayOrder = presentation.overlayOrder.map(group => GENERIC_GROUP_ALIASES.includes(group) ? 'genericFeatures' : group);
  }
}

function genericFallbackContract() {
  return {
    schemaVersion: LAND_OBJECT_SCHEMA_VERSION,
    coastlineAuthority: 'countries',
    purpose: 'lossless-fallback',
    directCreation: false,
    sourceProvenanceSchemaVersion: SOURCE_PROVENANCE_SCHEMA_VERSION,
    canonicalProperties: GENERIC_FEATURE_CANONICAL_PROPERTY_KEYS.filter(key => key !== 'schemaVersion'),
  };
}

export function migrateProjectV3ToV4(input) {
  const project = clone(input);
  if (Number(project?.schemaVersion) !== 3) throw migrationError('Project v3 migration requires schemaVersion 3.', 'PL-MIGRATION-V3');

  const migratedCountries = migrateCountryCollection(project.countriesData, project.countryOverrides);
  project.countriesData = migratedCountries.collection;
  project.countryOverrides = migratedCountries.overrides;

  if (project.countryDelta?.changed) {
    const changed = migrateCountryCollection({ type: 'FeatureCollection', features: project.countryDelta.changed }, project.countryOverrides);
    project.countryDelta.changed = changed.collection.features;
    project.countryOverrides = changed.overrides;
  }

  const legacyDrawings = Array.isArray(project.drawings) ? project.drawings : [];
  project.genericFeatures = normalizeGenericFeatureCollection([
    ...(Array.isArray(project.genericFeatures) ? project.genericFeatures : []),
    ...legacyDrawings,
  ]);
  delete project.drawings;
  renameGenericGroupAliases(project);

  project.landObjectModel = genericFallbackContract();
  project.schemaVersion = 4;
  return project;
}

const LEGACY_DISTRIBUTION_GROUPS = Object.freeze({
  language: 'languages',
  ethnicity: 'ethnicities',
  religion: 'religions',
});

export function migrateProjectV5ToV6(input) {
  const project = clone(input);
  if (Number(project?.schemaVersion) !== 5) throw migrationError('Project v5 migration requires schemaVersion 5.', 'PL-MIGRATION-V5');
  const oldVisibility = project.layerVisibility || {};
  const oldItems = project.itemVisibility || {};
  const nextItems = { ...(oldItems.distributions || {}) };
  project.distributionLayers = (project.distributionLayers || []).map(layer => {
    const group = LEGACY_DISTRIBUTION_GROUPS[layer.type];
    if (group && (oldVisibility[group] === false || oldItems[group]?.[layer.id] === false)) nextItems[layer.id] = false;
    const rest = { ...layer };
    delete rest.type;
    return { ...rest, schemaVersion: 3, unit: '%', valueScale: { mode: 'manual', min: 0, max: 100 } };
  });
  project.distributionEntries = (project.distributionEntries || []).map(entry => {
    const { share, ...rest } = entry;
    return { ...rest, schemaVersion: 3, value: share };
  });
  for (const group of Object.values(LEGACY_DISTRIBUTION_GROUPS)) {
    delete oldVisibility[group];
    delete oldItems[group];
  }
  project.layerVisibility = { ...oldVisibility, distributions: true };
  project.itemVisibility = { ...oldItems, distributions: nextItems };
  const oldPresentation = project.layerPresentation || {};
  const oldStyles = oldPresentation.styles || {};
  project.layerPresentation = normalizeLayerPresentation({
    ...oldPresentation,
    styles: { ...oldStyles, distributions: oldStyles.languages || oldStyles.ethnicities || oldStyles.religions || oldStyles.distributions },
  });
  project.distributionSettings = {
    renderMode: 'overlap',
    activeLayerId: '',
    boundaryVisible: project.distributionSettings?.boundaryVisible !== false,
  };
  project.distributionModel = {
    schemaVersion: 3,
    sourceModes: ['territorial', 'geometry'],
    valueKind: 'finite-number',
  };
  project.schemaVersion = 6;
  return project;
}

export const PROJECT_MIGRATIONS = Object.freeze({
  3: migrateProjectV3ToV4,
  4: migrateProjectV4ToV5,
  5: migrateProjectV5ToV6,
});

export function migrationPath(fromVersion, toVersion = PROJECT_SCHEMA_VERSION) {
  const from = Number(fromVersion);
  const to = Number(toVersion);
  if (!Number.isInteger(from) || !Number.isInteger(to) || from > to) return [];
  const path = [];
  for (let version = from; version < to; version += 1) {
    if (typeof PROJECT_MIGRATIONS[version] !== 'function') return [];
    path.push(Object.freeze({ from: version, to: version + 1 }));
  }
  return path;
}

export function migrateProjectToCurrent(input) {
  if (!input || typeof input !== 'object') throw migrationError('프로젝트 형식이 올바르지 않습니다.', 'PL-MIGRATION-FORMAT');
  const startVersion = Number(input.schemaVersion);
  if (!Number.isInteger(startVersion)) throw migrationError('프로젝트 schemaVersion이 없습니다.', 'PL-MIGRATION-MISSING');
  if (startVersion > PROJECT_SCHEMA_VERSION) {
    throw migrationError(`프로젝트 schemaVersion ${startVersion}은 현재 버전 ${PROJECT_SCHEMA_VERSION}보다 새롭습니다.`, 'PL-MIGRATION-FUTURE');
  }
  if (startVersion < MIN_SUPPORTED_PROJECT_SCHEMA_VERSION) {
    throw migrationError(`프로젝트 schemaVersion ${startVersion}은 자동 마이그레이션 지원 범위(${MIN_SUPPORTED_PROJECT_SCHEMA_VERSION}~${PROJECT_SCHEMA_VERSION})보다 오래되었습니다.`, 'PL-MIGRATION-TOO-OLD');
  }
  let current = clone(input);
  while (Number(current.schemaVersion) < PROJECT_SCHEMA_VERSION) {
    const version = Number(current.schemaVersion);
    const migrate = PROJECT_MIGRATIONS[version];
    if (typeof migrate !== 'function') throw migrationError(`schemaVersion ${version} → ${version + 1} 마이그레이션이 없습니다.`, 'PL-MIGRATION-GAP');
    current = migrate(current);
    if (Number(current.schemaVersion) !== version + 1) {
      throw migrationError(`schemaVersion ${version} 마이그레이션 결과가 ${version + 1}이 아닙니다.`, 'PL-MIGRATION-RESULT');
    }
  }
  return current;
}

export function migrateProjectInPlace(project) {
  const migrated = migrateProjectToCurrent(project);
  if (migrated === project) return project;
  for (const key of Object.keys(project)) delete project[key];
  Object.assign(project, migrated);
  return project;
}
