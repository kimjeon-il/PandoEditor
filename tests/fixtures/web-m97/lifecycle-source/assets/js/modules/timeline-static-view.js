const unsupported = () => { throw Object.assign(new Error('시간 기록의 활성화는 T3/T4 구현 후 지원합니다.'), { code: 'TIMELINE_ACTIVATION' }); };

/** Capability boundary for the existing editor, not a month/date resolver. */
export function assertStaticTimeline(records, entities) {
  if (!records || !Array.isArray(entities)) unsupported();
  for (const entity of entities) for (const name of ['lifetimes', 'geometryBindings', 'parentRelations']) {
    const rows = records[name].filter(record => record.entityId === entity.id);
    if (rows.length !== 1 || rows[0].validFrom !== null || rows[0].validTo !== null) unsupported();
  }
  return true;
}

export function staticTimelineViews(identities, records, geometries) {
  assertStaticTimeline(records, identities);
  const binding = new Map(records.geometryBindings.map(record => [record.entityId, record]));
  const parents = new Map(records.parentRelations.map(record => [record.entityId, record]));
  return identities.map(identity => {
    const freeze = value => { if (value && typeof value === 'object') {
      for (const child of Object.values(value)) freeze(child); Object.freeze(value);
    } return value; };
    freeze(identity.properties.style);
    freeze(identity.properties.metadata);
    const p = parents.get(identity.id), properties = { ...identity.properties };
    Object.defineProperties(properties, {
      parentId: { enumerable: true, get: () => p.parentId },
      coverageMode: { enumerable: true, get: () => p.coverageMode },
      validFrom: { enumerable: true, get: () => null },
      validTo: { enumerable: true, get: () => null },
    });
    const feature = { ...identity, properties: Object.freeze(properties) };
    Object.defineProperty(feature, 'geometry', { enumerable: true,
      get: () => geometries.get(binding.get(identity.id).geometryRef) });
    return Object.freeze(feature);
  });
}
