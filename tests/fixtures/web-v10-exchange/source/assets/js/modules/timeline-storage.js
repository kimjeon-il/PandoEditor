import { createGeometryVersionStore } from './geometry-version-store.js';
import { normalizeTimelineRecords } from './timeline-records.js';

export const TIMELINE_STORAGE_SCHEMA_VERSION = 1;
const fail = (code, detail) => { throw Object.assign(new Error(`${code}: ${detail}`), { code }); };
function context(geometries, entities) {
  if (!Array.isArray(entities) || !geometries || typeof geometries.get !== 'function'
    || typeof geometries.snapshot !== 'function') fail('TIMELINE_CONTEXT', 'An entity catalog and geometry store are required.');
  return { entities, geometryExists(ref) {
    const geometry = geometries.get(ref);
    return geometry?.type === 'Polygon' || geometry?.type === 'MultiPolygon';
  } };
}

/** A storage/checkpoint value; does not change the current project or its history. */
export function snapshotTimelineStorage(records, geometries, entities) {
  const normalized = normalizeTimelineRecords(records, context(geometries, entities));
  return Object.freeze({
    schemaVersion: TIMELINE_STORAGE_SCHEMA_VERSION,
    records: normalized,
    // Preserve all versions, including versions used by other document domains.
    geometries: geometries.snapshot(),
  });
}

/** Construct and validate everything before a caller replaces its current state. */
export function restoreTimelineStorage(input, entities, { reuse = null } = {}) {
  const fields = ['schemaVersion', 'records', 'geometries'];
  if (!input || typeof input !== 'object' || Array.isArray(input)
    || fields.some(field => !Object.hasOwn(input, field))
    || Object.keys(input).some(field => !fields.includes(field))
    || input.schemaVersion !== TIMELINE_STORAGE_SCHEMA_VERSION || !Array.isArray(input.geometries)) {
    fail('TIMELINE_STORAGE_SCHEMA', 'Unexpected or missing storage fields/version.');
  }
  if (!Array.isArray(entities)) fail('TIMELINE_CONTEXT', 'An entity catalog is required.');
  const geometries = createGeometryVersionStore(input.geometries, { reuse });
  const records = normalizeTimelineRecords(input.records, context(geometries, entities));
  return Object.freeze({ records, geometries });
}
