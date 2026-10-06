(function(global) {
  'use strict';

  const TERRITORIAL_TABLES = Object.freeze({
    general: 'entities',
    regional: 'regions',
  });
  const DISTRIBUTION_TABLE = 'distributions';
  const TERRITORIAL_TYPES_BY_TABLE = Object.freeze({
    ...Object.fromEntries(Object.entries(TERRITORIAL_TABLES).map(([type, table]) => [table, type])),
  });
  const clone = value => value == null ? value : structuredClone(value);
  const text = value => String(value ?? '').trim();
  const polygonGeometry = geometry => ['Polygon', 'MultiPolygon'].includes(geometry?.type) ? clone(geometry) : null;
  const distributionValue = (value, row) => {
    const input = typeof value === 'string' ? value.trim() : value;
    if ((typeof input !== 'string' && typeof input !== 'number') || input === ''
      || !Number.isFinite(Number(input))) {
      throw new Error(`분포 ${row}행의 값은 유한한 숫자여야 합니다.`);
    }
    return Number(input);
  };
  const parseJson = (value, fallback = {}) => {
    if (!value) return clone(fallback);
    try {
      const parsed = typeof value === 'string' ? JSON.parse(value) : value;
      return parsed && typeof parsed === 'object' ? parsed : clone(fallback);
    } catch (_) {
      return clone(fallback);
    }
  };

  function countryGeometryIndex(state) {
    const index = new Map();
    for (const feature of state?.territorialEntities || []) {
      const id = text(feature?.id);
      if (id && polygonGeometry(feature?.geometry)) index.set(id, feature.geometry);
    }
    return index;
  }

  function territorialProperties(properties) {
    const current = structuredClone(properties || {});
    delete current.adminLevel;
    delete current.isRemainder;
    if (current.metadata) delete current.metadata.legacyTerritorialPartition;
    return current;
  }

  function territorialRows(state) {
    const rows = Object.fromEntries(Object.values(TERRITORIAL_TABLES).map(table => [table, []]));
    for (const item of state?.territorialEntities || []) {
      const properties = territorialProperties(item?.properties);
      const entityKind = text(properties.entityKind);
      const table = TERRITORIAL_TABLES[entityKind];
      const geometry = polygonGeometry(item?.geometry);
      if (!table || !geometry) continue;
      rows[table].push({
        geometry,
        id: text(item.id),
        name: text(properties.name),
        entity_kind: entityKind,
        parent_id: text(properties.parentId),
        valid_from: text(properties.validFrom),
        valid_to: text(properties.validTo),
        color: text(properties.style?.color),
        style_key: text(properties.style?.key),
        source_entity_id: text(properties.sourceEntityId),
        source_geometry_version: text(properties.sourceGeometryVersion),
        metadata_json: JSON.stringify(properties.metadata || {}),
        properties_json: JSON.stringify(properties),
      });
    }
    return rows;
  }

  function distributionRows(state) {
    const rows = { [DISTRIBUTION_TABLE]: [] };
    const layers = new Map((state?.distributionLayers || []).map(layer => [text(layer.id), layer]));
    const geometryIndex = countryGeometryIndex(state);
    for (const entry of state?.distributionEntries || []) {
      const layer = layers.get(text(entry.layerId));
      const sourceMode = text(entry.mode) === 'territorial' ? 'territorial' : 'geometry';
      const geometry = sourceMode === 'territorial' ? polygonGeometry(geometryIndex.get(text(entry.territorialUnitId))) : polygonGeometry(entry.geometry);
      if (!layer || !geometry) continue;
      rows[DISTRIBUTION_TABLE].push({
        geometry,
        entry_id: text(entry.id),
        layer_id: text(layer.id),
        name: text(layer.name),
        unit: text(layer.unit),
        value_scale_mode: layer.valueScale?.mode === 'manual' ? 'manual' : 'auto',
        value_scale_min: layer.valueScale?.mode === 'manual' ? layer.valueScale.min : null,
        value_scale_max: layer.valueScale?.mode === 'manual' ? layer.valueScale.max : null,
        parent_layer_id: text(layer.parentId),
        layer_groups_json: JSON.stringify(layer.groups || []),
        layer_valid_from: text(layer.validFrom),
        layer_valid_to: text(layer.validTo),
        color: text(layer.color),
        layer_visible: state?.itemVisibility?.distributions?.[text(layer.id)] === false ? 0 : 1,
        layer_locked: layer.locked === true ? 1 : 0,
        source_mode: sourceMode,
        territorial_unit_id: sourceMode === 'territorial' ? text(entry.territorialUnitId) : '',
        value: distributionValue(entry.value, entry.id),
        certainty: text(entry.certainty) || 'unknown',
        valid_from: text(entry.validFrom),
        valid_to: text(entry.validTo),
        layer_metadata_json: JSON.stringify(layer.metadata || {}),
        entry_metadata_json: JSON.stringify(entry.metadata || {}),
      });
    }
    return rows;
  }

  function importTerritorialFeature(feature, tableName, index, schemaVersion) {
    if (!Number.isInteger(schemaVersion)) throw new TypeError('Current territorial schema version is required');
    const properties = feature?.properties || {};
    const entityKind = TERRITORIAL_TYPES_BY_TABLE[tableName];
    const geometry = polygonGeometry(feature?.geometry);
    if (!entityKind || !geometry) return null;
    const currentProperties = territorialProperties(parseJson(properties.properties_json));
    const id = text(properties.id || feature.id);
    if (!id) throw new Error(`영역 원본 ID가 비어 있습니다: ${entityKind} ${index + 1}`);
    return {
      type: 'Feature',
      id,
      properties: {
        ...currentProperties,
        schemaVersion,
        entityKind,
        name: text(properties.name ?? currentProperties.name) || id,
        parentId: text(properties.parent_id ?? currentProperties.parentId),
        coverageMode: entityKind === 'regional' || !text(properties.parent_id ?? currentProperties.parentId) ? 'explicit' : text(currentProperties.coverageMode) || 'partition',
        validFrom: text(properties.valid_from ?? currentProperties.validFrom) || null,
        validTo: text(properties.valid_to ?? currentProperties.validTo) || null,
        style: {
          ...(currentProperties.style || {}),
          color: text(properties.color ?? currentProperties.style?.color),
          key: text(properties.style_key ?? currentProperties.style?.key),
        },
        sourceEntityId: text(properties.source_entity_id ?? currentProperties.sourceEntityId),
        sourceGeometryVersion: text(properties.source_geometry_version ?? currentProperties.sourceGeometryVersion),
        metadata: territorialProperties({ metadata: parseJson(properties.metadata_json, currentProperties.metadata || {}) }).metadata,
      },
      geometry,
    };
  }

  function importDistributionFeature(feature, tableName, index = 0) {
    const properties = feature?.properties || {};
    const geometry = polygonGeometry(feature?.geometry);
    if (tableName !== DISTRIBUTION_TABLE || !geometry) return null;
    const layerId = text(properties.layer_id) || `distribution:${index + 1}`;
    const entryId = text(properties.entry_id || feature.id) || `${layerId}:entry:${index + 1}`;
    const sourceMode = text(properties.source_mode) === 'territorial' && text(properties.territorial_unit_id) ? 'territorial' : 'geometry';
    const scaleMode = text(properties.value_scale_mode) || 'auto';
    if (!['auto', 'manual'].includes(scaleMode)) throw new Error(`분포 ${index + 1}행의 색 농도 방식이 올바르지 않습니다.`);
    const valueScale = scaleMode === 'manual'
      ? { mode: 'manual', min: distributionValue(properties.value_scale_min, index + 1), max: distributionValue(properties.value_scale_max, index + 1) }
      : { mode: 'auto' };
    if (valueScale.mode === 'manual' && valueScale.min >= valueScale.max) throw new Error(`분포 ${index + 1}행의 색 농도 범위가 올바르지 않습니다.`);
    return {
      layer: {
        id: layerId,
        schemaVersion: 3,
        name: text(properties.name) || layerId,
        unit: text(properties.unit),
        valueScale,
        color: text(properties.color) || '#8c68d8',
        locked: Number(properties.layer_locked ?? 0) === 1,
        parentId: text(properties.parent_layer_id),
        groups: parseJson(properties.layer_groups_json, []),
        validFrom: text(properties.layer_valid_from) || null,
        validTo: text(properties.layer_valid_to) || null,
        metadata: parseJson(properties.layer_metadata_json),
      },
      entry: {
        id: entryId,
        schemaVersion: 3,
        layerId,
        mode: sourceMode,
        territorialUnitId: sourceMode === 'territorial' ? text(properties.territorial_unit_id) : '',
        geometry: sourceMode === 'geometry' ? geometry : null,
        value: distributionValue(properties.value, index + 1),
        certainty: text(properties.certainty) || 'unknown',
        validFrom: text(properties.valid_from) || null,
        validTo: text(properties.valid_to) || null,
        metadata: parseJson(properties.entry_metadata_json),
      },
    };
  }

  function mergeDistributionFeatures(collections, existingLayers = []) {
    const layerMap = new Map((existingLayers || []).map(layer => [text(layer.id), clone(layer)]));
    const entries = [];
    const entryIds = new Set();
    for (const { tableName, features } of collections || []) {
      for (let index = 0; index < (features || []).length; index += 1) {
        const imported = importDistributionFeature(features[index], tableName, index);
        if (!imported) continue;
        const current = layerMap.get(imported.layer.id);
        if (current && (current.unit !== imported.layer.unit
          || JSON.stringify(current.valueScale) !== JSON.stringify(imported.layer.valueScale))) {
          throw new Error(`분포 레이어 ${imported.layer.id}의 단위 또는 색 농도 범위가 일치하지 않습니다.`);
        }
        layerMap.set(imported.layer.id, { ...(current || {}), ...imported.layer });
        if (entryIds.has(imported.entry.id)) throw new Error(`분포 엔트리 ID 충돌: ${imported.entry.id}`);
        entryIds.add(imported.entry.id);
        entries.push(imported.entry);
      }
    }
    return { layers: [...layerMap.values()], entries };
  }

  global.PandoLabGisAdapters = Object.freeze({
    TERRITORIAL_TABLES,
    TERRITORIAL_TYPES_BY_TABLE,
    DISTRIBUTION_TABLE,
    countryGeometryIndex,
    territorialRows,
    distributionRows,
    importTerritorialFeature,
    importDistributionFeature,
    mergeDistributionFeatures,
  });
})(globalThis);
