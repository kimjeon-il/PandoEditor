import { createDocumentMutationRunner } from './document-mutation-runner.js';
import { validateSubunitParentChanges } from './territorial-scope.js';
import {
  TERRITORIAL_UNIT_TYPES,
  runTerritorialTransaction,
  validateTerritorialRelations,
} from './territorial-units.js';

const text = value => String(value ?? '').trim();

export function createTerritorialApplicationService({
  repository,
  commandPipeline,
  countryCommands,
  unitCommands,
}) {
  const mutateDocument = createDocumentMutationRunner({ commandPipeline });
  const country = id => {
    const feature = repository.get(id);
    return feature?.properties?.unitType === TERRITORIAL_UNIT_TYPES.COUNTRY ? feature : null;
  };
  const unit = (type, id) => {
    const feature = repository.get(id);
    return feature?.properties?.unitType === type && type !== TERRITORIAL_UNIT_TYPES.COUNTRY ? feature : null;
  };

  function get(id) {
    return repository.get(id);
  }

  function list(options) {
    return repository.list(options);
  }

  function isLocked(type, id) {
    if (type === TERRITORIAL_UNIT_TYPES.COUNTRY) return !!country(id) && countryCommands.isLocked(id);
    return unit(type, id)?.properties?.locked === true;
  }

  function updateMetadata(type, id, field, value) {
    const key = text(id);
    if (type === TERRITORIAL_UNIT_TYPES.COUNTRY) {
      const feature = country(key);
      if (!feature) return { ok: false, code: 'not-found' };
      const currentValue = field === 'color' ? feature.properties?.style?.color : feature.properties?.[field];
      const hasExplicitValue = field === 'flagDataUrl'
        ? countryCommands.hasField?.(key, field) === true
        : currentValue !== undefined;
      if (currentValue === value && (value !== undefined || !hasExplicitValue)) return { ok: true, changed: false, unit: feature };
      mutateDocument({ type: 'country-metadata', affectedIds: [key] }, () => {
        countryCommands.setField(key, field, value);
      }, { renderDirty: { domain: 'country', change: 'metadata' } });
      return { ok: true, changed: true, unit: repository.get(key) };
    }
    const feature = unit(type, key);
    if (!feature) return { ok: false, code: 'not-found' };
    if (feature.properties?.locked === true && field !== 'locked') return { ok: false, code: 'locked', unit: feature };
    const currentValue = field === 'color' ? feature.properties?.style?.color : feature.properties?.[field];
    if (currentValue === value) return { ok: true, changed: false, unit: feature };
    if (field === 'parentId' || field === 'unitType') {
      const previous = repository.list();
      const candidate = previous.map(item => String(item.id) === key
        ? { ...item, properties: { ...item.properties, [field]: value } } : item);
      const validation = validateSubunitParentChanges(previous, candidate, id => !!country(id));
      if (!validation.ok) return { ok: false, code: 'invalid-parent', issues: validation.issues, unit: feature };
    }
    mutateDocument({ type: 'territorial-metadata', affectedIds: [key] }, () => {
      unitCommands.setField(key, field, value);
    }, { renderDirty: { domain: 'territorial', change: 'metadata' } });
    return { ok: true, changed: true, unit: repository.get(key) };
  }

  function replaceUnits(units, { type = 'territorial-metadata', affectedIds = [] } = {}) {
    const parentValidation = validateSubunitParentChanges(repository.list(), units, id => !!country(id));
    if (!parentValidation.ok) throw new Error(parentValidation.issues[0]);
    if (JSON.stringify(repository.list().filter(feature => feature?.properties?.unitType !== TERRITORIAL_UNIT_TYPES.COUNTRY)) === JSON.stringify(units)) {
      return { ok: true, changed: false };
    }
    mutateDocument({ type, affectedIds: affectedIds.map(text).filter(Boolean) }, () => {
      unitCommands.replaceAll(units);
    }, { renderDirty: { domain: 'territorial', change: 'structure' } });
    return { ok: true, changed: true };
  }

  function setLocked(type, id, locked, { history = {} } = {}) {
    const key = text(id);
    const next = !!locked;
    if (type === TERRITORIAL_UNIT_TYPES.COUNTRY) {
      if (!country(key)) return { ok: false, code: 'not-found' };
      if (countryCommands.isLocked(key) === next) return { ok: true, changed: false, unit: repository.get(key) };
      mutateDocument(
        { ...history, type: 'country-lock', affectedIds: [key] },
        () => countryCommands.setLocked(key, next),
        { renderDirty: { domain: 'country', change: 'metadata' } },
      );
      return { ok: true, changed: true, unit: repository.get(key) };
    }
    const feature = unit(type, key);
    if (!feature) return { ok: false, code: 'not-found' };
    if (feature.properties?.locked === next) return { ok: true, changed: false, unit: feature };
    mutateDocument(
      { ...history, type: 'territorial-lock', affectedIds: [key] },
      () => unitCommands.setField(key, 'locked', next),
      { renderDirty: { domain: 'territorial', change: 'metadata' } },
    );
    return { ok: true, changed: true, unit: repository.get(key) };
  }

  return Object.freeze({
    get,
    list,
    isLocked,
    updateMetadata,
    replaceUnits,
    setLocked,
    runGeometryTransaction: options => runTerritorialTransaction(options),
    validateRelations: (units, options) => validateTerritorialRelations(units, options),
  });
}
