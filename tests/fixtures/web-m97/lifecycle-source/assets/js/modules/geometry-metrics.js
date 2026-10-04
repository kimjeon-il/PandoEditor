export const MEAN_EARTH_RADIUS_KM = 6371.0088;

const radians = value => Number(value || 0) * Math.PI / 180;
const clamp = (value, min, max) => Math.max(min, Math.min(max, value));

export function distanceKm(a, b) {
  if (!Array.isArray(a) || !Array.isArray(b)) return 0;
  const lat1 = radians(a[1]);
  const lat2 = radians(b[1]);
  const deltaLat = lat2 - lat1;
  let deltaLon = radians(b[0]) - radians(a[0]);
  if (deltaLon > Math.PI) deltaLon -= Math.PI * 2;
  if (deltaLon < -Math.PI) deltaLon += Math.PI * 2;
  const haversine = Math.sin(deltaLat / 2) ** 2
    + Math.cos(lat1) * Math.cos(lat2) * Math.sin(deltaLon / 2) ** 2;
  return MEAN_EARTH_RADIUS_KM * 2 * Math.asin(Math.sqrt(clamp(haversine, 0, 1)));
}

export function lineDistanceKm(coordinates = []) {
  let total = 0;
  for (let index = 1; index < coordinates.length; index += 1) {
    total += distanceKm(coordinates[index - 1], coordinates[index]);
  }
  return total;
}

function ringAreaSteradians(ring = []) {
  if (ring.length < 3) return 0;
  let sum = 0;
  const limit = ring.length > 1 && ring[0][0] === ring[ring.length - 1][0] && ring[0][1] === ring[ring.length - 1][1]
    ? ring.length - 1
    : ring.length;
  for (let index = 0; index < limit; index += 1) {
    const current = ring[index];
    const next = ring[(index + 1) % limit];
    let deltaLon = radians(next[0]) - radians(current[0]);
    if (deltaLon > Math.PI) deltaLon -= Math.PI * 2;
    if (deltaLon < -Math.PI) deltaLon += Math.PI * 2;
    sum += deltaLon * (2 + Math.sin(radians(current[1])) + Math.sin(radians(next[1])));
  }
  let area = Math.abs(sum / 2);
  if (area > Math.PI * 2) area = Math.PI * 4 - area;
  return Math.max(0, area);
}

function polygonAreaSteradians(polygon = []) {
  if (!polygon.length) return 0;
  const outer = ringAreaSteradians(polygon[0]);
  const holes = polygon.slice(1).reduce((sum, ring) => sum + ringAreaSteradians(ring), 0);
  return Math.max(0, outer - holes);
}

export function geometryAreaKm2(geometry) {
  if (!geometry) return 0;
  const polygons = geometry.type === 'Polygon'
    ? [geometry.coordinates || []]
    : geometry.type === 'MultiPolygon'
      ? geometry.coordinates || []
      : [];
  const steradians = polygons.reduce((sum, polygon) => sum + polygonAreaSteradians(polygon), 0);
  return steradians * MEAN_EARTH_RADIUS_KM * MEAN_EARTH_RADIUS_KM;
}

export function formatArea(valueKm2, locale = 'ko-KR', { approximate = true } = {}) {
  const value = Math.max(0, Number(valueKm2 || 0));
  const digits = value < 1 ? 2 : value < 100 ? 1 : 0;
  return `${approximate ? '약 ' : ''}${value.toLocaleString(locale, { maximumFractionDigits: digits })} km²`;
}

export function percentChange(before, after) {
  const base = Number(before || 0);
  if (!(base > 0)) return null;
  return (Number(after || 0) - base) / base * 100;
}
