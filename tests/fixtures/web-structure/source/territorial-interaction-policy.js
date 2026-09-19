export function subunitSelectionPolicy(units, { adjacent, deferConnectivity = false, locked = unit => unit.properties?.locked } = {}) {
  if (units.length < 2 || units.some(unit => unit?.properties?.unitType !== 'subunit')) return { valid: false, message: '하위단위를 2개 이상 선택하세요.' };
  const first = units[0].properties;
  if (units.some(unit => unit.properties.parentId !== first.parentId || unit.properties.sovereignId !== first.sovereignId)) return { valid: false, message: '같은 소속 국가·상위 단위의 하위단위만 함께 편집할 수 있습니다.' };
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

export function removeTerritorialUnits(state, ids, territorialMode) {
  const removed = new Set([...ids].map(String));
  const targets = [...removed].map(id => state.territorialUnits.find(unit => String(unit.id) === id));
  if (!territorialDeletionAllowed(targets, state.territorialUnits)) throw new Error('잠금 또는 자식 관계 때문에 삭제할 수 없습니다.');
  state.territorialUnits = state.territorialUnits.filter(unit => !removed.has(String(unit.id)));
  state.territorialRelations = state.territorialRelations.filter(relation => !removed.has(String(relation.unitId)) && !removed.has(String(relation.parentId)));
  state.distributionEntries = state.distributionEntries.filter(entry => entry.mode !== territorialMode || !removed.has(String(entry.territorialUnitId)));
  for (const id of removed) {
    delete state.itemVisibility.subunits?.[id];
    delete state.itemVisibility.regions?.[id];
    for (const type of ['subunit', 'region']) {
      delete state.labelSettings?.[`${type}:${id}`];
      delete state.labelSettings?.[`territorial:${type}:${id}`];
    }
  }
}
