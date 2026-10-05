import { tracePlanarGraphFaces } from './planar-graph-faces.js';
import { geometryRevision } from './geometry-versions.js';
import { geometrySegmentIndex, segmentQueryBounds } from './geometry-segment-index.js';
import { preparedCut } from './cut-preparation-cache.js';
import { coordinateBounds as calculateCoordinateBounds } from './coordinate-bounds.js';
/** CutGeometry: extracted application responsibility.
 * Dependencies are explicitly wired once by the composition modules.
 * Mutable bindings stay local; exported accessors retain live identity.
 */
const polygonIndexGeometries = new WeakMap();
const polygonCutEvents = new WeakMap();
const sourceRevisions = new WeakMap();
function prepareSourceIndexes(source) {
  if (!source || sourceRevisions.get(source) === geometryRevision(source)) return;
  const polygons = source.type === 'Polygon' ? [source.coordinates] : source.coordinates || [];
  for (const polygon of polygons) { polygonIndexGeometries.delete(polygon); polygonCutEvents.delete(polygon); }
  sourceRevisions.set(source, geometryRevision(source));
}
export function createCutGeometry() {
  let dependencies;

  function connect(ports) {
    if (dependencies) throw new Error('cut-geometry already connected');
    dependencies = ports;
  }

  function coordinateBounds(value, bounds = [Infinity, Infinity, -Infinity, -Infinity]) {
    return calculateCoordinateBounds(value, bounds);
  }

  function boundsOverlap(a, b) {
    return a[0] <= b[2] && a[2] >= b[0] && a[1] <= b[3] && a[3] >= b[1];
  }

  function normalizeClippedLandGeometry(multiPolygon) {
    return (0, dependencies.geometryModel.normalizePolygonGeometry)(multiPolygon);
  }

  function pointOnRingBoundary(point, rawRing, tolerance = 1e-7) {
    const ring = (0, dependencies.geometryModel.ensureClosedRing)(rawRing);
    for (let i = 0; i < ring.length - 1; i += 1) {
      if ((0, dependencies.territoryGeometry.pointOnSegment)(point, ring[i], ring[i + 1], tolerance)) return true;
    }
    return false;
  }

  function pointInPolygonSetInterior(point, polygon) {
    if (!polygon?.length || pointOnRingBoundary(point, polygon[0])) return false;
    if (!(0, dependencies.territoryGeometry.pointInRing)(point, polygon[0])) return false;
    for (let i = 1; i < polygon.length; i += 1) {
      if (pointOnRingBoundary(point, polygon[i]) || (0, dependencies.territoryGeometry.pointInRing)(point, polygon[i])) return false;
    }
    return true;
  }

  function unwrapLongitudeNear(longitude, reference) {
    let value = Number(longitude);
    while (value - reference > 180) value -= 360;
    while (value - reference < -180) value += 360;
    return value;
  }

  function interpolateCutCoordinate(a, b, t) {
    return [Number(a[0]) + (unwrapLongitudeNear(b[0], a[0]) - a[0]) * t, Number(a[1]) + (Number(b[1]) - a[1]) * t];
  }

  function segmentIntersectionDetail(a, b, c, d, epsilon = 1e-10) {
    const p = [Number(a[0]), Number(a[1])];
    const q = [unwrapLongitudeNear(c[0], p[0]), Number(c[1])];
    const bLon = unwrapLongitudeNear(b[0], p[0]);
    let dLon = unwrapLongitudeNear(d[0], q[0]);
    const lineMid = (p[0] + bLon) / 2;
    while ((q[0] + dLon) / 2 - lineMid > 180) { q[0] -= 360; dLon -= 360; }
    while ((q[0] + dLon) / 2 - lineMid < -180) { q[0] += 360; dLon += 360; }
    const r = [bLon - p[0], Number(b[1]) - p[1]];
    const s = [dLon - q[0], Number(d[1]) - q[1]];
    const cross = (u, v) => u[0] * v[1] - u[1] * v[0];
    const qp = [q[0] - p[0], q[1] - p[1]];
    const denominator = cross(r, s);
    if (Math.abs(denominator) <= epsilon) {
      if (Math.abs(cross(qp, r)) > epsilon) return null;
      const length2 = r[0] * r[0] + r[1] * r[1];
      if (length2 <= epsilon) return null;
      const t0 = (qp[0] * r[0] + qp[1] * r[1]) / length2;
      const qd = [q[0] + s[0] - p[0], q[1] + s[1] - p[1]];
      const t1 = (qd[0] * r[0] + qd[1] * r[1]) / length2;
      const overlapStart = Math.max(0, Math.min(t0, t1));
      const overlapEnd = Math.min(1, Math.max(t0, t1));
      return overlapEnd - overlapStart > epsilon ? { overlap: true, coord: interpolateCutCoordinate(a, b, (overlapStart + overlapEnd) / 2) } : null;
    }
    const lineT = cross(qp, s) / denominator;
    const boundaryT = cross(qp, r) / denominator;
    if (lineT < -epsilon || lineT > 1 + epsilon || boundaryT < -epsilon || boundaryT > 1 + epsilon) return null;
    const t = (0, dependencies.platform.clamp)(lineT, 0, 1);
    return {
      overlap: false,
      lineT: t,
      boundaryT: (0, dependencies.platform.clamp)(boundaryT, 0, 1),
      coord: interpolateCutCoordinate(a, b, t),
    };
  }

  function coordinateAtPathPosition(rawLine, position) {
    const maxPosition = Math.max(0, rawLine.length - 1);
    const bounded = (0, dependencies.platform.clamp)(position, 0, maxPosition);
    if (bounded >= maxPosition) return rawLine[rawLine.length - 1].slice();
    const segmentIndex = Math.floor(bounded);
    return interpolateCutCoordinate(rawLine[segmentIndex], rawLine[segmentIndex + 1], bounded - segmentIndex);
  }

  function activeCutDraftSourceGeometry() {
    if (dependencies.projectState.state.tool === 'split-generic-feature') {
      return dependencies.projectState.state.genericFeatures.find(item => String(item.id) === String(dependencies.projectState.state.genericFeatureSplitSourceId))?.geometry || null;
    }
    if (dependencies.projectState.state.tool === 'split-territorial-unit') {
      return (dependencies.territorialModel.entityRepository.get(dependencies.projectState.state.territorialUnitSplitSourceId) || dependencies.projectState.state.territorialUnitSplitVirtualSource)?.geometry || null;
    }
    const territorySelection = dependencies.projectState.state.territorySelectionSession;
    if (territorySelection?.tool === dependencies.projectState.state.tool && territorySelection.stage === 'selection'
      && territorySelection.activePhase === 'drawing' && territorySelection.activeMethod === 'line') return territorySelection.workingSourceGeometry || null;
    return null;
  }

  function cutEndpointSnapDistance() {
    const coarsePointer = dependencies.platform.coarsePointer ?? globalThis.matchMedia?.('(pointer: coarse)')?.matches;
    return coarsePointer ? dependencies.geometryValidation.CUT_ENDPOINT_SNAP_DISTANCE.touch : dependencies.geometryValidation.CUT_ENDPOINT_SNAP_DISTANCE.mouse;
  }

  function snapCutDraftLine(rawLine, sourceGeometry) {
    return (0, dependencies.geometryModel.snapLineEndpointsToBoundary)(rawLine, sourceGeometry, {
      project: coordinate => (0, dependencies.mapView.activeProjection)()(coordinate),
      maxDistance: cutEndpointSnapDistance(),
      isVisible: dependencies.mapView.isCoordVisible,
      maxSegmentLength: Math.max(1, dependencies.projectState.state.size.width * 0.7),
    });
  }

  function draftSelfIntersectionIssue(coords, closed = false) {
    const points = (coords || []).map(coord => coord.slice());
    if (closed && points.length >= 3) points.push(points[0].slice());
    const segmentCount = Math.max(0, points.length - 1);
    for (let left = 0; left < segmentCount; left += 1) {
      for (let right = left + 1; right < segmentCount; right += 1) {
        if (Math.abs(left - right) <= 1 || (closed && left === 0 && right === segmentCount - 1)) continue;
        const detail = segmentIntersectionDetail(points[left], points[left + 1], points[right], points[right + 1]);
        if (!detail) continue;
        return {
          kind: detail.overlap ? 'segment-overlap' : 'self-intersection',
          coordinate: detail.coord || (0, dependencies.geometryValidation.interpolateCoordinate)(points[left], points[left + 1], 0.5),
          segmentIndex: left,
        };
      }
    }
    return null;
  }

  function cutFailure(message, issue) {
    return Object.assign(new Error(message), { cutIssue: { ...issue, message } });
  }

  function prepareCutDraft(rawLine, sourceGeometry) {
    const snapped = snapCutDraftLine(rawLine, sourceGeometry);
    const line = snapped.line;
    if (line.length < 2) throw new Error('경계선을 만들려면 점을 두 개 이상 입력하세요.');
    for (let index = 1; index < line.length; index += 1) {
      if (dependencies.geometryPreview.coordNear(line[index - 1], line[index], 1e-9)) {
        throw cutFailure('서로 다른 위치를 연결하세요.', { kind: 'duplicate-vertex', coordinate: line[index], vertexIndex: index, segmentIndex: index - 1 });
      }
    }
    const intersection = draftSelfIntersectionIssue(line);
    if (intersection) throw cutFailure('새 국경선이 자기 자신과 교차하거나 겹칩니다.', intersection);
    return { ...snapped, extracted: extractInteriorCuts(line, sourceGeometry) };
  }

  function assessCutDraft(rawLine, sourceGeometry) {
    if (globalThis.document) return preparedCut(sourceGeometry, rawLine)
      || { line: rawLine, snaps: { start: null, end: null }, status: 'pending', valid: false, message: '경계선을 계산하는 중입니다.', issues: [] };
    prepareSourceIndexes(sourceGeometry);
    if (rawLine.length < 2) return { line: rawLine, snaps: { start: null, end: null }, status: 'pending', valid: false, message: '', issues: [] };
    try {
      return { ...prepareCutDraft(rawLine, sourceGeometry), status: 'valid', valid: true, message: '', issues: [] };
    } catch (error) {
      return { ...snapCutDraftLine(rawLine, sourceGeometry), status: 'invalid', valid: false,
        message: error.message, issues: error.cutIssue ? [error.cutIssue] : [] };
    }
  }

  function collectCutBoundaryEvents(rawLine, polygons) {
    const events = [];
    for (let lineIndex = 0; lineIndex < rawLine.length - 1; lineIndex += 1) {
      const a = rawLine[lineIndex], b = rawLine[lineIndex + 1];
      if ((0, dependencies.geometryPreview.coordNear)(a, b, 1e-10)) continue;
      polygons.forEach((polygon, polygonIndex) => {
        let geometry = polygonIndexGeometries.get(polygon);
        if (!geometry) { geometry = { type: 'Polygon', coordinates: polygon }; polygonIndexGeometries.set(polygon, geometry); }
        let cache = polygonCutEvents.get(polygon);
        if (!cache) { cache = new Map(); polygonCutEvents.set(polygon, cache); }
        const key = JSON.stringify([a, b]);
        let hits = cache.get(key);
        if (!hits) {
          hits = [];
          for (const edge of geometrySegmentIndex(geometry).query(segmentQueryBounds(a, b))) {
            const detail = segmentIntersectionDetail(a, b, edge.a, edge.b);
            if (detail) hits.push({ detail, edge });
          }
          cache.set(key, hits);
          while (cache.size > 64) cache.delete(cache.keys().next().value);
        }
        for (const { detail, edge } of hits) {
          if (detail.overlap) throw cutFailure('국경선을 기존 경계와 겹쳐 그릴 수 없습니다.', { kind: 'boundary-overlap', coordinate: detail.coord, segmentIndex: lineIndex });
          events.push({ position: lineIndex + detail.lineT, coord: detail.coord,
            ref: { polygonIndex, ringIndex: edge.ringIndex, boundarySegmentIndex: edge.segmentIndex, boundaryT: detail.boundaryT } });
        }
      });
    }
    events.sort((a, b) => a.position - b.position);
    const unique = [];
    for (const event of events) {
      const previous = unique[unique.length - 1];
      if (previous && Math.abs(previous.position - event.position) <= 1e-7 && (0, dependencies.geometryPreview.coordNear)(previous.coord, event.coord, 1e-7)) {
        previous.refs.push(event.ref);
      } else {
        unique.push({ position: event.position, coord: event.coord.slice(), refs: [event.ref] });
      }
    }
    return unique;
  }

  function unwrapCoordinates(coordinates, reference) {
    let previous = reference;
    return coordinates.map(coord => {
      previous = unwrapLongitudeNear(coord[0], previous);
      return [previous, Number(coord[1])];
    });
  }

  function createCutGraph(component, line, events) {
    const nodes = [];
    const edges = [];
    const buckets = new Map();
    const edgeKeys = new Set();
    const tolerance = 1e-10;
    const node = point => {
      const x = Math.round(point[0] / tolerance), y = Math.round(point[1] / tolerance);
      for (let dx = -1; dx <= 1; dx += 1) for (let dy = -1; dy <= 1; dy += 1) {
        for (const id of buckets.get(`${x + dx}:${y + dy}`) || []) {
          const other = nodes[id].point;
          if (Math.abs(point[0] - other[0]) <= tolerance && Math.abs(point[1] - other[1]) <= tolerance) return id;
        }
      }
      const id = nodes.length;
      nodes.push({ id, point: point.slice() });
      const key = `${x}:${y}`;
      if (!buckets.has(key)) buckets.set(key, []);
      buckets.get(key).push(id);
      return id;
    };
    const edge = (a, b, cut) => {
      const from = node(a), to = node(b);
      if (from === to) return;
      const key = from < to ? `${from}:${to}` : `${to}:${from}`;
      if (edgeKeys.has(key)) throw cutFailure('국경선이 기존 경계와 겹칩니다.', { kind: 'boundary-overlap', coordinate: a });
      edgeKeys.add(key);
      edges.push({ id: edges.length, a: from, b: to, cut });
    };
    const insertions = new Map();
    for (const event of events) for (const ref of event.refs) {
      const key = `${ref.ringIndex}:${ref.boundarySegmentIndex}`;
      if (!insertions.has(key)) insertions.set(key, []);
      insertions.get(key).push({ t: ref.boundaryT, coord: event.coord });
    }
    component.forEach((ring, ringIndex) => {
      for (let index = 0; index < ring.length - 1; index += 1) {
        const points = [{ t: 0, coord: ring[index] }, ...(insertions.get(`${ringIndex}:${index}`) || []), { t: 1, coord: ring[index + 1] }].sort((a, b) => a.t - b.t);
        for (let part = 1; part < points.length; part += 1) edge(points[part - 1].coord, points[part].coord, false);
      }
    });
    const points = [...line.map((coord, position) => ({ position, coord })), ...events].sort((a, b) => a.position - b.position);
    let cutEdges = 0;
    for (let index = 1; index < points.length; index += 1) {
      const a = points[index - 1], b = points[index];
      if (b.position - a.position <= 1e-10) continue;
      const middle = coordinateAtPathPosition(line, (a.position + b.position) / 2);
      if (!pointInPolygonSetInterior(middle, component)) continue;
      edge(a.coord, b.coord, true);
      cutEdges += 1;
    }
    return { nodes, edges, cutEdges };
  }

  function extractInteriorCuts(line, sourceGeometry) {
    const polygons = dependencies.geometryPreview.geometryPolygonSets(sourceGeometry);
    if (!polygons.length) throw new Error('분할할 영토를 찾을 수 없습니다.');
    const partitions = [];
    polygons.forEach((rawComponent, componentIndex) => {
      const reference = rawComponent[0][0][0];
      const unwrapped = rawComponent.map(ring => unwrapCoordinates(ring, reference));
      const unchanged = unwrapped.every((ring, ri) => ring.every((coord, i) => coord[0] === rawComponent[ri][i][0]));
      const component = unchanged ? rawComponent : unwrapped;
      const localLine = unwrapCoordinates(line, reference);
      if (!boundsOverlap(coordinateBounds(component), coordinateBounds(localLine))) return;
      for (const [vertexIndex, coord] of [[0, localLine[0]], [line.length - 1, localLine.at(-1)]]) {
        if (pointInPolygonSetInterior(coord, component)) {
          throw cutFailure('시작점과 끝점을 영역 밖이나 경계에 놓으세요.', { kind: 'endpoint-inside', vertexIndex, coordinate: line[vertexIndex] });
        }
      }
      const events = collectCutBoundaryEvents(localLine, [component]);
      if (!events.length) return;
      const graph = createCutGraph(component, localLine, events);
      if (!graph.cutEdges) return; // Tangential contacts do not partition land.
      const faces = tracePlanarGraphFaces(graph).filter(face => face.edgeIds.some(id => graph.edges[id].cut));
      if (faces.length < 2) return; // One outer-to-hole connection is a bridge, not a split.
      partitions.push({ componentIndex, component, faces });
    });
    if (!partitions.length) throw new Error('영역을 나누지 않습니다. 선을 양쪽 경계까지 연결하세요.');
    return { partitions };
  }

  function wrapCutGeometry(geometry, clipper) {
    const polygons = dependencies.territoryGeometry.geometryMultiCoordinates(geometry);
    const bounds = coordinateBounds(polygons);
    if (bounds[0] >= -180 && bounds[2] <= 180) return geometry;
    const result = [];
    const first = Math.floor((bounds[0] + 180) / 360), last = Math.floor((bounds[2] + 180) / 360);
    for (let strip = first; strip <= last; strip += 1) {
      const west = -180 + strip * 360, east = 180 + strip * 360;
      const clipped = clipper.intersection(polygons, [[[[west, -90], [east, -90], [east, 90], [west, 90], [west, -90]]]]);
      result.push(...clipped.map(polygon => polygon.map(ring => ring.map(([x, y]) => [x - strip * 360, y]))));
    }
    return normalizeClippedLandGeometry(result);
  }

  function buildCutSplitCandidates(sourceGeometry, rawLine, assessment = null) {
    const cached = preparedCut(sourceGeometry, rawLine);
    if (cached?.split) return cached.split;
    if (globalThis.document) throw new Error(cached?.splitError || '최신 경계선의 계산이 끝난 뒤 다시 시도하세요.');
    prepareSourceIndexes(sourceGeometry);
    const clipper = dependencies.platform.polygonClipping || globalThis.polygonClipping;
    if (!clipper?.intersection || !clipper?.union || !clipper?.xor) throw new Error('영토 편입 엔진을 불러오지 못했습니다.');
    const prepared = assessment?.valid ? assessment : prepareCutDraft(rawLine, sourceGeometry);
    const candidates = [];
    for (const { componentIndex, component, faces } of prepared.extracted.partitions) {
      const geometries = [];
      for (const face of faces) {
        const clipped = clipper.intersection([[face.ring]], [component]);
        for (const polygon of clipped) {
          const geometry = normalizeClippedLandGeometry([polygon]);
          if (geometry) geometries.push(geometry);
        }
      }
      const coordinates = geometries.map(dependencies.territoryGeometry.geometryMultiCoordinates);
      const componentArea = dependencies.territoryGeometry.multiPolygonPlanarArea([component]);
      const tolerance = Math.max(1e-10, componentArea * 1e-10);
      if (geometries.length < 2) throw new Error('새 국경선으로 유효한 조각을 만들 수 없습니다.');
      for (let left = 0; left < geometries.length; left += 1) {
        const area = dependencies.territoryGeometry.multiPolygonPlanarArea(coordinates[left]);
        if (area <= tolerance) throw new Error('새 국경선으로 생긴 영토가 너무 작거나 비어 있습니다.');
        for (let right = left + 1; right < geometries.length; right += 1) {
          if (dependencies.territoryGeometry.multiPolygonPlanarArea(clipper.intersection(coordinates[left], coordinates[right])) > tolerance) {
            throw new Error('분할된 영토 조각이 서로 겹칩니다.');
          }
        }
        candidates.push({ id: `cut:${componentIndex}:${left}`, geometry: wrapCutGeometry(geometries[left], clipper), area });
      }
      const combined = clipper.union(...coordinates);
      if (dependencies.territoryGeometry.multiPolygonPlanarArea(clipper.xor([component], combined)) > tolerance) {
        throw new Error('분할된 영토 조각이 원본 영역과 일치하지 않습니다.');
      }
    }
    return { line: prepared.line, candidates };
  }

  function applyWorkerCountryPatches(result, options = {}) {
    const updates = new Map((result.features || []).map(feature => {
      const next = (0, dependencies.platform.deepClone)(feature);
      const normalizedGeometry = (0, dependencies.geometryModel.normalizePolygonGeometry)(next.geometry);
      if (!normalizedGeometry) throw new Error(`${(0, dependencies.objectPresentation.territorialEntityName)(next)}의 편집 결과가 유효하지 않습니다.`);
      next.geometry = normalizedGeometry;
      return [String(next.id || ''), next];
    }));
    const removed = new Set((result.removedIds || []).map(String));
    dependencies.territorialModel.entityStore.applyChanges({
      features: [...updates.values()], removedIds: [...removed],
    });
    dependencies.geometryMutation.setApplyingWorkerResult(true);
    try {
      (0, dependencies.spatialQuery.markCountryGeometriesChanged)(
        new Set(result.affectedIds || [...updates.keys(), ...removed]),
        options,
      );
    } finally {
      dependencies.geometryMutation.setApplyingWorkerResult(false);
    }
  }

  return Object.freeze({
    connect,

    get activeCutDraftSourceGeometry() { return activeCutDraftSourceGeometry; },
    get applyWorkerCountryPatches() { return applyWorkerCountryPatches; },
    get assessCutDraft() { return assessCutDraft; },
    get boundsOverlap() { return boundsOverlap; },
    get buildCutSplitCandidates() { return buildCutSplitCandidates; },
    get coordinateBounds() { return coordinateBounds; },
    get normalizeClippedLandGeometry() { return normalizeClippedLandGeometry; },
    get segmentIntersectionDetail() { return segmentIntersectionDetail; },

  });
}
