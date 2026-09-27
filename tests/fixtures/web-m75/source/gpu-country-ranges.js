import { visibleSpatialBlockRanges } from './mesh-spatial-blocks.js';

const COUNTRY_BOUNDS_FLAG_DATELINE = 1;
const COUNTRY_BOUNDS_FLAG_FULL_LONGITUDE = 2;
const COUNTRY_BOUNDS_SCALE = 1e-6;
const COUNTRY_CULLING_PADDING_PIXELS = 64;
const COUNTRY_CULLING_FULL_RANGE_THRESHOLD = 0.7;
const COUNTRY_CULLING_MAX_RANGES = 96;

function fullCountryDrawRange(indexCount) {
  const count = Math.max(0, Number(indexCount || 0));
  return count ? [{ first: 0, count }] : [];
}

export function mergeCountryDrawRanges(ranges = []) {
  const normalized = ranges
    .map(range => ({
      first: Math.max(0, Number(range?.first || 0)),
      count: Math.max(0, Number(range?.count || 0)),
    }))
    .filter(range => range.count > 0)
    .sort((left, right) => left.first - right.first);
  const merged = [];
  for (const range of normalized) {
    const previous = merged[merged.length - 1];
    if (previous && range.first <= previous.first + previous.count) {
      previous.count = Math.max(previous.first + previous.count, range.first + range.count) - previous.first;
    } else {
      merged.push({ ...range });
    }
  }
  return merged;
}

export function createCountryTriangleRangeMap(sourceMesh, countryIds = []) {
  const ranges = new Map();
  const metadataRanges = sourceMesh?.countryTriangleRanges;
  const metadataIds = sourceMesh?.metadataCountryIds;
  if (metadataRanges?.length === metadataIds?.length * 2) {
    for (let countryIndex = 0; countryIndex < metadataIds.length; countryIndex += 1) {
      const count = Number(metadataRanges[countryIndex * 2 + 1] || 0);
      if (!count) continue;
      ranges.set(String(metadataIds[countryIndex]), Object.freeze([Object.freeze({
        first: Number(metadataRanges[countryIndex * 2] || 0),
        count,
      })]));
    }
    return ranges;
  }
  const indices = sourceMesh?.triangleIndices || [];
  let activeId = '';
  let activeRange = null;
  for (let first = 0; first + 2 < indices.length; first += 3) {
    const vertexIndex = Number(indices[first]);
    const countryIndex = Number(sourceMesh.countryIndices?.[vertexIndex]);
    const countryId = String(countryIds?.[countryIndex] || '');
    if (!countryId) {
      activeId = '';
      activeRange = null;
      continue;
    }
    if (countryId === activeId && activeRange && activeRange.first + activeRange.count === first) {
      activeRange.count += 3;
      continue;
    }
    activeId = countryId;
    activeRange = { first, count: 3 };
    if (!ranges.has(countryId)) ranges.set(countryId, []);
    ranges.get(countryId).push(activeRange);
  }
  for (const [countryId, countryRanges] of ranges) {
    ranges.set(countryId, Object.freeze(countryRanges.map(range => Object.freeze({ ...range }))));
  }
  return ranges;
}

function longitudeIntervals(west, east, flags) {
  if ((flags & COUNTRY_BOUNDS_FLAG_FULL_LONGITUDE) !== 0) return [[-180, 180]];
  if ((flags & COUNTRY_BOUNDS_FLAG_DATELINE) !== 0 || west > east) return [[west, 180], [-180, east]];
  return [[west, east]];
}

function longitudeSpanDegrees(west, east, flags) {
  if ((flags & COUNTRY_BOUNDS_FLAG_FULL_LONGITUDE) !== 0) return 360;
  return west <= east ? east - west : 360 - west + east;
}

function longitudeMidpointDegrees(west, east, flags) {
  if ((flags & COUNTRY_BOUNDS_FLAG_FULL_LONGITUDE) !== 0) return 0;
  const span = longitudeSpanDegrees(west, east, flags);
  const midpoint = west + span / 2;
  return midpoint > 180 ? midpoint - 360 : midpoint;
}

function flatBoundsIntersectViewport(bounds, flags, frameContext, paddingPixels) {
  const [west, south, east, north] = bounds;
  const width = Math.max(1, Number(frameContext.cssViewport?.[0] || 0));
  const height = Math.max(1, Number(frameContext.cssViewport?.[1] || 0));
  const translate = frameContext.cssTranslate || [width / 2, height / 2];
  const scale = Math.abs(Number(frameContext.cssScale || 0));
  const center = frameContext.flatCenter || [0, 0];
  if (!scale) return true;
  const minY = translate[1] - scale * (north * Math.PI / 180 - center[1]);
  const maxY = translate[1] - scale * (south * Math.PI / 180 - center[1]);
  if (maxY < -paddingPixels || minY > height + paddingPixels) return false;
  for (const [intervalWest, intervalEast] of longitudeIntervals(west, east, flags)) {
    for (const worldOffset of frameContext.worldOffsets || [0]) {
      const minX = translate[0] + scale * (intervalWest * Math.PI / 180 + worldOffset - center[0]);
      const maxX = translate[0] + scale * (intervalEast * Math.PI / 180 + worldOffset - center[0]);
      if (maxX >= -paddingPixels && minX <= width + paddingPixels) return true;
    }
  }
  return false;
}

function globeBoundsIntersectViewport(bounds, flags, frameContext, paddingPixels) {
  const [west, south, east, north] = bounds;
  if ((flags & COUNTRY_BOUNDS_FLAG_FULL_LONGITUDE) !== 0) return true;
  const width = Math.max(1, Number(frameContext.cssViewport?.[0] || 0));
  const height = Math.max(1, Number(frameContext.cssViewport?.[1] || 0));
  const translate = frameContext.cssTranslate || [width / 2, height / 2];
  const scale = Math.abs(Number(frameContext.cssScale || 0));
  const lon = longitudeMidpointDegrees(west, east, flags) * Math.PI / 180;
  const lat = ((south + north) / 2) * Math.PI / 180;
  const vector = [Math.cos(lat) * Math.cos(lon), Math.cos(lat) * Math.sin(lon), Math.sin(lat)];
  const rowX = frameContext.rowX || [1, 0, 0];
  const rowY = frameContext.rowY || [0, 1, 0];
  const rowZ = frameContext.rowZ || [0, 0, 1];
  const dot = (row, point) => row[0] * point[0] + row[1] * point[1] + row[2] * point[2];
  const longitudeRadius = longitudeSpanDegrees(west, east, flags) * Math.PI / 360;
  const latitudeRadius = Math.max(0, north - south) * Math.PI / 360;
  const angularRadius = Math.min(Math.PI, Math.hypot(longitudeRadius, latitudeRadius) + 2 * Math.PI / 180);
  if (angularRadius >= Math.PI / 2) return true;
  const paddingAngle = scale > 0 ? paddingPixels / scale : 0;
  const centerDepth = Math.max(-1, Math.min(1, dot(rowZ, vector)));
  if (Math.acos(centerDepth) > Math.PI / 2 + angularRadius + paddingAngle) return false;
  const screenX = translate[0] + scale * dot(rowX, vector);
  const screenY = translate[1] + scale * dot(rowY, vector);
  const radiusPixels = scale * 2 * Math.sin(angularRadius / 2) + paddingPixels;
  return screenX + radiusPixels >= 0
    && screenX - radiusPixels <= width
    && screenY + radiusPixels >= 0
    && screenY - radiusPixels <= height;
}

const countryVisibilityFrames = new WeakMap();
function countryVisibilityForFrame(sourceMesh, frameContext, paddingPixels, countryCount) {
  let meshes = countryVisibilityFrames.get(frameContext);
  if (!meshes) countryVisibilityFrames.set(frameContext, meshes = new WeakMap());
  let cached = meshes.get(sourceMesh);
  if (cached?.paddingPixels === paddingPixels) return cached.visible;
  const visible = new Uint8Array(countryCount);
  for (let i = 0; i < countryCount; i += 1) {
    const bounds = Array.from(sourceMesh.countryBounds.subarray(i * 4, i * 4 + 4), value => value * COUNTRY_BOUNDS_SCALE);
    const flags = Number(sourceMesh.countryBoundsFlags[i] || 0);
    visible[i] = frameContext.mode === 0
      ? globeBoundsIntersectViewport(bounds, flags, frameContext, paddingPixels)
      : flatBoundsIntersectViewport(bounds, flags, frameContext, paddingPixels);
  }
  meshes.set(sourceMesh, { paddingPixels, visible });
  return visible;
}

export function countryDrawRangesForFrame(sourceMesh, frameContext, {
  kind = 'triangle',
  paddingPixels = COUNTRY_CULLING_PADDING_PIXELS,
  fullRangeThreshold = COUNTRY_CULLING_FULL_RANGE_THRESHOLD,
  maxRanges = COUNTRY_CULLING_MAX_RANGES,
  includeCountry = null,
} = {}) {
  const indexCount = Number(kind === 'boundary'
    ? sourceMesh?.lineIndices?.length
    : sourceMesh?.triangleIndices?.length) || 0;
  const fullRanges = fullCountryDrawRange(indexCount);
  const rangeData = kind === 'boundary' ? sourceMesh?.countryBoundaryRanges : sourceMesh?.countryTriangleRanges;
  const boundsData = sourceMesh?.countryBounds;
  const flagsData = sourceMesh?.countryBoundsFlags;
  const countryIds = sourceMesh?.metadataCountryIds;
  const countryCount = Array.isArray(countryIds) ? countryIds.length : Number(rangeData?.length || 0) / 2;
  const invalidMetadata = !frameContext
    || !rangeData || rangeData.length !== countryCount * 2
    || !boundsData || boundsData.length !== countryCount * 4
    || !flagsData || flagsData.length !== countryCount;
  if (!indexCount || invalidMetadata) {
    return { ranges: fullRanges, culled: false, visibleCountryCount: countryCount, indexCount, fullIndexCount: indexCount, fallback: true };
  }
  const width = Math.max(1, Number(frameContext.cssViewport?.[0] || 0));
  const height = Math.max(1, Number(frameContext.cssViewport?.[1] || 0));
  const scale = Math.abs(Number(frameContext.cssScale || 0));
  const worldVisible = frameContext.mode !== 0
    && scale * 2 * Math.PI <= width + paddingPixels * 2 && scale * Math.PI <= height + paddingPixels * 2;
  if (worldVisible && typeof includeCountry !== 'function') {
    return { ranges: fullRanges, culled: false, visibleCountryCount: countryCount, indexCount, fullIndexCount: indexCount, fallback: false };
  }
  const candidates = [];
  const blocks = typeof includeCountry !== 'function' ? visibleSpatialBlockRanges(sourceMesh, frameContext, kind, paddingPixels, maxRanges) : null;
  if (blocks) {
    let submittedCountries = 0;
    for (let index = 0; index < countryCount; index++) {
      const first = rangeData[index * 2], count = rangeData[index * 2 + 1];
      if (count && blocks.some(range => range.first < first + count && range.first + range.count > first)) submittedCountries++;
    }
    return { ranges: blocks, culled: true, visibleCountryCount: submittedCountries, indexCount: blocks.reduce((sum, r) => sum + r.count, 0), fullIndexCount: indexCount, fallback: false };
  }
  const visibility = countryVisibilityForFrame(sourceMesh, frameContext, paddingPixels, countryCount);
  let visibleCountryCount = 0;
  for (let countryIndex = 0; countryIndex < countryCount; countryIndex += 1) {
    const countryId = String(countryIds?.[countryIndex] ?? countryIndex);
    if (typeof includeCountry === 'function' && !includeCountry(countryId, countryIndex)) continue;
    const range = {
      first: Number(rangeData[countryIndex * 2] || 0),
      count: Number(rangeData[countryIndex * 2 + 1] || 0),
    };
    if (!range.count) continue;
    if (!visibility[countryIndex]) continue;
    visibleCountryCount += 1;
    candidates.push(range);
  }
  const merged = mergeCountryDrawRanges(candidates);
  const visibleIndexCount = merged.reduce((sum, range) => sum + range.count, 0);
  // A globe's conservative country bounds often retain most indices. Do not
  // undo its rear-hemisphere rejection merely because that fraction is high.
  if ((frameContext.mode !== 0 && visibleIndexCount >= indexCount * Math.max(0, Number(fullRangeThreshold) || 0)) || merged.length > Math.max(1, Number(maxRanges) || 1)) {
    return { ranges: fullRanges, culled: false, visibleCountryCount, indexCount, fullIndexCount: indexCount, fallback: true };
  }
  return { ranges: merged, culled: true, visibleCountryCount, indexCount: visibleIndexCount, fullIndexCount: indexCount, fallback: false };
}
