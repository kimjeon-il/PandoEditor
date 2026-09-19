export const PROJECT_SCHEMA_VERSION = 5;
export const MIN_SUPPORTED_PROJECT_SCHEMA_VERSION = 3;

export const LAND_OBJECT_SCHEMA_VERSION = 2;
export const SOURCE_PROVENANCE_SCHEMA_VERSION = 1;
export const GENERIC_FEATURE_SCHEMA_VERSION = 2;
export const TERRITORIAL_MODEL_SCHEMA_VERSION = 2;
export const DISTRIBUTION_MODEL_SCHEMA_VERSION = 2;
export const LAYER_PRESENTATION_SCHEMA_VERSION = 3;

export const VERSION_POLICY = Object.freeze({
  appVersionSource: 'package.json',
  minor: Object.freeze([
    'user-visible feature',
    'UX system change',
    'architecture contract change',
    'save-format capability change',
  ]),
  patch: Object.freeze([
    'bug fix',
    'visual polish',
    'performance improvement without contract change',
    'internal refactor without contract change',
  ]),
  projectSchemaIndependentFromAppVersion: true,
  datasetVersionsIndependentFromAppVersion: true,
  assetRevisionSource: 'build metadata',
});
