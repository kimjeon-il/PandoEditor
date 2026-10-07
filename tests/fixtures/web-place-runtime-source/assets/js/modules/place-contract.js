/** Builtin places share the label domain, but never the project's storage. */
export const PLACE_LIMITS = Object.freeze({ candidates: 1500, layoutCandidates: 2048, tileRecords: 512, queryTiles: 96, shardBytes: 512 * 1024, cacheBytes: 24 * 1024 * 1024, searchResults: 50, retainedRecords: 256 });
export const PLACE_KINDS = Object.freeze(['capital', 'city', 'region', 'town', 'mountain', 'water', 'custom']);
export const isBuiltinPlaceId = id => /^builtin:place:[a-z0-9-]+:.+$/u.test(String(id || ''));
export const normalizePlaceQuery = value => String(value || '').normalize('NFKC').trim().toLocaleLowerCase('ko').replace(/\s+/gu, ' ');

function text(value, name, maximum, required = false) {
  const result = String(value ?? '').trim();
  if ((required && !result) || [...result].length > maximum || result.includes('\0')) throw new TypeError(`Invalid place ${name}`);
  return result;
}
export function normalizePlace(raw) {
  const source = text(raw?.source, 'source', 32, true);
  if (!/^[a-z0-9-]+$/u.test(source)) throw new TypeError('Invalid place source');
  const sourceId = text(raw.sourceId, 'sourceId', 128, true);
  const coordinates = [Number(raw.coordinates?.[0]), Number(raw.coordinates?.[1])];
  if (!coordinates.every(Number.isFinite) || Math.abs(coordinates[0]) > 180 || Math.abs(coordinates[1]) > 90) throw new TypeError('Invalid place coordinates');
  const population = Number(raw.population ?? 0), priority = Number(raw.priority ?? 40), minZoom = Number(raw.minZoom ?? 0);
  if (!Number.isFinite(population) || population < 0 || !Number.isFinite(Math.fround(priority)) || !Number.isFinite(Math.fround(minZoom)) || minZoom < 0) throw new TypeError('Invalid place ranking');
  const kind = text(raw.kind || 'custom', 'kind', 32);
  if (!PLACE_KINDS.includes(kind)) throw new TypeError('Invalid place kind');
  return Object.freeze({ id: `builtin:place:${source}:${sourceId}`, source, sourceId, name: text(raw.name, 'name', 256, true), kind,
    coordinates: Object.freeze(coordinates), countryCode: text(raw.countryCode, 'countryCode', 8), population,
    priority: Math.fround(priority), minZoom: Math.fround(minZoom), featureCode: text(raw.featureCode, 'featureCode', 32), notes: '' });
}
export function comparePlaces(a, b) {
  return b.priority - a.priority || b.population - a.population || (a.id < b.id ? -1 : a.id > b.id ? 1 : 0);
}
