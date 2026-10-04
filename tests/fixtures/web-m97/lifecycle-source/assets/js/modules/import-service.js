import { EXCHANGE_TARGETS, normalizeExchangeTarget } from './exchange-adapter-registry.js';
import { createGisImportPlan } from './gis-import-plan.js';
import {
  WORKER_RPC_ERROR_CATEGORIES,
  createWorkerRpcClient,
} from './worker-rpc.js';

export const GIS_GEOMETRY_TIMEOUT_MS = 60_000;

const text = value => String(value ?? '');

function createLegacyGisGeometryCodec() {
  return Object.freeze({
    encodeRequest(envelope) {
      return {
        id: envelope.requestId,
        action: envelope.operation === 'gis.validate' ? 'validate' : envelope.operation,
        collection: envelope.payload?.collection,
        affectedIds: envelope.payload?.affectedIds || null,
        projectRevision: envelope.projectRevision,
        priority: envelope.priority,
      };
    },
    encodeCancel(envelope) {
      return { id: envelope.requestId, action: 'cancel', reason: envelope.reason || 'cancelled' };
    },
    encodeEvent(envelope) {
      return { action: envelope.operation, ...(envelope.payload || {}) };
    },
    decodeMessage(message) {
      if (!message || message.id == null) return null;
      const { id, ok, error, ...result } = message;
      return {
        kind: 'result',
        requestId: Number(id || 0),
        operation: 'gis.validate',
        projectRevision: Number(message.projectRevision || 0),
        ok: ok === true,
        result,
        error: ok === true ? null : {
          category: WORKER_RPC_ERROR_CATEGORIES.OPERATION,
          code: 'PL-GIS-GEOMETRY',
          message: error || 'GIS 지오메트리 검증에 실패했습니다.',
        },
      };
    },
  });
}

export function createGisGeometryValidator({ createWorker, timeoutMs = GIS_GEOMETRY_TIMEOUT_MS }) {
  let rpc = null;

  function ensureRpc() {
    if (rpc) return rpc;
    rpc = createWorkerRpcClient({
      createWorker,
      codec: createLegacyGisGeometryCodec(),
      defaultTimeoutMs: timeoutMs,
      restartOnCrash: true,
    });
    return rpc;
  }

  function dispose() {
    rpc?.stop('disposed');
    rpc = null;
  }

  async function validate(collection, affectedIds = null) {
    const scopedIds = affectedIds
      ? [...new Set([...affectedIds].map(text).filter(Boolean))]
      : null;
    try {
      const response = await ensureRpc().request('gis.validate', {
        collection,
        affectedIds: scopedIds?.length ? scopedIds : null,
      }, { timeoutMs, priority: 300 });
      return response.result;
    } catch (error) {
      if (error?.category === WORKER_RPC_ERROR_CATEGORIES.TIMEOUT) dispose();
      throw error;
    }
  }

  return Object.freeze({ dispose, validate, stats: () => rpc?.stats?.() || Object.freeze({ pendingCount: 0, workerActive: false }) });
}

export function createCountryImportMergePlanner({
  clipper,
  clone,
  featureCountryId,
  countryName,
  geometryBounds,
  boundsOverlap,
  normalizeGeometry,
  geometryCoordinates,
  planarArea,
  areaKm2,
  validateCountryCollection,
}) {
  function mergeImportedCountryProperties(existing, imported, geometry) {
    const importedId = text(existing?.id || imported?.id);
    const existingProperties = existing?.properties || {};
    const importedProperties = imported?.properties || {};
    const properties = {
      name: text(existingProperties.name || importedProperties.name || importedId),
    };
    const validFrom = text(existingProperties.validFrom || importedProperties.validFrom);
    const validTo = text(existingProperties.validTo || importedProperties.validTo);
    if (validFrom) properties.validFrom = validFrom;
    if (validTo) properties.validTo = validTo;
    return {
      type: 'Feature',
      id: importedId,
      properties,
      geometry,
    };
  }

  return async function planMerge(currentCountries, importedCountries, strategy) {
    if (!clipper?.union || !clipper?.difference || !clipper?.intersection) throw new Error('국가 병합 연산 엔진을 불러오지 못했습니다.');
    // Only an affected wrapper is written; source geometries remain read-only.
    const current = (currentCountries?.features || []).map(feature => ({ ...feature }));
    const imported = (clone(importedCountries)?.features || []).map((feature, index) => {
      feature.id = featureCountryId(feature, index);
      return feature;
    });
    if (!imported.length) throw new Error('병합할 국가 객체가 없습니다.');
    const incomingIds = imported.map(feature => text(feature?.id));
    if (incomingIds.some(id => !id) || new Set(incomingIds).size !== incomingIds.length) {
      throw new Error('가져온 국가 ID 연결 결과가 비어 있거나 중복되었습니다.');
    }

    const currentById = new Map(current.map(feature => [text(feature?.id), feature]));
    const importedById = new Map(imported.map(feature => [text(feature?.id), feature]));
    const importedIds = new Set(importedById.keys());
    const affectedIds = new Set(importedIds);
    const matched = [...importedIds].filter(id => currentById.has(id)).length;
    const counts = {
      matched,
      added: imported.length - matched,
      replaced: 0,
      subtracted: 0,
      deleted: 0,
      overlapAreaKm2: 0,
      residualOverlapAreaKm2: 0,
    };
    let result;

    if (strategy === 'id-replace') {
      counts.replaced = matched;
      result = current.filter(feature => !importedIds.has(text(feature?.id)));
      result.push(...imported.map(feature => mergeImportedCountryProperties(
        currentById.get(text(feature?.id)),
        feature,
        clone(feature.geometry),
      )));
      const unaffected = result.filter(feature => !importedIds.has(text(feature?.id)));
      for (const incoming of imported) {
        for (const other of unaffected) {
          if (!boundsOverlap(geometryBounds(incoming.geometry), geometryBounds(other.geometry))) continue;
          const overlap = normalizeGeometry(clipper.intersection(incoming.geometry.coordinates, other.geometry.coordinates));
          if (!overlap || planarArea(geometryCoordinates(overlap)) <= 1e-8) continue;
          counts.overlapAreaKm2 += areaKm2(overlap);
        }
      }
      return {
        countriesData: { type: 'FeatureCollection', features: result },
        counts,
        affectedIds: [...affectedIds],
        canCommit: counts.overlapAreaKm2 <= 0.001,
      };
    }

    const importedRawUnion = normalizeGeometry(clipper.union(...imported.map(feature => feature.geometry.coordinates)));
    if (!importedRawUnion) throw new Error('가져온 영토를 결합할 수 없습니다.');
    const donorIds = [];
    result = [];
    for (const existing of current) {
      const id = text(existing?.id);
      if (importedIds.has(id)) continue;
      if (!boundsOverlap(geometryBounds(existing.geometry), geometryBounds(importedRawUnion))) {
        result.push(existing);
        continue;
      }
      const overlap = normalizeGeometry(clipper.intersection(existing.geometry.coordinates, importedRawUnion.coordinates));
      if (!overlap || planarArea(geometryCoordinates(overlap)) <= 1e-8) {
        result.push(existing);
        continue;
      }
      counts.overlapAreaKm2 += areaKm2(overlap);
      const remainder = normalizeGeometry(clipper.difference(existing.geometry.coordinates, importedRawUnion.coordinates));
      affectedIds.add(id);
      donorIds.push(id);
      counts.subtracted += 1;
      if (!remainder) {
        counts.deleted += 1;
        continue;
      }
      existing.geometry = remainder;
      result.push(existing);
    }
    for (const incoming of imported) {
      const id = text(incoming?.id);
      const existing = currentById.get(id);
      const geometry = clone(incoming.geometry);
      if (!geometry) throw new Error(`${countryName(incoming)} 영토를 결합할 수 없습니다.`);
      result.push(mergeImportedCountryProperties(existing, incoming, geometry));
    }
    const countriesData = { type: 'FeatureCollection', features: result };
    counts.residualOverlapAreaKm2 = (await validateCountryCollection(countriesData, affectedIds)).overlapAreaKm2;
    return {
      countriesData,
      counts,
      affectedIds: [...affectedIds],
      donorIds,
      transferredGeometry: clone(importedRawUnion),
      canCommit: counts.residualOverlapAreaKm2 <= 0.001,
    };
  };
}

export function applyImportedPackageAssets(metadata, entities) {
  const assets = new Map((metadata?.countryAssets || []).map(asset => [text(asset.countryId), asset]));
  return entities.map(entity => {
    const asset = (entity.properties.entityKind === 'general' && !entity.properties.parentId) && assets.get(text(entity.id));
    if (!asset?.base64) return entity;
    return { ...entity, properties: { ...entity.properties, metadata: { ...entity.properties.metadata,
      flagDataUrl: `data:${asset.mimeType || 'application/octet-stream'};base64,${asset.base64}` } } };
  });
}

export function appendImportedSourceInfo(previous, next, now = () => new Date().toISOString()) {
  const imports = [];
  const append = value => {
    if (!value) return;
    if (Array.isArray(value.imports)) imports.push(...value.imports);
    else imports.push(value);
  };
  append(previous);
  append(next);
  return { mergedAt: now(), imports };
}

export function createImportService({
  openImportWizard,
  getWizardOptions,
  validateStructuredGeometry,

  exchangeRegistry = null,
  getProjectGeneration = () => 0,

}) {
  const buildPlan = (kind, result, context, extra = {}) => createGisImportPlan({
    kind,
    projectGeneration: getProjectGeneration(),
    source: { fileName: context.fileName, sourceKind: result.sourceKind || result.importPlan?.sourceKind || '' },
    payload: { result, ...extra },
    affectedIds: extra.plan?.affectedIds || [],
    render: kind === 'project-replace'
      ? { kind: 'territorial-patch', domain: 'territorial' }
      : kind === 'territorial'
        ? { kind: 'territorial-patch', domain: 'territorial' }
        : kind === 'distribution'
          ? { kind: 'overlay-geometry', domain: 'distribution' }
          : { kind: 'generic-patch', domain: 'generic' },
    summary: extra.plan?.counts || result.summary || {},
  });

  async function openFiles(files, { targetType = '', sourceKind = '' } = {}) {
    if (!files?.length) return { status: 'empty' };
    const result = await openImportWizard(files, {
      targetType,
      sourceKind,
      ...getWizardOptions(),
    });
    const resolvedTarget = normalizeExchangeTarget(result.importPlan?.targetType || result.targetType, '');
    if (!resolvedTarget) throw new Error('가져올 객체 종류가 올바르지 않습니다.');
    const context = { fileName: files[0]?.name || '벡터 파일' };

    if (result.sourceKind === 'project' || result.importPlan?.sourceKind === 'project') {
      return { status: 'planned', plan: buildPlan('project-replace', result, context) };
    }
    if (['general', 'regional'].includes(resolvedTarget)) {
      return { status: 'planned', plan: buildPlan('territorial', result, context) };
    }
    const importedFeatures = result.collection?.features || [];
    const structuredIssues = importedFeatures.flatMap(validateStructuredGeometry);
    if (structuredIssues.length) throw new Error(`가져온 geometry가 올바르지 않습니다. ${structuredIssues[0].message}`);
    const kind = resolvedTarget === EXCHANGE_TARGETS.DISTRIBUTION ? 'distribution' : 'generic';
    return { status: 'planned', plan: buildPlan(kind, result, context) };
  }

  return Object.freeze({ openFiles, exchangeRegistry });
}
