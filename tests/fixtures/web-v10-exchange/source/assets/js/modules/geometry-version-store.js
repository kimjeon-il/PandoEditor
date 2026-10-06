// Persistent geometry IDs/versions are not renderer revisions or snapshot-pool keys.
const fail = (code, detail) => { throw Object.assign(new Error(`${code}: ${detail}`), { code }); };
const invalid = detail => fail('INVALID_GEOMETRY', detail);
function fields(value, names) {
  if (!value || typeof value !== 'object' || Array.isArray(value)
    || names.some(name => !Object.hasOwn(value, name))
    || Object.keys(value).some(name => !names.includes(name))) invalid('Unexpected or missing fields.');
}
function reference(ref) {
  fields(ref, ['id', 'version']);
  if (typeof ref.id !== 'string' || !ref.id.length || !Number.isInteger(ref.version)
    || ref.version < 1 || ref.version > 4294967295) invalid('A nonempty ID and uint32 version are required.');
  return JSON.stringify([ref.id, ref.version]);
}
function point(value) {
  if (!Array.isArray(value) || value.length !== 2 || !Number.isFinite(value[0])
    || !Number.isFinite(value[1]) || Math.abs(value[0]) > 180 || Math.abs(value[1]) > 90) invalid('Coordinate.');
}
function list(value, minimum) {
  if (!Array.isArray(value) || value.length < minimum) invalid('Empty or short geometry.');
}
function line(value) {
  list(value, 2);
  for (const position of value) point(position);
}
function ring(value) {
  list(value, 4);
  const first = value[0], last = value.at(-1);
  for (const position of value) point(position);
  if (first[0] !== last[0] || first[1] !== last[1]) invalid('Open ring.');
  let area = 0;
  for (let i = 0; i < value.length; i += 1) {
    const a = value[i], b = value[(i + 1) % value.length];
    area += a[0] * b[1] - b[0] * a[1];
  }
  if (Math.abs(area) < 1e-14) invalid('Degenerate ring.');
}
function polygon(value) {
  list(value, 1);
  for (const boundary of value) ring(boundary);
}
function freeze(value) {
  if (value && typeof value === 'object') {
    for (const child of Object.values(value)) freeze(child);
    Object.freeze(value);
  }
  return value;
}
// Values branded here have passed structural validation and recursive freezing.
// Their validity cannot change; separate registries may safely retain them.
const immutableGeometries = new WeakSet();
function geometry(value) {
  if (immutableGeometries.has(value)) return value;
  fields(value, ['type', 'coordinates']);
  const copy = structuredClone(value), c = copy.coordinates;
  switch (copy.type) {
    case 'Point': point(c); break;
    case 'MultiPoint': list(c, 1); for (const p of c) point(p); break;
    case 'LineString': line(c); break;
    case 'MultiLineString': list(c, 1); for (const path of c) line(path); break;
    case 'Polygon': polygon(c); break;
    case 'MultiPolygon': list(c, 1); for (const p of c) polygon(p); break;
    default: invalid('Unsupported geometry type.');
  }
  // Structural validation matches GeometryStore::insert in the native core.
  // Topology/partition checks remain the editing owner's responsibility.
  freeze(copy);
  immutableGeometries.add(copy);
  return copy;
}
const encoder = new TextEncoder();
function compareIds(a, b) {
  const left = encoder.encode(a), right = encoder.encode(b);
  for (let i = 0; i < Math.min(left.length, right.length); i += 1) {
    if (left[i] !== right[i]) return left[i] - right[i];
  }
  return left.length - right.length;
}

/** Append-only geometry registry. Restoration constructs an isolated candidate. */
export function createGeometryVersionStore(entries = [], { reuse = null } = {}) {
  if (!Array.isArray(entries)) invalid('Geometry versions must be an array.');
  const versions = new Map();
  function insert(ref, value) {
    const key = reference(ref);
    if (versions.has(key)) fail('DUPLICATE_ID', 'Geometry version already exists.');
    const validated = geometry(value);
    const previous = reuse?.get(ref);
    const geojson = previous && (previous === validated || JSON.stringify(previous) === JSON.stringify(validated)) ? previous : validated;
    // No registry changes are made until validation and copying have succeeded.
    versions.set(key, Object.freeze({ id: ref.id, version: ref.version, geojson }));
  }
  for (const entry of entries) {
    fields(entry, ['id', 'version', 'geojson']);
    insert({ id: entry.id, version: entry.version }, entry.geojson);
  }
  return Object.freeze({
    insert,
    get: ref => versions.get(reference(ref))?.geojson ?? null,
    snapshot: () => Object.freeze([...versions.values()].sort((a, b) => compareIds(a.id, b.id) || a.version - b.version)),
  });
}
