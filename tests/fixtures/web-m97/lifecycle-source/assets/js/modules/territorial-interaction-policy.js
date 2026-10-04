import { territorialSceneDisplayId } from './builtin-subunits.js';
export function subunitSelectionPolicy(units, { adjacent, deferConnectivity = false, locked = unit => unit.properties?.locked } = {}) {
  if (units.length < 2 || units.some(unit => !(unit?.properties?.entityKind === 'general' && !!unit?.properties?.parentId))) return { valid: false, message: '하위단위를 2개 이상 선택하세요.' };
  const first = units[0].properties;
  if (units.some(unit => unit.properties.parentId !== first.parentId)) return { valid: false, message: '같은 소속 국가·상위 단위의 하위단위만 함께 편집할 수 있습니다.' };
  if (units.some(locked)) return { valid: false, message: '선택한 하위단위의 잠금을 해제하세요.' };
  const connected = [units[0]], pending = units.slice(1);
  while (!deferConnectivity && pending.length) {
    const index = pending.findIndex(unit => connected.some(other => adjacent(unit, other)));
    if (index < 0) return { valid: false, message: '선택한 하위단위들이 공유 경계로 연결되어야 합니다.' };
    connected.push(...pending.splice(index, 1));
  }
  return { valid: true, message: '선택한 하위단위 사이의 공유 경계를 편집합니다.' };
}

export function territorialDeletionAllowed(targets, allUnits) {
  return targets.every(unit => unit && !unit.properties?.locked
    && !allUnits.some(child => String(child.properties?.parentId) === String(unit.id)));
}

export function boundaryTouchesGeometry(geometry, point, epsilon = 1e-7) {
  const polygons = geometry?.type === 'Polygon' ? [geometry.coordinates] : geometry?.coordinates || [];
  return polygons.some(polygon => polygon.some(ring => ring.some((b, index) => {
    if (!index) return false;
    const a = ring[index - 1], dx = b[0] - a[0], dy = b[1] - a[1], length = Math.hypot(dx, dy);
    if (!length) return false;
    const t = ((point[0] - a[0]) * dx + (point[1] - a[1]) * dy) / (length * length);
    return t >= -epsilon && t <= 1 + epsilon && Math.abs(dx * (point[1] - a[1]) - dy * (point[0] - a[0])) / length <= epsilon;
  })));
}

export function removeTerritorialEntities(state, {
  countryIds = [],
  unitIds = [],
} = {}, territorialMode = 'territorial', { entityStore = null } = {}) {
  for (const method of ['snapshot', 'applyChanges']) {
    if (typeof entityStore?.[method] !== 'function') {
      throw new TypeError(`영역 삭제에는 공통 엔티티 저장소의 ${method}가 필요합니다.`);
    }
  }
  const removedCountries = new Set([...countryIds].map(String).filter(Boolean));
  const removedUnits = new Set([...unitIds].map(String).filter(Boolean));
  const removedAll = new Set([...removedCountries, ...removedUnits]);

  const beforeUnits = entityStore.snapshot().filter(entity => !(entity.properties.entityKind === 'general' && !entity.properties.parentId));
  const countryIdsBefore = new Set(entityStore.snapshot().filter(entity => (entity.properties.entityKind === 'general' && !entity.properties.parentId)).map(feature => String(feature.id)));
  const unitTargets = [...removedUnits].map(id => beforeUnits.find(unit => String(unit.id) === id));
  if (!territorialDeletionAllowed(unitTargets, beforeUnits)) {
    throw new Error('잠금 또는 자식 관계 때문에 삭제할 수 없습니다.');
  }
  if (removedCountries.size && beforeUnits.some(unit => removedCountries.has(String(unit.properties?.parentId || '')))) {
    throw new Error('하위 영역이 있는 국가는 삭제할 수 없습니다.');
  }

  if (removedAll.size) entityStore.applyChanges({ removedIds: [...removedAll] });
  for (const id of removedAll) delete state.labelSettings?.[`territorial:${id}`];
  const removedDisplayIds = new Set([...removedCountries,
    ...unitTargets.map(feature => territorialSceneDisplayId(feature, countryIdsBefore))]);
  for (const displayId of removedDisplayIds) {
    delete state.itemVisibility?.countryLabels?.[displayId];
  }
  for (const feature of unitTargets) delete state.layerPresentation?.objectStyles?.[`territorial:entity:${feature.id}`];
  for (const id of removedCountries) delete state.layerPresentation?.objectStyles?.[`territorial:entity:${id}`];

  state.distributionEntries = (state.distributionEntries || []).filter(entry => (
    entry.mode !== territorialMode || !removedAll.has(String(entry.territorialUnitId || ''))
  ));

  state.labels = (state.labels || []).map(label => {
    const countryId = String(label.countryId || label.country_id || '');
    if (!removedCountries.has(countryId)) return label;
    const next = { ...label };
    if ('countryId' in next) next.countryId = '';
    if ('country_id' in next) next.country_id = '';
    return next;
  });

  for (const id of removedCountries) {
    delete state.itemVisibility?.countries?.[id];
    delete state.itemVisibility?.countryLabels?.[id];
  }
  for (const id of removedUnits) {
    delete state.itemVisibility?.subunits?.[id];
    delete state.itemVisibility?.regions?.[id];

  }

  return {
    countryIds: [...removedCountries],
    unitIds: [...removedUnits],
  };
}
