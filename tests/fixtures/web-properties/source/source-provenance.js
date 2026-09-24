import { SOURCE_PROVENANCE_SCHEMA_VERSION } from './version-contract.js';

export { SOURCE_PROVENANCE_SCHEMA_VERSION };

export const SOURCE_KINDS = Object.freeze({
  USER: 'user',
  BUILTIN: 'builtin',
  LIBRARY: 'library',
  GIS: 'gis',
  LEGACY: 'legacy',
  PLUGIN: 'plugin',
  UNSUPPORTED: 'unsupported',
});

const SOURCE_KIND_SET = new Set(Object.values(SOURCE_KINDS));
const SOURCE_KEYS = new Set([
  'schemaVersion', 'kind', 'dataset', 'sourceId', 'sourceFormat', 'sourceType',
  'version', 'importedAt', 'details',
]);
const SOURCE_ALIASES = new Set(['id', 'format', 'type']);
const text = value => String(value ?? '').trim();
const plainObject = value => !!value && typeof value === 'object' && !Array.isArray(value);
const clone = value => value == null ? value : structuredClone(value);

function normalizedDetails(value) {
  return plainObject(value) ? clone(value) : {};
}

export function normalizeSourceProvenance(value = {}, defaults = {}) {
  const raw = typeof value === 'string'
    ? { dataset: value }
    : plainObject(value) ? value : {};
  const fallback = plainObject(defaults) ? defaults : {};
  let kind = text(raw.kind || fallback.kind);
  if (!SOURCE_KIND_SET.has(kind)) kind = SOURCE_KINDS.UNSUPPORTED;

  const details = {
    ...normalizedDetails(fallback.details),
    ...normalizedDetails(raw.details),
  };
  const unmapped = {};
  for (const [key, item] of Object.entries(raw)) {
    if (SOURCE_KEYS.has(key) || SOURCE_ALIASES.has(key) || item === undefined) continue;
    unmapped[key] = clone(item);
  }
  if (Object.keys(unmapped).length) {
    details.unmappedSourceFields = {
      ...normalizedDetails(details.unmappedSourceFields),
      ...unmapped,
    };
  }

  return Object.freeze({
    schemaVersion: SOURCE_PROVENANCE_SCHEMA_VERSION,
    kind,
    dataset: text(raw.dataset ?? fallback.dataset),
    sourceId: text(raw.sourceId ?? raw.id ?? fallback.sourceId),
    sourceFormat: text(raw.sourceFormat ?? raw.format ?? fallback.sourceFormat),
    sourceType: text(raw.sourceType ?? raw.type ?? fallback.sourceType),
    version: text(raw.version ?? fallback.version),
    importedAt: text(raw.importedAt ?? fallback.importedAt),
    details,
  });
}

export function validateSourceProvenance(value) {
  const issues = [];
  if (!plainObject(value)) return { ok: false, issues: ['source provenance must be an object'] };
  for (const key of Object.keys(value)) {
    if (!SOURCE_KEYS.has(key)) issues.push(`unsupported source provenance field: ${key}`);
  }
  if (Number(value.schemaVersion) !== SOURCE_PROVENANCE_SCHEMA_VERSION) {
    issues.push(`source provenance schemaVersion must be ${SOURCE_PROVENANCE_SCHEMA_VERSION}`);
  }
  if (!SOURCE_KIND_SET.has(text(value.kind))) issues.push(`unknown source provenance kind: ${text(value.kind) || '(empty)'}`);
  for (const key of ['dataset', 'sourceId', 'sourceFormat', 'sourceType', 'version', 'importedAt']) {
    if (value[key] != null && typeof value[key] !== 'string') issues.push(`source provenance ${key} must be a string`);
  }
  if (value.details != null && !plainObject(value.details)) issues.push('source provenance details must be an object');
  return { ok: issues.length === 0, issues };
}
