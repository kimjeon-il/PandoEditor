import { createDocumentMutationRunner } from './document-mutation-runner.js';
import { normalizeTemporalInterval } from './temporal.js';
import { createTerritorialFeature, normalizeTerritorialEntities, territorialRootId } from './territorial-units.js';

const text = value => String(value ?? '').trim();

export function createTerritorialApplicationService({
  entityRepository,
  entityStore,
  commandPipeline,
}) {
  if (!entityStore) throw new TypeError('영역 애플리케이션 서비스에는 엔티티 저장소가 필요합니다.');
  const mutateDocument = createDocumentMutationRunner({ commandPipeline });
  const entity = id => entityRepository.get(id);

  function canDelete(id) {
    const key = text(id);
    const feature = entity(key);
    if (!feature) return { ok: false, code: 'not-found' };
    if (isLocked(key)) return { ok: false, code: 'locked', unit: feature };
    const children = entityRepository.children(key);
    if (children.length) return { ok: false, code: 'has-children', unit: feature, children };
    return { ok: true, unit: feature, children: [] };
  }

  function isLocked(id) {
    return entity(id)?.properties?.locked === true;
  }

  function updateMetadata(id, field, value) {
    const key = text(id);
    const feature = entityRepository.get(key);
    if (!feature) return { ok: false, code: 'not-found' };
    if (isLocked(key)) return { ok: false, code: 'locked', unit: feature };
    if (field === 'parentId' || field === 'entityKind') {
      return { ok: false, code: 'unsupported-relation-field', unit: feature };
    }
    if (!['name', 'notes', 'color', 'capital', 'flagDataUrl', 'validFrom', 'validTo'].includes(field)) {
      return { ok: false, code: 'unsupported-field', unit: feature };
    }
    let nextValue = value;
    if (['name', 'color', 'capital'].includes(field)) nextValue = text(value);
    if (field === 'notes') nextValue = String(value ?? '');
    if (field === 'flagDataUrl' && value !== null && value !== undefined) {
      if (typeof value !== 'string' || !value.trim()) return { ok: false, code: 'invalid-flag', unit: feature };
      nextValue = value.trim();
    }
    if (field === 'validFrom' || field === 'validTo') {
      try {
        const interval = normalizeTemporalInterval(
          field === 'validFrom' ? value : feature.properties?.validFrom,
          field === 'validTo' ? value : feature.properties?.validTo,
        );
        nextValue = interval[field];
        if (nextValue !== null) return { ok: false, code: 'TIMELINE_ACTIVATION', unit: feature,
          issues: ['날짜별 편집은 T4 구현 후 지원합니다.'] };
      } catch (error) {
        return { ok: false, code: 'invalid-temporal', issues: [String(error?.message || error)], unit: feature };
      }
    }
    const currentValue = field === 'color' ? text(feature.properties?.style?.color)
      : ['capital', 'flagDataUrl'].includes(field) ? feature.properties?.metadata?.[field] : feature.properties?.[field];
    const comparable = ['validFrom', 'validTo'].includes(field) ? currentValue ?? null
      : ['name', 'color', 'capital'].includes(field) ? text(currentValue)
      : field === 'notes' ? String(currentValue ?? '') : currentValue;
    if (comparable === nextValue && (nextValue !== undefined || !entityStore.hasField(key, field))) return { ok: true, changed: false, unit: feature };
    mutateDocument({ type: 'territorial-metadata', affectedIds: [key] }, () => {
      entityStore.setField(key, field, nextValue);
    }, { renderDirty: { domain: 'territorial', change: 'metadata' } });
    return { ok: true, changed: true, unit: entityRepository.get(key) };
  }

  function changeAdministrativeParent(id, parentId, { validateCandidate = null } = {}) {
    const key = text(id);
    const feature = entity(key);
    if (!feature) return { ok: false, code: 'not-found' };
    if (feature.properties?.locked === true) return { ok: false, code: 'locked', unit: feature };
    if (feature.properties.entityKind !== 'general') {
      return { ok: false, code: 'unsupported-parent-type', unit: feature };
    }
    const nextParentId = text(parentId);
    if (text(feature.properties?.parentId) === nextParentId) return { ok: true, changed: false, unit: feature };

    const previousUnits = entityRepository.list();
    const candidateUnits = previousUnits.map(candidate => String(candidate.id) === key
      ? { ...candidate, properties: { ...candidate.properties, parentId: nextParentId, coverageMode: nextParentId ? candidate.properties.coverageMode : 'explicit' } }
      : candidate);
    try {
      normalizeTerritorialEntities(candidateUnits, { cloneGeometry: geometry => geometry });
      const proposed = new Map(candidateUnits.map(candidate => [candidate.id, candidate]));
      for (const before of previousUnits) if (before.properties.locked && territorialRootId(before, id => entityRepository.get(id)) !== territorialRootId(proposed.get(before.id), id => proposed.get(id))) throw new Error(before.id + ': 잠긴 객체의 루트를 변경할 수 없습니다.');
    } catch (error) { return { ok: false, code: 'invalid-parent', issues: [error.message], unit: feature }; }
    if (typeof validateCandidate === 'function') {
      try {
        const extraValidation = validateCandidate({ feature, previousUnits, candidateUnits, parentId: nextParentId });
        if (extraValidation === false || extraValidation?.ok === false) {
          return {
            ok: false,
            code: 'invalid-parent-geometry',
            issues: extraValidation?.issues || [extraValidation?.message || '상위 단위 변경 조건을 만족하지 않습니다.'],
            unit: feature,
          };
        }
      } catch (error) {
        return { ok: false, code: 'invalid-parent-geometry', issues: [String(error?.message || error)], unit: feature };
      }
    }

    mutateDocument({ type: 'territorial-parent', affectedIds: [key] }, () => {
      entityStore.applyChanges({ features: [candidateUnits.find(candidate => candidate.id === key)] });
    }, { renderDirty: { domain: 'territorial', change: 'structure' } });
    return { ok: true, changed: true, unit: entityRepository.get(key) };
  }

  function setColorBatch(items, color, { history = {} } = {}) {
    const requested = (items || []).map(item => ({ type: text(item?.type), id: text(item?.id) }))
      .filter(item => item.type && item.id);
    if (!requested.length) return { ok: true, changed: false, units: [] };

    const units = [];
    for (const item of requested) {
      const feature = entity(item.id);
      if (!feature) return { ok: false, code: 'not-found', id: item.id, type: item.type };
      units.push(feature);
    }
    const changed = requested.filter((item, index) => units[index].properties?.style?.color !== color);
    if (!changed.length) return { ok: true, changed: false, units };

    mutateDocument(
      {
        ...history,
        type: history.type || 'territorial-color-batch',
        affectedIds: changed.map(item => item.id),
      },
      () => entityStore.transaction(() => {
        for (const item of changed) {
          entityStore.setField(item.id, 'color', color);
        }
      }),
      { renderDirty: { domain: 'territorial', change: 'metadata' } },
    );
    return { ok: true, changed: true, units: requested.map(item => entityRepository.get(item.id)).filter(Boolean) };
  }

  function setLockedBatch(items, locked, { history = {} } = {}) {
    const next = !!locked;
    const requested = (items || []).map(item => ({ type: text(item?.type), id: text(item?.id) }))
      .filter(item => item.type && item.id);
    if (!requested.length) return { ok: true, changed: false, units: [] };

    const units = [];
    for (const item of requested) {
      const feature = entity(item.id);
      if (!feature) return { ok: false, code: 'not-found', id: item.id, type: item.type };
      units.push(feature);
    }
    const changed = requested.filter((item, index) => {
      const feature = units[index];
      return (feature.properties?.locked === true) !== next;
    });
    if (!changed.length) return { ok: true, changed: false, units };

    mutateDocument(
      {
        ...history,
        type: history.type || 'territorial-lock-batch',
        affectedIds: changed.map(item => item.id),
      },
      () => entityStore.transaction(() => {
        for (const item of changed) {
          entityStore.setLocked(item.id, next);
        }
      }),
      { renderDirty: { domain: 'territorial', change: 'metadata' } },
    );
    return { ok: true, changed: true, units: requested.map(item => entityRepository.get(item.id)).filter(Boolean) };
  }

  function setLocked(id, locked, { history = {} } = {}) {
    const key = text(id);
    const next = !!locked;
    const feature = entity(key);
    if (!feature) return { ok: false, code: 'not-found' };
    if ((feature.properties?.locked === true) === next) return { ok: true, changed: false, unit: feature };
    mutateDocument(
      { ...history, type: 'territorial-lock', affectedIds: [key] },
      () => entityStore.setLocked(key, next),
      { renderDirty: { domain: 'territorial', change: 'metadata' } },
    );
    return { ok: true, changed: true, unit: entityRepository.get(key) };
  }

  function copyIndependentRegion(sourceId, { name = '', color = '' } = {}) {
    const source = entityRepository.get(sourceId);
    if (!source || source.properties.entityKind !== 'general') return { ok: false, code: 'invalid-source' };
    const feature = createTerritorialFeature({
      id: globalThis.crypto.randomUUID(), entityKind: 'regional',
      name, color, geometry: source.geometry,
    });
    mutateDocument({ type: 'territorial-copy-region', affectedIds: [feature.id] }, () => {
      entityStore.appendEntities([feature]);
    }, { renderDirty: { domain: 'territorial', change: 'structure' } });
    return { ok: true, changed: true, unit: entityRepository.get(feature.id) };
  }

  return Object.freeze({
    copyIndependentRegion,
    canDelete,
    isLocked,
    updateMetadata,
    changeAdministrativeParent,

    setColorBatch,
    setLocked,
    setLockedBatch,
  });
}
