import { compareTemporal, normalizeTemporalInterval, parseTemporal } from './temporal.js';

export const TIMELINE_RECORD_SCHEMA_VERSION = 1;
const COLLECTIONS = ['lifetimes', 'geometryBindings', 'parentRelations'];
const COMMON_FIELDS = ['id', 'entityId', 'validFrom', 'validTo'];
const fail = (code, detail) => { throw Object.assign(new Error(`${code}: ${detail}`), { code }); };
const isObject = value => value !== null && typeof value === 'object' && !Array.isArray(value);
function shape(value, fields) {
  if (!isObject(value) || fields.some(field => !Object.hasOwn(value, field))
    || Object.keys(value).some(field => !fields.includes(field))) fail('TIMELINE_SCHEMA', 'Unexpected or missing record fields.');
}
function id(value) {
  if (typeof value !== 'string' || !value.length) fail('TIMELINE_ID', 'A nonempty string ID is required.');
  return value;
}
function interval(row) {
  try {
    for (const value of [row.validFrom, row.validTo]) {
      if (value !== null) {
        if (typeof value !== 'string') fail('TIMELINE_INTERVAL', 'Endpoints must be strings or null.');
        parseTemporal(value, { nullable: false });
      }
    }
    return normalizeTemporalInterval(row.validFrom, row.validTo);
  } catch (error) {
    if (error.code === 'TIMELINE_INTERVAL') throw error;
    throw Object.assign(new Error(`TIMELINE_INTERVAL: ${error.message}`, { cause: error }), { code: 'TIMELINE_INTERVAL' });
  }
}
const compareKeys = (a, b) => compareTemporal({ startKey: a }, { startKey: b });
function followingDay([year, month, day]) {
  const magnitude = String(Math.abs(year)).padStart(4, '0');
  const yearText = (year < 0 ? '-' : year > 9999 ? '+' : '') + magnitude;
  // Use the canonical temporal parser for month lengths and BCE leap semantics.
  const last = parseTemporal(`${yearText}-${String(month).padStart(2, '0')}`).endKey[2];
  if (day < last) return [year, month, day + 1];
  if (month < 12) return [year, month + 1, 1];
  // The key after +999999-12-31 is an internal event sentinel, never a stored date.
  return [year === -1 ? 1 : year + 1, 1, 1];
}

/** Pure model validation. Does not read/write project, renderer, history, or UI state. */
export function normalizeTimelineRecords(input, context) {
  shape(input, ['schemaVersion', ...COLLECTIONS]);
  if (input.schemaVersion !== TIMELINE_RECORD_SCHEMA_VERSION
    || COLLECTIONS.some(name => !Array.isArray(input[name]))) fail('TIMELINE_SCHEMA', 'Unsupported timeline shape/version.');
  if (!context || !Array.isArray(context.entities) || typeof context.geometryExists !== 'function') {
    fail('TIMELINE_CONTEXT', 'Entity catalog and geometry repository are required.');
  }
  const entities = new Map();
  for (const entity of context.entities) {
    const key = id(entity?.id);
    if (entities.has(key)) fail('TIMELINE_ENTITY', `Duplicate entity ${key}.`);
    if (!['general', 'regional'].includes(entity.entityKind)) fail('TIMELINE_KIND', `Invalid kind for ${key}.`);
    entities.set(key, entity.entityKind);
  }
  const ids = new Set();
  const hasLife = new Set();
  const slots = new Map([...entities.keys()].map(key => [key, [new Map(), new Map(), new Map()]]));
  const events = new Map([['open', { key: null, changes: [] }]]);
  function event(key, slot, row, add) {
    const token = key === null ? 'open' : key.join('/');
    if (!events.has(token)) events.set(token, { key, changes: [] });
    events.get(token).changes.push({ slot, row, add });
  }
  function common(raw, slot) {
    const fields = [...COMMON_FIELDS, ...(slot === 1 ? ['geometryRef'] : slot === 2 ? ['parentId', 'coverageMode'] : [])];
    shape(raw, fields);
    const recordId = id(raw.id), entityId = id(raw.entityId);
    if (ids.has(recordId)) fail('TIMELINE_ID', `Duplicate record ${recordId}.`);
    ids.add(recordId);
    if (!entities.has(entityId)) fail('TIMELINE_ENTITY', `Unknown entity ${entityId}.`);
    const dates = interval(raw);
    const row = { id: recordId, entityId, validFrom: dates.validFrom, validTo: dates.validTo };
    if (slot === 0) hasLife.add(entityId);
    if (slot === 1) {
      shape(raw.geometryRef, ['id', 'version']);
      const ref = raw.geometryRef;
      if (typeof ref.id !== 'string' || !ref.id.length || !Number.isInteger(ref.version)
        || ref.version < 1 || ref.version > 4294967295) fail('TIMELINE_GEOMETRY', `Invalid geometry reference for ${recordId}.`);
      row.geometryRef = Object.freeze({ id: ref.id, version: ref.version });
      // Do not swallow repository failures or replace them with an absent geometry.
      if (context.geometryExists(row.geometryRef) !== true) fail('TIMELINE_GEOMETRY', `Missing geometry for ${recordId}.`);
    }
    if (slot === 2) {
      if (typeof raw.parentId !== 'string') fail('TIMELINE_PARENT', `Parent ID must be a string for ${recordId}.`);
      row.parentId = raw.parentId;
      row.coverageMode = raw.coverageMode;
      if (!['explicit', 'partition'].includes(row.coverageMode)) fail('TIMELINE_COVERAGE', `Invalid coverage mode for ${recordId}.`);
      if (row.parentId === entityId) fail('TIMELINE_CYCLE', `Self-parent ${entityId}.`);
      if (row.parentId && !entities.has(row.parentId)) fail('TIMELINE_PARENT', `Unknown parent ${row.parentId}.`);
      if (row.parentId && (entities.get(entityId) !== 'general' || entities.get(row.parentId) !== 'general')) {
        fail('TIMELINE_KIND', 'Both ends of an administrative relationship must be general entities.');
      }
      if (!row.parentId && row.coverageMode !== 'explicit') fail('TIMELINE_COVERAGE', 'Roots and regional entities must be explicit.');
    }
    Object.freeze(row);
    event(dates.start?.startKey ?? null, slot, row, true);
    if (dates.end) event(followingDay(dates.end.endKey), slot, row, false);
    return row;
  }
  const result = { schemaVersion: TIMELINE_RECORD_SCHEMA_VERSION };
  COLLECTIONS.forEach((name, slot) => { result[name] = Object.freeze(input[name].map(row => common(row, slot))); });
  for (const key of entities.keys()) if (!hasLife.has(key)) fail('TIMELINE_LIFETIME', `No lifetime for ${key}.`);

  function inspect() {
    // Overlap checks precede coverage checks; no array-order winner is selected.
    for (const [key, values] of slots) {
      if (values.some(value => value.size > 1)) fail('TIMELINE_OVERLAP', `Concurrent records for ${key}.`);
    }
    const parents = new Map();
    for (const [key, [life, geometry, parent]] of slots) {
      if (!life.size && (geometry.size || parent.size)) fail('TIMELINE_OUTSIDE_LIFETIME', `Records outside the lifetime of ${key}.`);
      if (life.size) {
        if (!geometry.size || !parent.size) fail('TIMELINE_GAP', `Incomplete lifetime coverage for ${key}.`);
        parents.set(key, parent.values().next().value.parentId);
      }
    }
    for (const [key, parent] of parents) {
      if (parent && !parents.has(parent)) fail('TIMELINE_PARENT_INACTIVE', `Inactive parent ${parent} for ${key}.`);
    }
    const settled = new Set();
    for (const key of parents.keys()) {
      const path = new Set();
      let cursor = key;
      while (cursor && !settled.has(cursor)) {
        if (path.has(cursor)) fail('TIMELINE_CYCLE', `Parent cycle at ${cursor}.`);
        path.add(cursor);
        cursor = parents.get(cursor);
      }
      for (const entry of path) settled.add(entry);
    }
  }
  const ordered = [...events.values()].sort((a, b) => a.key === null ? (b.key === null ? 0 : -1)
    : b.key === null ? 1 : compareKeys(a.key, b.key));
  for (const boundary of ordered) {
    // Apply the entire simultaneous boundary before checking its resulting graph.
    for (const { slot, row, add } of boundary.changes) {
      const active = slots.get(row.entityId)[slot];
      if (add) active.set(row.id, row);
      else active.delete(row.id);
    }
    inspect();
  }
  return Object.freeze(result);
}
