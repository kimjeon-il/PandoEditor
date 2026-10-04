import { buildGeometryPreview } from './geometry-preview.js';
import { validateGeometry, validateTerritorialGeometry } from './geometry-validation.js';

const featureId = feature => String(feature?.id || '');
const validation = issues => ({ issues, blocking: issues.some(issue => issue.severity !== 'warning') });

export function calculateTerritorialPreview(payload, beforeFeatures, clipper) {
  const { beforeIds = [], afterFeatures = [], removedIds = [], operation, transferredGeometry } = payload;
  if (beforeFeatures.length !== beforeIds.length || beforeFeatures.some(feature => feature.properties?.locked)) throw new Error('편집 대상이 변경되었거나 잠겨 있습니다.');
  return { ...buildGeometryPreview({ operation, beforeFeatures, afterFeatures, removedIds, transferredGeometry, clipper }), validation: validation(afterFeatures.flatMap(validateGeometry)) };
}

export function calculateEditPreview(operation, result, originals, clipper) {
  const beforeFeatures = originals.filter(feature => result.affectedIds.includes(featureId(feature)));
  return { ...buildGeometryPreview({ operation: 'territorial-' + operation, beforeFeatures, afterFeatures: result.features, removedIds: result.removedIds, clipper }), validation: validation(result.features.flatMap(validateGeometry)) };
}

export function calculateCountryPreview(message, result, before, after, clipper) {
  const affectedIds = new Set(result.affectedIds.map(String));
  const issueKey = issue => String(issue.kind) + ':' + [...(issue.entityRefs || [])].sort().join('|');
  const baseline = new Set(validateTerritorialGeometry(before, { clipper, affectedIds }).map(issueKey));
  const issues = validateTerritorialGeometry(after, { clipper, affectedIds }).filter(issue => !baseline.has(issueKey(issue)));
  return { ...buildGeometryPreview({ operation: message.operation, beforeFeatures: before.filter(feature => affectedIds.has(featureId(feature))), afterFeatures: result.features, removedIds: result.removedIds, clipper, transferredGeometry: result.transferredGeometry || message.previewTransferredGeometry }), validation: validation(issues) };
}
