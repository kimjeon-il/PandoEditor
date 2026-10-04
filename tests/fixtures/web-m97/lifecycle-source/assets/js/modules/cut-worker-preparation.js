import { createCutGeometry } from './app-cut-geometry.js';
import { createCountryValidation } from './app-country-validation.js';
import { createLandRelations } from './app-land-relations.js';
import { createTerritoryComponents } from './app-territory-components.js';
import { createRingHitTester } from './ring-hit-test.js';
import { snapLineEndpointsToBoundary } from './territorial-geometry.js';

export function prepareCutInWorker({ source, coords, view, buildPreview }, geometryApi, d3, clipper) {
  const projection = (view.kind === 'globe' ? d3.geo.orthographic() : d3.geo.equirectangular())
    .scale(view.scale).translate(view.translate).rotate(view.rotate).center(view.center);
  const validation = createCountryValidation();
  validation.connect({ geometryModel: geometryApi });
  const land = createLandRelations();
  land.connect({ territorialModel: { createRingHitTester }, geometryModel: geometryApi });
  land.initializeRingHitTester();
  const components = createTerritoryComponents();
  components.connect({ geometryModel: geometryApi });
  const cut = createCutGeometry();
  cut.connect({
    geometryModel: { ...geometryApi, snapLineEndpointsToBoundary },
    geometryPreview: {
      coordKey: (coordinate, precision = 7) => coordinate.map(value => Number(value).toFixed(precision)).join(','),
      coordNear: (a, b, tolerance = 0.00008) => !!a && !!b && Math.min(Math.abs(a[0] - b[0]), Math.abs(Math.abs(a[0] - b[0]) - 360)) <= tolerance && Math.abs(a[1] - b[1]) <= tolerance,
      geometryPolygonSets: components.geometryMultiCoordinates,
    },
    geometryValidation: {
      CUT_ENDPOINT_SNAP_DISTANCE: view.snapDistance,
      interpolateCoordinate: validation.interpolateCoordinate,
      segmentsProperlyIntersect: validation.segmentsProperlyIntersect,
      ringHasSelfIntersection: validation.ringHasSelfIntersection,
    },
    territoryGeometry: {
      geometryMultiCoordinates: components.geometryMultiCoordinates,
      multiPolygonPlanarArea: components.multiPolygonPlanarArea,
      pointOnSegment: land.pointOnSegment,
      pointInRing: land.pointInRing,
    },
    platform: { polygonClipping: clipper, coarsePointer: view.coarsePointer,
      clamp: (value, min, max) => Math.max(min, Math.min(max, value)) },
    projectState: { state: { size: view.size } },
    mapView: { activeProjection: () => projection, isCoordVisible: coordinate => {
      const p = projection(coordinate);
      if (!p?.every(Number.isFinite)) return false;
      if (view.kind === 'globe') return d3.geo.distance(coordinate, [-view.rotate[0], -view.rotate[1]]) <= Math.PI / 2 + 0.005;
      return p[0] >= -30 && p[0] <= view.size.width + 30 && p[1] >= -30 && p[1] <= view.size.height + 30;
    } },
  });
  const result = cut.assessCutDraft(coords, source);
  if (result.valid && buildPreview) {
    try { result.split = cut.buildCutSplitCandidates(source, coords, result); }
    catch (error) { result.split = null; result.splitError = error.message; result.valid = false; result.status = 'invalid'; result.message = error.message; result.issues = error.cutIssue ? [error.cutIssue] : []; }
  }
  delete result.extracted; // Graph preparation is Worker-private; publish only display candidates.
  return result;
}
