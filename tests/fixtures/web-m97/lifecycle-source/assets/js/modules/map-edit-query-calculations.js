import { normalizePolygonGeometry, multiCoordinates, featureId } from './map-edit-geometry.js';
import './territorial-edit-plan.js';
import { analyzeAdminCountryCoast } from './coast-reconciliation.js';

export function calculateUncoveredSource(parent, children, clipper) {
  if (!parent) throw new Error('상위 단위를 찾을 수 없습니다.');
  const occupied = children.length ? clipper.union(...children.map(feature => multiCoordinates(feature.geometry))) : [];
  return { geometry: normalizePolygonGeometry({ type: 'MultiPolygon', coordinates: occupied.length
    ? clipper.difference(multiCoordinates(parent.geometry), occupied) : multiCoordinates(parent.geometry) }) };
}

export function calculateCoastAvailability(unit, country, countryTopology, clipper) {
  const kernel = globalThis.PandoLabTerritorialEdit.createKernel(clipper);
  const coastal = [...countryTopology.segments.values()].some(segment => segment.kind === 'coast'
    && segment.ownerIds.has(featureId(country)) && kernel.adjacent(unit.geometry,
      { type: 'Polygon', coordinates: [[segment.a, segment.b]] }));
  const analysis = analyzeAdminCountryCoast({ adminFeature: unit, countryFeature: country, countryTopology });
  return { coastal, reconciliation: analysis.status !== 'unavailable' && !!analysis.conflicts?.length };
}
