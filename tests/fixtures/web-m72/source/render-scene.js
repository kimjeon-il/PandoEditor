import { buildSelectionBoundarySegments } from './selection-stroke-geometry.js';
import { resolveRenderLod, simplifyRenderGeometry } from './render-lod.js';

const EMPTY_REVISIONS = Object.freeze({
  geometry: 0,
  style: 0,
  overlayOrder: 0,
  countryState: 0,
  selection: 0,
  editPreview: 0,
  view: 0,
});

function finiteCoordinate(value) {
  return Array.isArray(value) && value.length >= 2
    && Number.isFinite(Number(value[0])) && Number.isFinite(Number(value[1]));
}

function openRing(ring = []) {
  const coordinates = ring.filter(finiteCoordinate).map(coord => [Number(coord[0]), Number(coord[1])]);
  if (coordinates.length > 1) {
    const first = coordinates[0];
    const last = coordinates[coordinates.length - 1];
    if (first[0] === last[0] && first[1] === last[1]) coordinates.pop();
  }
  return coordinates;
}

function unwrapRing(ring = [], anchor = null) {
  const coordinates = openRing(ring);
  if (!coordinates.length) return coordinates;
  const result = [];
  let previous = Number.isFinite(anchor) ? Number(anchor) : coordinates[0][0];
  for (const coordinate of coordinates) {
    let longitude = coordinate[0];
    while (longitude - previous > 180) longitude -= 360;
    while (longitude - previous < -180) longitude += 360;
    result.push([longitude, coordinate[1]]);
    previous = longitude;
  }
  return result;
}

function polygonComponents(geometry) {
  if (geometry?.type === 'Polygon') return [geometry.coordinates || []];
  if (geometry?.type === 'MultiPolygon') return geometry.coordinates || [];
  return [];
}

function fallbackTriangulate(vertices, holes = []) {
  if (holes.length || vertices.length < 6) return [];
  const count = vertices.length / 2;
  const result = [];
  for (let index = 1; index < count - 1; index += 1) result.push(0, index, index + 1);
  return result;
}

export function buildPolygonGeometryPacket(geometry, { triangulate = null } = {}) {
  const positions = [];
  const indices = [];
  const ringCoordinates = [];
  const ringOffsets = [0];
  const polygonOffsets = [0];
  for (const component of polygonComponents(geometry)) {
    for (const ring of component || []) {
      for (const point of ring || []) if (finiteCoordinate(point)) ringCoordinates.push(Number(point[0]), Number(point[1]));
      ringOffsets.push(ringCoordinates.length / 2);
    }
    polygonOffsets.push(ringOffsets.length - 1);
    const rings = (component || []).map((ring, index) => unwrapRing(ring, index ? component?.[0]?.[0]?.[0] : null))
      .filter(ring => ring.length >= 3);
    if (!rings.length) continue;
    const vertices = [];
    const holes = [];
    let vertexCount = 0;
    for (let ringIndex = 0; ringIndex < rings.length; ringIndex += 1) {
      if (ringIndex > 0) holes.push(vertexCount);
      for (const coordinate of rings[ringIndex]) {
        vertices.push(coordinate[0], coordinate[1]);
        vertexCount += 1;
      }
    }
    const localIndices = typeof triangulate === 'function'
      ? triangulate(vertices, holes, 2)
      : fallbackTriangulate(vertices, holes);
    if (!localIndices?.length) continue;
    const offset = positions.length / 2;
    positions.push(...vertices);
    for (const index of localIndices) indices.push(offset + Number(index));
  }
  return Object.freeze({
    positions: new Float32Array(positions),
    indices: new Uint32Array(indices),
    featureIds: new Uint32Array(positions.length / 2),
    vertexCount: positions.length / 2,
    triangleCount: indices.length / 3,
    ringCoordinates: new Float64Array(ringCoordinates),
    ringOffsets: new Uint32Array(ringOffsets),
    polygonOffsets: new Uint32Array(polygonOffsets),
  });
}

export function buildStrokeGeometryPacket(geometry) {
  const startsEnds = [];
  for (const segment of buildSelectionBoundarySegments(geometry, { densify: true })) {
    const [start, end] = segment || [];
    if (!finiteCoordinate(start) || !finiteCoordinate(end)) continue;
    const values = [Number(start[0]), Number(start[1]), Number(end[0]), Number(end[1])];
    if (Math.hypot(values[2] - values[0], values[3] - values[1]) <= 1e-12) continue;
    startsEnds.push(...values);
  }
  return Object.freeze({
    startsEnds: new Float32Array(startsEnds),
    segmentCount: startsEnds.length / 4,
  });
}

function normalizedStyle(style = {}) {
  const color = /^#[0-9a-f]{6}$/i.test(String(style.color || '')) ? String(style.color).toLowerCase() : '#000000';
  const casing = style.casing && typeof style.casing === 'object'
    ? Object.freeze({
      color: /^#[0-9a-f]{6}$/i.test(String(style.casing.color || '')) ? String(style.casing.color).toLowerCase() : '#000000',
      alpha: Math.max(0, Math.min(1, Number(style.casing.alpha ?? 1))),
      width: Math.max(0, Number(style.casing.width || 0)),
    })
    : null;
  return Object.freeze({
    color,
    alpha: Math.max(0, Math.min(1, Number(style.alpha ?? 1))),
    width: Math.max(0, Number(style.width ?? 1)),
    fillAlpha: Math.max(0, Math.min(1, Number(style.fillAlpha ?? style.alpha ?? 1))),
    casing,
    cap: style.cap === 'butt' ? 'butt' : 'round',
    join: ['round', 'bevel', 'miter'].includes(style.join) ? style.join : 'round',
    dash: Object.freeze(Array.isArray(style.dash) ? [Math.max(0, Number(style.dash[0]) || 0), Math.max(0, Number(style.dash[1]) || 0)] : [0, 0]),
    miterLimit: Math.max(1, Number(style.miterLimit) || 4),
    blendMode: style.blendMode === 'multiply' ? 'multiply' : 'normal',
  });
}

function packetKey(item, kind, lod = 'high', projection = 'flat') {
  return `${kind}:${String(item?.key || '')}:${String(item?.geometryRevision ?? 0)}:${lod}:${projection}`;
}

function geometryPacketByteLength(packet) {
  return Number(packet?.positions?.byteLength || 0)
    + Number(packet?.indices?.byteLength || 0)
    + Number(packet?.featureIds?.byteLength || 0)
    + Number(packet?.ringCoordinates?.byteLength || 0)
    + Number(packet?.ringOffsets?.byteLength || 0)
    + Number(packet?.polygonOffsets?.byteLength || 0)
    + Number(packet?.startsEnds?.byteLength || 0);
}

function freezePacket(item, geometryPacket, kind, { lod = 'high', projection = 'flat', protected: protectedGeometry = false } = {}) {
  const ownerRanges = item?.ownerRanges && typeof item.ownerRanges === 'object'
    ? Object.freeze(Object.fromEntries(Object.entries(item.ownerRanges).map(([key, range]) => [String(key), Object.freeze({
      first: Math.max(0, Number(range?.first || 0)),
      count: Math.max(0, Number(range?.count || 0)),
    })])))
    : undefined;
  return Object.freeze({
    key: String(item?.key || ''),
    geometryRevision: item?.geometryRevision ?? 0,
    order: Number(item?.order || 0),
    style: normalizedStyle(item?.style),
    blendMode: item?.blendMode === 'multiply' || item?.style?.blendMode === 'multiply' ? 'multiply' : 'normal',
    role: String(item?.role || ''),
    interactionPriority: Number(item?.interactionPriority || 0),
    ownerId: String(item?.ownerId || ''),
    parentId: String(item?.parentId || ''),
    territoryDepth: Number(item?.territoryDepth || 0),
    sourceKey: packetKey(item, kind, lod, projection),
    chunkKey: String(item?.chunkKey || item?.key || ''),
    lod,
    lodPolicy: String(item?.lodPolicy || 'exact'),
    priority: Math.max(Number(item?.priority || 0), protectedGeometry ? 100 : 0),
    protected: item?.protected === true || protectedGeometry,
    byteLength: geometryPacketByteLength(geometryPacket),
    ownerIds: item?.ownerIds ? Object.freeze([...item.ownerIds].map(String)) : undefined,
    ownerRanges,
    ...geometryPacket,
  });
}

export function createRenderSceneBuilder({
  triangulate = null,
  cacheLimit = 4096,
  cacheByteBudget = 192 * 1024 * 1024,
} = {}) {
  const polygonCache = new Map();
  const strokeCache = new Map();
  let polygonCacheBytes = 0;
  let strokeCacheBytes = 0;
  let cacheBudgetBytes = Math.max(1, Number(cacheByteBudget) || 1);
  let cacheEvictionCount = 0;
  let buildCount = 0;
  let patchCount = 0;
  let polygonCacheHits = 0;
  let polygonCacheMisses = 0;
  let strokeCacheHits = 0;
  let strokeCacheMisses = 0;
  let triangulationMs = 0;

  function deleteCacheEntry(cache, key) {
    const packet = cache.get(key);
    if (!packet) return false;
    cache.delete(key);
    const bytes = geometryPacketByteLength(packet);
    if (cache === polygonCache) polygonCacheBytes = Math.max(0, polygonCacheBytes - bytes);
    else strokeCacheBytes = Math.max(0, strokeCacheBytes - bytes);
    cacheEvictionCount += 1;
    return true;
  }

  function trimCaches() {
    while (polygonCache.size + strokeCache.size > cacheLimit
      || polygonCacheBytes + strokeCacheBytes > cacheBudgetBytes) {
      const cache = polygonCacheBytes >= strokeCacheBytes ? polygonCache : strokeCache;
      const fallback = cache === polygonCache ? strokeCache : polygonCache;
      const target = cache.size ? cache : fallback;
      const key = target.keys().next().value;
      if (key === undefined || !deleteCacheEntry(target, key)) break;
    }
  }

  function cacheGeometry(cache, key, packet) {
    if (cache.has(key)) deleteCacheEntry(cache, key);
    cache.set(key, packet);
    const bytes = geometryPacketByteLength(packet);
    if (cache === polygonCache) polygonCacheBytes += bytes;
    else strokeCacheBytes += bytes;
    trimCaches();
  }

  function packetContext(item, input = {}) {
    const protectedKeys = input.protectedKeys instanceof Set
      ? input.protectedKeys
      : new Set([...(input.protectedKeys || [])].map(String));
    const protectedGeometry = item?.protected === true || protectedKeys.has(String(item?.objectKey || item?.key || ''));
    return {
      lod: resolveRenderLod({
        requested: input.renderQuality?.backgroundLod || input.quality?.backgroundLod || 'high',
        policy: item?.lodPolicy || 'exact',
        protected: protectedGeometry,
      }),
      projection: input.projection === 'globe' ? 'globe' : 'flat',
      protected: protectedGeometry,
    };
  }

  function polygonPacket(item, input = {}) {
    const context = packetContext(item, input);
    const key = packetKey(item, 'polygon', context.lod, context.projection);
    const cached = polygonCache.get(key);
    if (cached) {
      polygonCache.delete(key);
      polygonCache.set(key, cached);
      polygonCacheHits += 1;
      return freezePacket(item, cached, 'polygon', context);
    }
    const started = performance.now();
    const renderGeometry = simplifyRenderGeometry(item?.geometry, {
      lod: context.lod,
      policy: item?.lodPolicy || 'exact',
      projection: context.projection,
    });
    let geometryPacket = buildPolygonGeometryPacket(renderGeometry, { triangulate });
    if (!geometryPacket.triangleCount && renderGeometry !== item?.geometry) {
      geometryPacket = buildPolygonGeometryPacket(item?.geometry, { triangulate });
    }
    triangulationMs += performance.now() - started;
    cacheGeometry(polygonCache, key, geometryPacket);
    polygonCacheMisses += 1;
    return freezePacket(item, geometryPacket, 'polygon', context);
  }

  function strokePacket(item, input = {}) {
    const context = packetContext(item, input);
    const key = packetKey(item, 'stroke', context.lod, context.projection);
    const cached = strokeCache.get(key);
    if (cached) {
      strokeCache.delete(key);
      strokeCache.set(key, cached);
      strokeCacheHits += 1;
      return freezePacket(item, cached, 'stroke', context);
    }
    const geometryPacket = item?.startsEnds instanceof Float32Array
      ? Object.freeze({ startsEnds: item.startsEnds, segmentCount: Number(item.segmentCount || item.startsEnds.length / 4) })
      : buildStrokeGeometryPacket(simplifyRenderGeometry(item?.geometry, {
        lod: context.lod,
        policy: item?.lodPolicy || 'exact',
        projection: context.projection,
      }));
    cacheGeometry(strokeCache, key, geometryPacket);
    strokeCacheMisses += 1;
    return freezePacket(item, geometryPacket, 'stroke', context);
  }

  function build(input = {}) {
    buildCount += 1;
    const polygons = (input.polygons || []).filter(item => item?.key && item?.geometry)
      .map(item => polygonPacket(item, input)).filter(packet => packet.triangleCount > 0)
      .sort((left, right) => left.order - right.order);
    const strokes = (input.strokes || []).filter(item => item?.key && (item?.geometry || item?.startsEnds))
      .map(item => strokePacket(item, input)).filter(packet => packet.segmentCount > 0)
      .sort((left, right) => left.order - right.order);
    const revisions = Object.freeze({ ...EMPTY_REVISIONS, ...(input.revisions || {}) });
    return Object.freeze({
      revision: Number(input.revision || buildCount),
      revisions,
      country: Object.freeze({ ...(input.country || {}) }),
      physical: Object.freeze({ ...(input.physical || {}) }),
      quality: Object.freeze({ ...(input.renderQuality || input.quality || {}) }),
      polygons: Object.freeze(polygons),
      strokes: Object.freeze(strokes),
      interaction: Object.freeze({
        selectionPacket: input.interaction?.selectionPacket || null,
        genericFillKeys: Object.freeze([...(input.interaction?.genericFillKeys || [])].map(String)),
        genericFillItems: Object.freeze([...(input.interaction?.genericFillItems || [])]),
        previewPackets: Object.freeze([...(input.interaction?.previewPackets || [])]),
        draftPackets: Object.freeze([...(input.interaction?.draftPackets || [])]),
      }),
    });
  }

  function patch(previous, input = {}) {
    if (!isRenderScene(previous)) return build(input);
    patchCount += 1;
    const polygonMap = new Map(previous.polygons.map(packet => [String(packet.key), packet]));
    const strokeMap = new Map(previous.strokes.map(packet => [String(packet.key), packet]));
    for (const key of input.removePolygonKeys || []) polygonMap.delete(String(key));
    for (const key of input.removeStrokeKeys || []) strokeMap.delete(String(key));
    for (const item of input.polygons || []) {
      if (!item?.key || !item?.geometry) continue;
      const packet = polygonPacket(item, input);
      if (packet.triangleCount > 0) polygonMap.set(String(packet.key), packet);
      else polygonMap.delete(String(item.key));
    }
    for (const item of input.strokes || []) {
      if (!item?.key || !(item?.geometry || item?.startsEnds)) continue;
      const packet = strokePacket(item, input);
      if (packet.segmentCount > 0) strokeMap.set(String(packet.key), packet);
      else strokeMap.delete(String(item.key));
    }
    const revisions = Object.freeze({ ...previous.revisions, ...(input.revisions || {}) });
    const interactionInput = input.interaction || previous.interaction;
    return Object.freeze({
      revision: Number(input.revision || previous.revision + 1),
      revisions,
      country: Object.freeze({ ...previous.country, ...(input.country || {}) }),
      physical: Object.freeze({ ...previous.physical, ...(input.physical || {}) }),
      quality: Object.freeze({ ...previous.quality, ...(input.renderQuality || input.quality || {}) }),
      polygons: Object.freeze([...polygonMap.values()].sort((left, right) => left.order - right.order)),
      strokes: Object.freeze([...strokeMap.values()].sort((left, right) => left.order - right.order)),
      interaction: Object.freeze({
        selectionPacket: interactionInput?.selectionPacket || null,
        genericFillKeys: Object.freeze([...(interactionInput?.genericFillKeys || [])].map(String)),
        genericFillItems: Object.freeze([...(interactionInput?.genericFillItems || [])]),
        previewPackets: Object.freeze([...(interactionInput?.previewPackets || [])]),
        draftPackets: Object.freeze([...(interactionInput?.draftPackets || [])]),
      }),
    });
  }

  function invalidateGeometry(key) {
    const needle = `:${String(key || '')}:`;
    for (const cache of [polygonCache, strokeCache]) {
      for (const entryKey of [...cache.keys()]) if (entryKey.includes(needle)) deleteCacheEntry(cache, entryKey);
    }
  }

  return Object.freeze({
    build,
    patch,
    invalidateGeometry,
    setCacheByteBudget: value => {
      cacheBudgetBytes = Math.max(1, Number(value) || 1);
      trimCaches();
      return cacheBudgetBytes;
    },
    clear: () => {
      polygonCache.clear(); strokeCache.clear();
      polygonCacheBytes = 0; strokeCacheBytes = 0;
    },
    stats: () => Object.freeze({
      buildCount,
      patchCount,
      polygonCacheSize: polygonCache.size,
      strokeCacheSize: strokeCache.size,
      polygonCacheHits,
      polygonCacheMisses,
      strokeCacheHits,
      strokeCacheMisses,
      triangulationMs,
      cacheBudgetBytes,
      cacheBytes: polygonCacheBytes + strokeCacheBytes,
      polygonCacheBytes,
      strokeCacheBytes,
      cacheEvictionCount,
    }),
  });
}

export function isRenderScene(value) {
  return !!value && Number.isFinite(Number(value.revision))
    && Array.isArray(value.polygons) && Array.isArray(value.strokes)
    && !!value.revisions && !!value.interaction;
}
