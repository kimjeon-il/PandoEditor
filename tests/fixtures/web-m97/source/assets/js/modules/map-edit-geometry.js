import './polygon-geometry.js';
import { coordinateBounds } from './coordinate-bounds.js';

const { normalizePolygonGeometry, ringSignedArea, hasCanonicalPolygonWinding } = globalThis.PandoLabPolygonGeometry;
const clone = value => value == null ? value : JSON.parse(JSON.stringify(value));
const featureId = feature => String(feature?.id || '');

function multiCoordinates(geometry) {
  if (!geometry) return [];
  if (geometry.type === 'Polygon') return [geometry.coordinates || []];
  return geometry.type === 'MultiPolygon' ? geometry.coordinates || [] : [];
}

function area(value) {
  const polygons = value?.type ? multiCoordinates(value) : (Array.isArray(value) ? value : []);
  return polygons.reduce((total, polygon) => total + Math.max(0,
    Math.abs(ringSignedArea(polygon[0] || [])) - (polygon || []).slice(1).reduce((sum, ring) => sum + Math.abs(ringSignedArea(ring)), 0),
  ), 0);
}

function geometryBounds(geometry) {
  return coordinateBounds(geometry?.coordinates);
}

function boundsOverlap(left, right) {
  return left[0] <= right[2] && left[2] >= right[0] && left[1] <= right[3] && left[3] >= right[1];
}

function polygonBounds(polygon) {
  return geometryBounds({ type: 'Polygon', coordinates: polygon });
}

export { normalizePolygonGeometry, ringSignedArea, hasCanonicalPolygonWinding, clone, featureId, multiCoordinates, area, geometryBounds, boundsOverlap, polygonBounds };
