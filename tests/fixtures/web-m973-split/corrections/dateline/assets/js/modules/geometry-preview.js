import { geometryAreaKm2, percentChange } from './geometry-metrics.js';
import { wrapPolygonGeometry, hasCanonicalPolygonWinding, multiCoordinates, normalizeClippedPolygonGeometry } from './map-edit-geometry.js';
import {
  buildRenderableBoundaryGeometry,
  buildRenderableBoundarySegments,
  buildRenderableStrokeFeature,
  densifyBoundarySegmentsForProjection,
  isArtificialBoundaryEdge,
  MAX_BOUNDARY_EDGE_DEGREES,
  normalizeLongitude,
  normalizeLongitudeNear,
} from './geographic-boundary.js';

export {
  buildRenderableBoundaryGeometry,
  buildRenderableBoundarySegments,
  buildRenderableStrokeFeature,
  densifyBoundarySegmentsForProjection,
  isArtificialBoundaryEdge,
  MAX_BOUNDARY_EDGE_DEGREES,
  normalizeLongitude,
  normalizeLongitudeNear,
};

const clone = value => value == null ? value : structuredClone(value);
const idOf = feature => String(feature?.id || '');
function geometryHasArea(geometry) {
  if (!geometry) return false;
  if (geometry.type === 'Polygon' || geometry.type === 'MultiPolygon') return true;
  return geometry.type === 'GeometryCollection' && (geometry.geometries || []).some(geometryHasArea);
}

export function hasAreaGeometry(value) {
  if (value?.type === 'FeatureCollection') return (value.features || []).some(hasAreaGeometry);
  return geometryHasArea(value?.type === 'Feature' ? value.geometry : value);
}

export function createGeometryPreviewState() {
  return { revision: 0, session: null };
}

export function beginGeometryPreview(state, raw = {}) {
  state.revision += 1;
  state.session = {
    sessionId: raw.sessionId || `geometry-preview-${state.revision}`,
    operation: String(raw.operation || ''),
    baseDataRevision: Number(raw.baseDataRevision || 0),
    workerRequestId: Number(raw.workerRequestId || 0),
    affectedIds: [...new Set((raw.affectedIds || []).map(String))],
    beforeFeatures: clone(raw.beforeFeatures || []),
    afterFeatures: clone(raw.afterFeatures || []),
    removedIds: [...new Set((raw.removedIds || []).map(String))],
    delta: clone(raw.delta || null),
    metrics: clone(raw.metrics || null),
    validation: clone(raw.validation || { issues: [], blocking: false }),
    status: raw.status || 'ready',
    revision: state.revision,
  };
  return state.session;
}

export function clearGeometryPreview(state) {
  const previous = state.session;
  state.revision += 1;
  state.session = null;
  return previous;
}

export function previewIsCurrent(state, sessionId, baseDataRevision) {
  return !!state.session && state.session.sessionId === sessionId
    && Number(state.session.baseDataRevision) === Number(baseDataRevision);
}

function featureMap(features) {
  return new Map((features || []).map(feature => [idOf(feature), feature]).filter(([id]) => id));
}

function unionGeometry(features, clipper) {
  if (!features.length || !clipper?.union) return null;
  const coordinates = clipper.union(...features.map(feature => multiCoordinates(wrapPolygonGeometry(feature.geometry, clipper))));
  return clippedPreviewGeometry(coordinates);
}

function clippedPreviewGeometry(coordinates) {
  if (!coordinates?.length) return null;
  const normalized = normalizeClippedPolygonGeometry(coordinates);
  if (!normalized) throw new Error('미리보기 계산 형상이 유효하지 않습니다.');
  return { type: 'MultiPolygon', coordinates: multiCoordinates(normalized) };
}

export function buildGeometryPreview({ operation, beforeFeatures = [], afterFeatures = [], removedIds = [], clipper = null, transferredGeometry = null } = {}) {
  const beforeById = featureMap(beforeFeatures);
  const afterById = featureMap(afterFeatures);
  const beforeUnion = unionGeometry(beforeFeatures, clipper);
  const afterUnion = unionGeometry(afterFeatures, clipper);
  let removedGeometry = null;
  let addedGeometry = null;
  if (operation !== 'annex' && clipper?.difference && beforeUnion && afterUnion) {
    const removed = clipper.difference(beforeUnion.coordinates, afterUnion.coordinates);
    const added = clipper.difference(afterUnion.coordinates, beforeUnion.coordinates);
    removedGeometry = clippedPreviewGeometry(removed);
    addedGeometry = clippedPreviewGeometry(added);
  }
  if (operation === 'annex') {
    if (!transferredGeometry || !['Polygon', 'MultiPolygon'].includes(transferredGeometry.type)) {
      throw new Error('편입 미리보기의 검증된 형상이 없습니다.');
    }
    if (!hasCanonicalPolygonWinding(transferredGeometry)) throw new Error('편입 미리보기 형상이 canonical 경계 방향 계약을 지키지 않습니다.');
    removedGeometry = clone(transferredGeometry);
    addedGeometry = clone(transferredGeometry);
  } else if (transferredGeometry) {
    removedGeometry ||= clone(transferredGeometry);
    addedGeometry ||= clone(transferredGeometry);
  }
  if (operation === 'merge' && afterUnion) addedGeometry = clone(afterUnion);
  const perEntity = [...new Set([...beforeById.keys(), ...afterById.keys()])].map(id => {
    const before = geometryAreaKm2(beforeById.get(id)?.geometry);
    const after = geometryAreaKm2(afterById.get(id)?.geometry);
    return { id, before, after, delta: after - before, percent: percentChange(before, after) };
  });
  return {
    delta: {
      removedGeometry,
      addedGeometry,
      beforeUnion,
      afterUnion,
      newBoundaries: afterFeatures.map(feature => clone(feature.geometry)),
      oldBoundaries: beforeFeatures.map(feature => clone(feature.geometry)),
    },
    metrics: {
      operation,
      removedCountryCount: removedIds.length,
      finalAreaKm2: geometryAreaKm2(afterUnion),
      transferredAreaKm2: geometryAreaKm2(transferredGeometry),
      perEntity,
    },
  };
}
