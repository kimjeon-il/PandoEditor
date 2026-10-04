import { normalizePolygonGeometry, multiCoordinates } from './map-edit-geometry.js';
import './territorial-edit-plan.js';
import { territorialSegmentCandidates } from './geometry-segment-index.js';
import { validateGeometry } from './geometry-validation.js';

export function calculateTerritorialEdit(payload, entities, clipper) {
  return globalThis.PandoLabTerritorialEdit.createKernel(clipper, { normalize: normalizePolygonGeometry, segmentCandidates: territorialSegmentCandidates }).plan({ ...payload, entities });
}

export function calculateRegionRedraw(source, draft) {
  if (!source || source.properties?.locked || source.properties?.entityKind !== 'regional') throw new Error('지방 편집 대상이 변경되었습니다.');
  const geometry = normalizePolygonGeometry(draft);
  if (!geometry) throw new Error('유효한 닫힌 영역이 필요합니다.');
  const issues = validateGeometry({ ...source, geometry });
  if (issues.length) throw new Error(issues[0].message);
  return { feature: { ...source, geometry } };
}

export function calculateDrawnGeometry(payload, clipper) {
  const draft = normalizePolygonGeometry(payload.draft);
  if (!draft) throw new Error('그린 영역을 닫힌 Polygon으로 만들 수 없습니다.');
  const issues = validateGeometry({ type: 'Feature', id: 'draft', properties: {}, geometry: draft });
  if (issues.length) throw new Error(issues[0].message);
  const source = payload.source;
  const geometry = source ? normalizePolygonGeometry({ type: 'MultiPolygon', coordinates: clipper.intersection(multiCoordinates(draft), multiCoordinates(source)) }) : draft;
  if (!geometry) throw new Error('그린 영역이 기준 영역 안에 없습니다.');
  return { geometry };
}
