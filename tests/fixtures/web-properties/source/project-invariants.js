import { normalizeTemporalInterval } from './temporal.js';
import { validateSourceProvenance } from './source-provenance.js';

const text = value => String(value ?? '').trim();
const POLYGON_TYPES = new Set(['Polygon', 'MultiPolygon']);
const DISTRIBUTION_MODES = new Set(['territorial', 'geometry']);

function issue(code, message, objectIds = [], field = '') {
  return Object.freeze({
    code,
    severity: 'error',
    message,
    objectIds: [...new Set(objectIds.map(String).filter(Boolean))],
    field,
  });
}

function duplicateIssues(rows, idOf, code, label) {
  const seen = new Set();
  const issues = [];
  for (const row of rows || []) {
    const id = text(idOf(row));
    if (!id) {
      issues.push(issue(`${code}-EMPTY`, `${label} ID가 비어 있습니다.`));
      continue;
    }
    if (seen.has(id)) issues.push(issue(`${code}-DUPLICATE`, `${label} ID가 중복되었습니다: ${id}`, [id]));
    seen.add(id);
  }
  return issues;
}

function parentCycle(startId, byId, parentOf) {
  const start = text(startId);
  if (!start) return false;
  const visited = new Set([start]);
  let cursor = text(parentOf(byId.get(start)));
  while (cursor && byId.has(cursor)) {
    if (visited.has(cursor)) return true;
    visited.add(cursor);
    cursor = text(parentOf(byId.get(cursor)));
  }
  return false;
}

function geometryIssue(row, id, label) {
  const geometry = row?.geometry;
  if (!geometry || !POLYGON_TYPES.has(geometry.type) || !Array.isArray(geometry.coordinates) || !geometry.coordinates.length) {
    return issue('PL-INV-GEOMETRY', `${label} ${id || '(ID 없음)'}의 면 geometry가 비어 있거나 올바르지 않습니다.`, [id], 'geometry');
  }
  return null;
}

export function validateProjectReferenceIntegrity({
  countries = [],
  countryOverrides = {},
  territorialUnits = [],
  territorialRelations = [],
  distributionLayers = [],
  distributionEntries = [],
  labels = [],
  genericFeatures = [],
  itemVisibility = {},
  labelSettings = {},
} = {}) {
  const issues = [];

  issues.push(...duplicateIssues(countries, row => row?.id, 'PL-INV-COUNTRY', '국가'));
  issues.push(...duplicateIssues(territorialUnits, row => row?.id, 'PL-INV-UNIT', '영역'));
  issues.push(...duplicateIssues(territorialRelations, row => row?.id, 'PL-INV-RELATION', '기간별 관계'));
  issues.push(...duplicateIssues(distributionLayers, row => row?.id, 'PL-INV-DIST-LAYER', '분포 레이어'));
  issues.push(...duplicateIssues(distributionEntries, row => row?.id, 'PL-INV-DIST-ENTRY', '분포 엔트리'));
  issues.push(...duplicateIssues(genericFeatures, row => row?.id, 'PL-INV-GENERIC', '기타 객체'));

  const countryIds = new Set((countries || []).map(row => text(row?.id)).filter(Boolean));
  const unitIds = new Set((territorialUnits || []).map(row => text(row?.id)).filter(Boolean));
  const territorialIds = new Set([...countryIds, ...unitIds]);
  const unitById = new Map((territorialUnits || []).map(row => [text(row?.id), row]).filter(([id]) => id));

  for (const id of Object.keys(countryOverrides || {})) {
    if (!countryIds.has(text(id))) issues.push(issue('PL-INV-ORPHAN-COUNTRY-OVERRIDE', `존재하지 않는 국가 ${id}의 설정이 남아 있습니다.`, [id], 'countryOverrides'));
  }

  for (const id of unitIds) {
    if (countryIds.has(id)) {
      issues.push(issue('PL-INV-TERRITORIAL-ID-COLLISION', `국가와 하위 영역이 같은 ID를 사용합니다: ${id}`, [id], 'id'));
    }
  }

  for (const feature of countries || []) {
    const id = text(feature?.id);
    const geometryError = geometryIssue(feature, id, '국가');
    if (geometryError) issues.push(geometryError);
  }

  for (const feature of territorialUnits || []) {
    const id = text(feature?.id);
    const parentId = text(feature?.properties?.parentId);
    const sovereignId = text(feature?.properties?.sovereignId);
    const geometryError = geometryIssue(feature, id, '영역');
    if (geometryError) issues.push(geometryError);

    if (parentId === id) {
      issues.push(issue('PL-INV-SELF-PARENT', `${id}가 자기 자신을 상위 단위로 참조합니다.`, [id], 'parentId'));
    } else if (parentId && !territorialIds.has(parentId)) {
      issues.push(issue('PL-INV-MISSING-PARENT', `${id}의 상위 단위 ${parentId}이 존재하지 않습니다.`, [id, parentId], 'parentId'));
    }

    if (sovereignId && !countryIds.has(sovereignId)) {
      issues.push(issue('PL-INV-MISSING-SOVEREIGN', `${id}의 소속 국가 ${sovereignId}이 존재하지 않습니다.`, [id, sovereignId], 'sovereignId'));
    }

    if (parentCycle(id, unitById, row => row?.properties?.parentId)) {
      issues.push(issue('PL-INV-PARENT-CYCLE', `${id}의 상위 단위 관계가 순환합니다.`, [id], 'parentId'));
    }
    try { normalizeTemporalInterval(feature?.properties?.validFrom, feature?.properties?.validTo); }
    catch (error) { issues.push(issue('PL-INV-TEMPORAL', `${id}의 유효기간이 올바르지 않습니다. ${error.message}`, [id], 'validFrom')); }
  }

  for (const relation of territorialRelations || []) {
    const id = text(relation?.id);
    const unitId = text(relation?.unitId);
    const parentId = text(relation?.parentId);
    const sovereignId = text(relation?.sovereignId);
    if (!territorialIds.has(unitId)) {
      issues.push(issue('PL-INV-MISSING-RELATION-UNIT', `${id || unitId}의 대상 영역 ${unitId}이 존재하지 않습니다.`, [id, unitId], 'unitId'));
    }
    if (parentId && !territorialIds.has(parentId)) {
      issues.push(issue('PL-INV-MISSING-RELATION-PARENT', `${id || unitId}의 상위 단위 ${parentId}이 존재하지 않습니다.`, [id, unitId, parentId], 'parentId'));
    }
    if (sovereignId && !countryIds.has(sovereignId)) {
      issues.push(issue('PL-INV-MISSING-RELATION-SOVEREIGN', `${id || unitId}의 소속 국가 ${sovereignId}이 존재하지 않습니다.`, [id, unitId, sovereignId], 'sovereignId'));
    }
    try { normalizeTemporalInterval(relation?.validFrom, relation?.validTo); }
    catch (error) { issues.push(issue('PL-INV-TEMPORAL', `${id || unitId}의 유효기간이 올바르지 않습니다. ${error.message}`, [id, unitId], 'validFrom')); }
  }

  const layerById = new Map((distributionLayers || []).map(layer => [text(layer?.id), layer]).filter(([id]) => id));
  for (const layer of distributionLayers || []) {
    const id = text(layer?.id);
    const parentId = text(layer?.parentId);
    if (parentId === id) {
      issues.push(issue('PL-INV-DIST-SELF-PARENT', `${id}가 자기 자신을 상위 분포 레이어로 참조합니다.`, [id], 'parentId'));
    } else if (parentId) {
      const parent = layerById.get(parentId);
      if (!parent) {
        issues.push(issue('PL-INV-MISSING-DIST-PARENT', `${id}의 상위 분포 레이어 ${parentId}이 존재하지 않습니다.`, [id, parentId], 'parentId'));
      } else if (parent.type !== layer.type) {
        issues.push(issue('PL-INV-DIST-PARENT-TYPE', `${id}의 상위 분포 레이어 유형이 다릅니다.`, [id, parentId], 'parentId'));
      }
    }
    if (parentCycle(id, layerById, row => row?.parentId)) {
      issues.push(issue('PL-INV-DIST-PARENT-CYCLE', `${id}의 분포 레이어 상위 관계가 순환합니다.`, [id], 'parentId'));
    }
    try { normalizeTemporalInterval(layer?.validFrom, layer?.validTo); }
    catch (error) { issues.push(issue('PL-INV-TEMPORAL', `${id}의 유효기간이 올바르지 않습니다. ${error.message}`, [id], 'validFrom')); }
  }

  for (const entry of distributionEntries || []) {
    const id = text(entry?.id);
    const layerId = text(entry?.layerId);
    const mode = text(entry?.mode);
    const share = Number(entry?.share);

    if (!layerById.has(layerId)) {
      issues.push(issue('PL-INV-MISSING-DIST-LAYER', `${id}의 분포 레이어 ${layerId}이 존재하지 않습니다.`, [id, layerId], 'layerId'));
    }
    if (!DISTRIBUTION_MODES.has(mode)) {
      issues.push(issue('PL-INV-DIST-MODE', `${id}의 분포 방식이 올바르지 않습니다.`, [id], 'mode'));
    }
    if (!Number.isFinite(share) || share < 0 || share > 100) {
      issues.push(issue('PL-INV-DIST-SHARE', `${id}의 비율이 0~100 범위를 벗어났습니다.`, [id], 'share'));
    }
    try { normalizeTemporalInterval(entry?.validFrom, entry?.validTo); }
    catch (error) { issues.push(issue('PL-INV-TEMPORAL', `${id}의 유효기간이 올바르지 않습니다. ${error.message}`, [id], 'validFrom')); }
    if (mode === 'territorial') {
      const territorialUnitId = text(entry?.territorialUnitId);
      if (!territorialIds.has(territorialUnitId)) {
        issues.push(issue('PL-INV-MISSING-DIST-TERRITORIAL', `${id}의 참조 영역 ${territorialUnitId}이 존재하지 않습니다.`, [id, territorialUnitId], 'territorialUnitId'));
      }
    } else if (mode === 'geometry') {
      const geometry = entry?.geometry;
      if (!geometry || !POLYGON_TYPES.has(geometry.type) || !Array.isArray(geometry.coordinates) || !geometry.coordinates.length) {
        issues.push(issue('PL-INV-DIST-GEOMETRY', `${id}의 자유 분포 geometry가 비어 있거나 올바르지 않습니다.`, [id], 'geometry'));
      }
    }
  }

  for (const label of labels || []) {
    const id = text(label?.id);
    const countryId = text(label?.countryId || label?.country_id);
    if (countryId && !countryIds.has(countryId)) {
      issues.push(issue('PL-INV-MISSING-LABEL-COUNTRY', `${id || '지명'}의 국가 ${countryId}이 존재하지 않습니다.`, [id, countryId], 'countryId'));
    }
  }

  for (const genericFeature of genericFeatures || []) {
    const id = text(genericFeature?.id);
    const schemaVersion = Number(genericFeature?.properties?.schemaVersion || 1);
    if (schemaVersion >= 2) {
      const sourceValidation = validateSourceProvenance(genericFeature?.properties?.source);
      for (const sourceIssue of sourceValidation.issues) {
        issues.push(issue('PL-INV-GENERIC-SOURCE', `${id || '기타 객체'}의 출처 정보가 올바르지 않습니다. ${sourceIssue}`, [id], 'source'));
      }
      continue;
    }

    // Compatibility only: v1 Generic Feature may still carry territorial semantics.
    const ownerId = text(genericFeature?.properties?.ownerId);
    if (ownerId && !territorialIds.has(ownerId)) {
      issues.push(issue('PL-INV-MISSING-GENERIC-OWNER', `${id || '기타 객체'}의 소유 영역 ${ownerId}이 존재하지 않습니다.`, [id, ownerId], 'ownerId'));
    }
    const topologyGroup = text(genericFeature?.properties?.topologyGroup);
    const landOwnerId = topologyGroup.startsWith('land:') ? topologyGroup.slice(5) : '';
    if (landOwnerId && !territorialIds.has(landOwnerId)) {
      issues.push(issue('PL-INV-MISSING-GENERIC-TOPOLOGY', `${id || '기타 객체'}의 지형 연결 대상 ${landOwnerId}이 존재하지 않습니다.`, [id, landOwnerId], 'topologyGroup'));
    }
  }

  for (const group of ['countries', 'countryLabels']) {
    for (const id of Object.keys(itemVisibility?.[group] || {})) {
      if (!countryIds.has(text(id))) issues.push(issue('PL-INV-ORPHAN-COUNTRY-VISIBILITY', `존재하지 않는 국가 ${id}의 표시 설정이 남아 있습니다.`, [id], `itemVisibility.${group}`));
    }
  }

  for (const key of Object.keys(labelSettings || {})) {
    if (!key.startsWith('country:')) continue;
    const id = key.slice('country:'.length);
    if (id && !countryIds.has(id)) issues.push(issue('PL-INV-ORPHAN-COUNTRY-LABEL', `존재하지 않는 국가 ${id}의 라벨 설정이 남아 있습니다.`, [id], 'labelSettings'));
  }

  return Object.freeze({ ok: issues.length === 0, issues });
}

export function assertProjectReferenceIntegrity(input) {
  const result = validateProjectReferenceIntegrity(input);
  if (result.ok) return result;
  const error = new Error(result.issues[0]?.message || '프로젝트 참조 무결성 검사에 실패했습니다.');
  error.code = 'PL-INV-001';
  error.issues = result.issues;
  throw error;
}
