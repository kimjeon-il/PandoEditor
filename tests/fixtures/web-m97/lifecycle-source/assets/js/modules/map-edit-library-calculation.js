import { territorialRootId } from './territorial-units.js';
import { normalizePolygonGeometry, multiCoordinates, area, geometryBounds, boundsOverlap, featureId } from './map-edit-geometry.js';
import { createCountryImportMergePlanner } from './import-service.js';
import { validateCollection } from './gis-geometry-validation.js';
import { geometryAreaKm2 } from './geometry-metrics.js';

/** Ordered library calculation. checkpoint is the caller-owned cancellation/yield boundary. */
export async function calculateLibraryBatch(payload, originals, existingUnits, clipper, checkpoint) {
  const planner = createCountryImportMergePlanner({ clipper, clone: structuredClone,
    featureCountryId: featureId, countryName: feature => feature.properties?.name || featureId(feature),
    geometryBounds, boundsOverlap, normalizeGeometry: normalizePolygonGeometry, geometryCoordinates: multiCoordinates,
    planarArea: area, areaKm2: geometryAreaKm2, validateCountryCollection: () => ({ overlapAreaKm2: 0 }) });
  let draft = { type: 'FeatureCollection', features: originals };
  const originalsById = new Map(originals.map(feature => [featureId(feature), feature]));
  const affected = new Set(), donors = new Set(), transfers = [], impacts = [];
  let deleted = 0;
  const merge = async (feature, geometry = feature.geometry) => {
    const plan = await planner(draft, { type: 'FeatureCollection', features: [feature] }, 'territory-replacement');
    for (const id of plan.affectedIds) {
      if (originalsById.get(id)?.properties?.locked) throw new Error(`${id}: 잠긴 국가의 영토를 변경할 수 없습니다.`);
      affected.add(id);
    }
    for (const id of plan.donorIds) {
      donors.add(id);
      const before = draft.features.find(item => featureId(item) === id), after = plan.countriesData.features.find(item => featureId(item) === id);
      impacts.push({ id, name: before.properties?.name || id, area: geometryAreaKm2(before.geometry) - geometryAreaKm2(after?.geometry), deleted: !after });
    }
    deleted += plan.counts.deleted;
    transfers.push({ targetId: featureId(feature), geometry, donorIds: plan.donorIds });
    draft = plan.countriesData;
    await checkpoint();
  };
  for (const feature of payload.countries) await merge(feature);
  const all = new Map([...originals, ...existingUnits, ...payload.countries, ...payload.units].map(feature => [featureId(feature), feature]));
  const units = payload.units, unitIds = new Set(units.map(featureId)), groups = new Map();
  for (const unit of units.filter(unit => (unit.properties.entityKind === 'general' && !!unit.properties.parentId) && !unitIds.has(String(unit.properties.parentId)))) {
    const id = territorialRootId(unit, id => all.get(id));
    if (!groups.has(id)) groups.set(id, []);
    groups.get(id).push(unit.geometry);
  }
  for (const [id, geometries] of groups) {
    const owner = draft.features.find(feature => featureId(feature) === id);
    if (!owner) throw new Error('소속 국가가 영토 변경으로 사라집니다. 소속을 다시 선택하세요.');
    const geometry = normalizePolygonGeometry(clipper.union(...geometries.map(multiCoordinates)));
    if (!clipper.difference(multiCoordinates(geometry), multiCoordinates(owner.geometry)).length) continue;
    impacts.push({ id, name: owner.properties?.name || id, expansion: true });
    await merge({ ...owner, geometry: normalizePolygonGeometry(clipper.union(multiCoordinates(owner.geometry), multiCoordinates(geometry))) }, geometry);
  }
  const byId = new Map([...draft.features, ...existingUnits, ...units].map(feature => [featureId(feature), feature]));
  for (const unit of units.filter(unit => (unit.properties.entityKind === 'general' && !!unit.properties.parentId))) {
    const parent = byId.get(String(unit.properties.parentId));
    if (!parent || !draft.features.some(country => featureId(country) === territorialRootId(unit, id => byId.get(id)))
      || clipper.difference(multiCoordinates(unit.geometry), multiCoordinates(parent.geometry)).length) throw new Error(`${unit.properties.name}: 상위 단위에 포함되지 않습니다.`);
  }
  if (validateCollection(draft, [...affected], clipper).overlapAreaKm2 > 0.001) throw new Error('영토 변경 후 국가 간 중첩이 남아 추가할 수 없습니다.');
  const kept = new Set(draft.features.map(featureId));
  return { features: draft.features.filter(feature => affected.has(featureId(feature))), removedIds: originals.filter(feature => !kept.has(featureId(feature))).map(featureId),
    affectedIds: [...affected], transfers, impacts, donorIds: [...donors], deleted };
}
