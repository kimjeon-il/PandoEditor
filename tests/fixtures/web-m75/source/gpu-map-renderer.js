import { createGpuCanvasWorker } from './gpu-canvas-worker.js';
import { drawGpuBaseScene } from './gpu-base-scene-pass.js';
import { drawGpuInteractionPass } from './gpu-interaction-pass.js';
import { prepareGpuBaseScene, prepareGpuInteraction, prepareGpuInteractionPlan } from './gpu-scene-preparation.js';
import { createGpuResourceLifecycle, createGpuUploadScope } from './gpu-resource-lifecycle.js';
import { createGpuWorkerChannels } from './gpu-worker-channels.js';
import { createGpuTerrainPreparation } from './gpu-terrain-preparation.js';
import { TERRAIN_DEM_FORMAT, TERRAIN_RASTER_DATASET, TERRAIN_RASTER_VERSION, terrainAssetUrl, validateTerrainManifest } from './terrain-manifest.js';
import { terrainDemFragmentSource } from './terrain-dem-shaders.js';
import { resolveMapInteractionStyle } from './map-interaction-style.js';
import '../workers/canvas-scene-composition-core.js';
import { decodeCountryMesh } from './country-mesh-codec.js';
import { createCountryTriangleRangeMap } from './gpu-country-ranges.js';
export { countryDrawRangesForFrame, createCountryTriangleRangeMap, mergeCountryDrawRanges } from './gpu-country-ranges.js';
import { createRenderDevice } from './render-device.js';
import { createSceneColorCache } from './scene-color-cache.js';
import { createGpuPolygonOverlayPass } from './gpu-polygon-overlay-pass.js';
import { createGpuStrokeRenderer } from './gpu-stroke-renderer.js';
import { resetGpuNormalBlend } from './gpu-blend-utils.js';
import { linkGpuProgram } from './gpu-shader-utils.js';
import { GPU_VIEW_UNIFORM_NAMES, setGpuViewUniforms } from './gpu-view-uniforms.js';
import { createGpuHydroPreparation } from './gpu-hydro-preparation.js';
import { createBuiltinMeshResourceLoader } from './builtin-mesh-resource.js';
import { isRenderScene } from './render-scene.js';
import { isMapVisualFrame } from './map-visual-frame.js';
import { createGpuMeshWorkerJobs } from './gpu-mesh-worker-jobs.js';
import { decideCountryPatchPresentation } from './country-mesh-quality-gate.js';

const DEFAULT_RENDER_QUALITY = Object.freeze({
  tier: 'high',
  phase: 'settle',
  revision: 0,
  countryMeshQuality: 'canonical',
  dprCap: 3,
  terrainResolutionScale: 1,
  terrainCacheBudgetBytes: 128 * 1024 * 1024,
  hydroCacheBudgetBytes: 96 * 1024 * 1024,
  overlayGpuBudgetBytes: 192 * 1024 * 1024,
  uploadBudgetBytes: 8 * 1024 * 1024,
});

export function resolveRenderPixelRatioValue(devicePixelRatio, mobileLayout = false, qualityCap = Infinity) {
  const deviceRatio = Math.max(1, Number(devicePixelRatio || 1));
  const cap = Math.max(1, Number(qualityCap) || Infinity);
  return Math.min(mobileLayout ? 2 : 3, cap, deviceRatio);
}

export function visibleFlatWorldOffsets({
  translateX,
  scale,
  flatCenterRadians = 0,
  viewportWidth,
  minimumOverlap = 0.5,
} = {}) {
  const resolvedTranslate = Number(translateX);
  const resolvedScale = Math.abs(Number(scale));
  const resolvedCenter = Number(flatCenterRadians);
  const resolvedWidth = Math.max(1, Number(viewportWidth));
  if (![resolvedTranslate, resolvedScale, resolvedCenter, resolvedWidth].every(Number.isFinite) || resolvedScale <= 0) {
    return [0];
  }
  const overlapThreshold = Math.max(0, Number(minimumOverlap) || 0);
  const candidates = [-2 * Math.PI, 0, 2 * Math.PI];
  const visible = candidates.filter(worldOffset => {
    const left = resolvedTranslate + resolvedScale * (-Math.PI + worldOffset - resolvedCenter);
    const right = resolvedTranslate + resolvedScale * (Math.PI + worldOffset - resolvedCenter);
    return Math.min(right, resolvedWidth) - Math.max(left, 0) > overlapThreshold;
  });
  if (visible.length) return visible;
  const viewportCenter = resolvedWidth / 2;
  return [candidates.reduce((nearest, worldOffset) => {
    const center = resolvedTranslate + resolvedScale * (worldOffset - resolvedCenter);
    const distance = Math.abs(center - viewportCenter);
    return distance < nearest.distance ? { worldOffset, distance } : nearest;
  }, { worldOffset: 0, distance: Infinity }).worldOffset];
}

export function createCountryGeometryRevisionTracker() {
  let committedRevision = 0;
  let displayedRevision = 0;
  let taskToken = 0;
  const pendingRevisions = new Map();

  const normalizedIds = ids => [...new Set([...(ids || [])].map(String).filter(Boolean))];

  function beginCommit(ids) {
    committedRevision += 1;
    taskToken += 1;
    const normalized = normalizedIds(ids);
    for (const id of normalized) pendingRevisions.set(id, committedRevision);
    return { ids: normalized, revision: committedRevision, token: taskToken };
  }

  function beginTask(revision = committedRevision) {
    taskToken += 1;
    return { revision: Number(revision || 0), token: taskToken };
  }

  function isCurrent(token, revision) {
    return Number(token) === taskToken && Number(revision) === committedRevision;
  }

  function markDisplayed(ids, revision) {
    const numericRevision = Number(revision || 0);
    if (numericRevision < committedRevision) return [];
    const cleared = [];
    for (const id of normalizedIds(ids)) {
      const pendingRevision = pendingRevisions.get(id);
      if (pendingRevision === undefined || pendingRevision > numericRevision) continue;
      pendingRevisions.delete(id);
      cleared.push(id);
    }
    displayedRevision = Math.max(displayedRevision, numericRevision);
    return cleared;
  }

  function reset() {
    taskToken += 1;
    pendingRevisions.clear();
    committedRevision += 1;
    return committedRevision;
  }

  return Object.freeze({
    beginCommit,
    beginTask,
    isCurrent,
    markDisplayed,
    reset,
    committedRevision: () => committedRevision,
    displayedRevision: () => displayedRevision,
    taskToken: () => taskToken,
    pendingIds: () => [...pendingRevisions.keys()],
    pendingRevision: id => pendingRevisions.get(String(id)),
    isPending: id => pendingRevisions.has(String(id)),
  });
}

export function createGpuMapRenderer(deps) {
  const {
    APP_VERSION,
    ASSET_REVISION,
    DATA_REVISION,
    PHYSICAL_DATA_BASE_URL,
    TERRAIN_RASTER_MANIFEST_URL,
    onTerrainSourceChanged,
    activeProjection,
    countryColor,
    countryFeatureById,
    countryOutlineFeature,
    d3,
    deepClone,
    defaultCountryColor,
    flatProjection,
    getSystemTheme,
    globeProjection,
    hydroDisplayColor,
    hydroFeatureById,
    hydroVisibilityThreshold,
    isCountryVisibleById,
    isHydroFeatureVisible,
    isLayerItemVisible,
    isMobile,
    isSafeKoreanErrorMessage,
    mapTheme,
    mapWorkScheduler,
    prepareHydroFeature,
    queueMapResize,
    renderPendingCountryOverlays,
    renderViewFrame,
    reportOperationError,
    rendererUi,
    runtimeAssetUrl,
    scheduleGpuFrame,
    scheduleGpuInteractionFrame,
    scheduleGpuMeshRebuild,
    setActionStatus,
    state,
  } = deps;
  return (() => {
    const lifecycle = createGpuResourceLifecycle();
    const workerChannels = createGpuWorkerChannels();
    let disposed = false;
    let canvasWorkerNeedsRestart = false;
    let canvas = null;
    let gl = null;
    let glVersion = 0;
    let renderDevice = null;
    let renderDeviceContextRevision = 0;
    let uploadScheduler = null;
    let stagingSequence = 0;
    let pendingCanonicalCommit = null;
    let projectGeneration = 0;
    let projectRenderBlocked = false;
    let builtinMeshBaseline = null;
    const builtinMeshResourceLoader = createBuiltinMeshResourceLoader({ runtimeAssetUrl, WorkerClass: workerChannels.WorkerClass });
    let renderScene = null;
    let renderInteractionState = Object.freeze({
      selectionPacket: null,
      genericFillItems: Object.freeze([]),
      previewPackets: Object.freeze([]),
      draftPackets: Object.freeze([]),
    });
    let selectionPass = null;
    let lastSelectionRenderResult = null;
    let lastBaseSceneResult = null;
    const sceneColorCache = createSceneColorCache();
    const interactionFillCache = createSceneColorCache();
    const interactionStrokeCache = createSceneColorCache({ nearestSampling: true });
    const polygonOverlayPass = createGpuPolygonOverlayPass({
      onResourceReady: key => overlayResourceReady(key),
      onError: payload => console.warn(`[${payload?.stage || 'gpu-polygon-overlay'}]`, payload?.error || payload),
    });
    const strokeRenderer = createGpuStrokeRenderer({
      isInputActive: () => interactionActive,
      getUploadBudget: () => renderQuality.uploadBudgetBytes,
      onResourceReady: key => overlayResourceReady(key),
      onError: payload => console.warn(`[${payload?.stage || 'gpu-stroke'}]`, payload?.error || payload),
    });
    let sceneCacheFallbackFrame = false;
    let sceneCacheFullDrawCount = 0;
    let sceneCacheInteractionDrawCount = 0;
    let sceneCacheSelectionOnlyBaseDrawCount = 0;
    let sceneCacheReprojectCount = 0;
    let lastRenderSceneRevision = 0;
    const rendererStartedAt = performance.now();
    let firstCanonicalFrameMs = null;
    let canonicalFrameFallbackCount = 0;
    let renderQuality = DEFAULT_RENDER_QUALITY;
    let meshSwitchCount = 0;
    let renderQualityChangeCount = 0;
    // Preview data is a one-way startup fallback. Once canonical geometry is
    // promoted it is never selected again for interaction or adaptive quality.
    let qualityPhase = 'startup-preview';
    let previewAllowed = true;
    let previewActivationCount = 0;
    let previewActivationAfterCanonical = 0;
    let canonicalPromotionCount = 0;
    let canonicalPromotionError = '';
    let canonicalReadyFrameId = 0;
    function recordCanonicalFrameReady() {
      if (firstCanonicalFrameMs === null) {
        firstCanonicalFrameMs = Math.max(0, performance.now() - rendererStartedAt);
      }
    }
    function recordSceneCacheFallback() {
      if (!sceneCacheFallbackFrame) canonicalFrameFallbackCount += 1;
      sceneCacheFallbackFrame = true;
    }
    let webGlContextKind = '';
    let uintIndexExtension = null;
    let ctx2d = null;
    let rendererMode = 'pending';
    let fillProgram = null;
    let landMaskProgram = null;
    let lineProgram = null;
    let pickProgram = null;
    let terrainProgram = null;
    let terrainDemProgram = null;
    let hydroFillProgram = null;
    let hydroLineProgram = null;
    let hydroPickProgram = null;
    let hydroLinePickProgram = null;
    let countryStateFillProgram = null;
    let countryStateQuadBuffer = null;
    let hydroCornerBuffer = null;
    let instancedExtension = null;
    let hydroVisibilityTexture = null;
    let hydroVisibilityWidth = 1;
    let hydroVisibilityHeight = 1;
    const hydroEditFeatureByFid = new Map();
    let interactionActive = false, hydroVisibilityDirty = true;
    const hydroPreparation = createGpuHydroPreparation({
      createWorker: () => workerChannels.create(runtimeAssetUrl('workers/hydro-tile-worker.js'), { name: 'pandolab-hydro-tiles' }),
      getMode: () => rendererMode, isMobile, DATA_REVISION, ASSET_REVISION,
      getCacheBudget: () => renderQuality.hydroCacheBudgetBytes,
      getProtectedPackIds: () => { const feature = state.selected?.type === 'hydro' ? hydroFeatureById(state.selected.id) : null;
        return feature?.properties?.pack_ids || [feature?.properties?.pack_id].filter(Number.isFinite); },
      getView: () => hydroViewSnapshot(), registerHydroFragments, registerHydroDescriptors, unregisterHydroFragments,
      queueHydroRender, reportOperationError, setActionStatus, onConnect: connectHydroCanvasWorkers,
      onLoadState: status => Object.assign(state.physicalLoadState, status),
      onReset: () => { hydroVisibilityDirty = true; hydroEditFeatureByFid.clear(); state.hydroFragmentsByLogicalId = new Map();
        setHydroEdits(state.hydroEdits || [], Number(state.stateRevision || 0)); },
    });
    let fillVao = null;
    let lineVao = null;
    let positionBuffer = null;
    let countryBuffer = null;
    let fillIndexBuffer = null;
    let lineIndexBuffer = null;
    let paletteTexture = null;
    let overridePaletteTexture = null;
    let emphasisPaletteTexture = null;
    let overrideEmphasisPaletteTexture = null;
    let paletteCapacity = 0;
    let palettePixels = null;
    const paletteDirty = { base: true, emphasis: true };
    const pendingEmphasisCountryIds = new Set();
    let emphasisPaletteFullDirty = true;
    let uniformLocationCache = new WeakMap();
    let attributeLocationCache = new WeakMap();
    let activeFrameContext = null;
    let lastVisualFrame = null;
    let framePresentationListener = null;
    let interactionStyle = resolveMapInteractionStyle();
    let countryEmphasis = { primaryId: '', primaryIds: new Set(), priorities: {}, hoverId: '', selectedIds: new Set() };
    let countryEmphasisRevision = 0;
    let lastInteractionFillResult = null;
    let countryPaletteRevision = 0;
    let physicalStyleStateRevision = 0;
    let overridePositionBuffer = null;
    let overrideCountryBuffer = null;
    let overrideFillIndexBuffer = null;
    let overrideLineIndexBuffer = null;
    let overrideFillVao = null;
    let overrideLineVao = null;
    let overrideMesh = null;
    const countryOverrideIds = new Set();
    // IDs changed while the 50m startup preview is painted. They are retained
    // in project state immediately, but their 10m override is deferred until
    // the canonical base can be installed in the same frame.
    const deferredCountryPatchIds = new Set();
    const overrideFeatureSnapshots = new Map();
    const geometryRevisionTracker = createCountryGeometryRevisionTracker();
    let countryPatchPresentation = null;
    let pendingOldMeshVisibleCount = 0;
    const patchJobScheduler = createGpuMeshWorkerJobs({
      createWorker: () => workerChannels.create(runtimeAssetUrl('workers/gpu-mesh-worker.js'), { name: 'pandolab-country-patch-mesh' }),
      createRebuildWorker: () => workerChannels.create(runtimeAssetUrl('workers/gpu-mesh-worker.js'), { name: 'pandolab-gpu-mesh' }),
      isCurrent: entry => geometryRevisionTracker.isCurrent(entry.payload?.token, entry.geometryRevision),
      onMesh: () => { if (lastGeometryCommitTimings) lastGeometryCommitTimings.patchWorkerCompletedAt = performance.now(); },
      onError: event => { console.error('[PL-GPU-PATCH-001]', event.message || event); scheduleGpuMeshRebuild(0); },
    });
    let terrainManifest = null;
    let terrainManifestUrl = null;
    let terrainFallbackPending = false;
    let demShadeBlend = 0;
    let demSettleStartedAt = 0;
    let demShadeTimer = null;
    const terrainPreparation = createGpuTerrainPreparation({
      isMobile, invalidate: invalidatePhysicalScene, geoDistance: (...args) => d3.geo.distance(...args),
      tileUrl: spec => {
        const relative = terrainManifest.urlTemplate.replace('{level}', String(spec.level)).replace('{column}', String(spec.column)).replace('{row}', String(spec.row));
        return terrainAssetUrl(relative, { manifestUrl: terrainManifestUrl,
          dataBaseUrl: PHYSICAL_DATA_BASE_URL,
          revision: terrainManifest.representation === TERRAIN_DEM_FORMAT
            ? terrainManifest.assetsSha256.slice(0, 12) : DATA_REVISION || terrainManifest.version || APP_VERSION });
      },
      tintUrl: () => terrainAssetUrl(terrainManifest.tint.url, { manifestUrl: terrainManifestUrl,
        dataBaseUrl: PHYSICAL_DATA_BASE_URL, revision: terrainManifest.assetsSha256.slice(0, 12) }),
      onUnusable: () => { void fallbackTerrainToRaster('DEM 타일을 사용할 수 없습니다.'); },
    });
    let preparedTerrain = [];
    function prepareTerrain(frame) {
      if (terrainManifest?.representation === TERRAIN_DEM_FORMAT) {
        const highQuality = renderQuality.phase === 'settle' && renderQuality.tier !== 'coarse';
        if (!highQuality) { demShadeBlend = 0; demSettleStartedAt = 0; }
        else {
          if (!demSettleStartedAt) demSettleStartedAt = performance.now();
          demShadeBlend = Math.min(1, (performance.now() - demSettleStartedAt) / 180);
          if (demShadeBlend < 1 && demShadeTimer === null) demShadeTimer = setTimeout(() => {
            demShadeTimer = null;
            if (terrainManifest?.representation === TERRAIN_DEM_FORMAT && !disposed) {
              invalidatePhysicalScene('terrain-shade-transition');
            }
          }, 16);
        }
      }
      terrainPreparation.setContext({ gl, ready: isWebGlRenderer(), scheduler: uploadScheduler, projectGeneration, contextGeneration: renderDeviceContextRevision });
      preparedTerrain = terrainPreparation.prepare(frame, {
        visible: state.physicalSettings.terrainVisible, enhanced: state.dataReadiness === 'enhanced',
        projection: state.projection, rotation: state.view.globeRotation, flatCenter: state.view.flatCenter,
        width: cssWidth, height: cssHeight, dpr: effectivePixelRatio, devicePixelRatio: window.devicePixelRatio,
        cacheBudgetBytes: renderQuality.terrainCacheBudgetBytes,
      });
    }
    function renderTerrain(pass = 'land') { for (const tile of preparedTerrain) drawTerrainTile(tile, pass); }
    let mesh = null;
    let meshCountryIds = [];
    const countryStrokePacketCache = {
      preview: { mesh: null, countryIds: null, revision: '', resource: null },
      canonical: { mesh: null, countryIds: null, revision: '', resource: null },
      override: { mesh: null, countryIds: null, revision: '', resource: null },
    };
    const countryStrokeMeshRevisions = new WeakMap();
    let countryStrokeMeshRevisionSequence = 0;
    let meshQuality = 'preview';
    let activeMeshQuality = 'preview';
    let canonicalMeshReady = false;
    const meshVariants = new Map();
    let pickCount = 0;
    let pickReadPixelsMs = 0;
    let pickLastReadPixelsMs = 0;
    let pickSceneKey = '';
    let pickSceneRenderCount = 0;
    let effectivePixelRatio = 1;
    let pixelWidth = 0;
    let pixelHeight = 0;
    let cssWidth = 0;
    let cssHeight = 0;
    let resizePending = true;
    let pickFramebuffer = null;
    let pickTexture = null;
    let activeRenderViewState = null;
    let lastSceneFrameContext = null;
    let canvasWorker = null;
    let canvasWorkerUrl = null;
    let canvasWorkerBitmapContext = null;
    let canvasWorker2dContext = null;
    let canvasStyleRevision = 0;
    let canvasPhysicalStyleRevision = 0;
    let canvasLastStyleSignature = '';
    let canvasDisplayedStyleRevision = 0;
    let canvasLastPhysicalStyleSignature = '';
    let fallbackReason = '';
    let layoutMismatchCount = 0;
    let lastLayoutMismatchCssPx = 0;
    let layoutVerificationFrame = 0;
    let webglRecoveryTimer = 0;
    let webglContextLost = false;
    let currentRenderRevision = 0;
    let displayedRenderRevision = 0;
    let previewFramePresented = false;
    const previewFrameWaiters = new Set();
    function markPreviewFramePresented() {
      if (previewFramePresented || !previewAllowed || canonicalMeshReady
        || !state.countriesData?.features?.length) return;
      previewFramePresented = true;
      for (const resolve of previewFrameWaiters) resolve(true);
      previewFrameWaiters.clear();
    }
    function waitForPreviewFrame() {
      if (previewFramePresented) return Promise.resolve(true);
      return new Promise(resolve => previewFrameWaiters.add(resolve));
    }
    const frameTimes = [];
    const performanceMetrics = {
      paletteRebuildCount: 0,
      paletteUploadCount: 0,
      paletteUploadBytes: 0,
      uniformCacheHits: 0,
      uniformCacheMisses: 0,
      attributeCacheHits: 0,
      attributeCacheMisses: 0,
      frameContextBuildCount: 0,
      visualFrameConsumeCount: 0,
      hydroViewRequestCount: 0,
      hydroTileWindowCacheHitCount: 0,
      hydroTileWindowRecomputeCount: 0,
      hydroUploadBytes: 0,
      terrainIncompleteFrameCount: 0,
      canvasWorkerMessageCount: 0,
      canvasWorkerMessageBytes: 0,
      canvasWorkerViewMessageCount: 0,
      canvasWorkerStaleFrameCount: 0,
      canvasWorkerStateMessageCount: 0,
      canvasWorkerMessagesByType: {},
      baseSceneDrawCount: 0,
      interactionFrameCount: 0,
      selectionOnlyFrameCount: 0,
      selectionOnlyGeometryUploadBytes: 0,
      countryInteractionIndexCount: 0,
      countryInteractionRangeCount: 0,
      countryInteractionFullIndexCount: 0,
      countryBaseIndexCount: 0,
      countryBaseFullIndexCount: 0,
      countryBaseRangeCount: 0,
      countryBoundaryIndexCount: 0,
      countryBoundaryFullIndexCount: 0,
      countryVisibleCount: 0,
      countryCullingFallbackCount: 0,
      countryStateCompositeCount: 0,
      countryStateCompositeMs: 0,
      countryPatchUploadBytes: 0,
      lastCountryPatchUploadBytes: 0,
      countryPatchSceneHoldCount: 0,
      paletteChangedCountryCount: 0,
      paletteUploadRangeCount: 0,
      paletteFullRebuildCount: 0,
      paletteSkippedUnchangedCount: 0,
      overlayUploadBytes: 0,
      lastOverlayUploadBytes: 0,
      overlayDeferredItemCount: 0,
      uploadBudgetOverrunCount: 0,
    };
    let cachedDetailedStats = { at: 0, p95CpuSubmitMs: 0, p99CpuSubmitMs: 0 };
    let canvasDataReplacementResolver = null;
    let lastGeometryCommitTimings = null;
    const forcedRenderer = (() => {
      try {
        const value = new URLSearchParams(location.search).get('renderer');
        return ['webgl2', 'webgl1', 'canvas'].includes(value) ? value : '';
      } catch (_) { return ''; }
    })();

    function invalidateGpuFrame(reason = 'gpu-frame') {
      if (disposed) return;
      if (typeof scheduleGpuFrame === 'function') return scheduleGpuFrame(reason);
      return renderViewFrame?.(reason);
    }

    function invalidateGpuInteraction(reason = 'gpu-interaction') {
      if (disposed) return;
      if (typeof scheduleGpuInteractionFrame === 'function') return scheduleGpuInteractionFrame(reason);
      return invalidateGpuFrame(reason);
    }

    function invalidatePhysicalScene(reason = 'physical-scene') {
      lastBaseSceneResult = null;
      sceneColorCache.invalidate(reason);
      if (rendererMode !== 'pending') return invalidateGpuFrame(reason);
      return false;
    }

    function countryPatchPresentationCanDrawOverride(id) {
      return countryPatchPresentation?.phase === 'staging'
        && countryPatchPresentation.ids.has(String(id));
    }

    const terrainTargetsHaveSettled = () => terrainPreparation.settled();

    function shouldHoldCountryPatchScene(viewSignature) {
      const presentation = countryPatchPresentation;
      if (!presentation || presentation.phase !== 'staging') return false;
      if (presentation.preserveAllowed === false) return false;
      if (presentation.geometryRevision !== geometryRevisionTracker.committedRevision()) return false;
      if (!sceneColorCache.canCompositePreserved?.(viewSignature, projectGeneration)) return false;
      if (!state.physicalSettings.terrainVisible || !terrainManifest?.levels?.length || !terrainProgram) return false;
      const { terrainTargetTileCount, terrainTargetTilesLoaded } = terrainPreparation.stats();
      return terrainTargetTileCount > 0
        && terrainTargetTilesLoaded < terrainTargetTileCount
        && !terrainTargetsHaveSettled();
    }

    function completePreservedCountryPatchPresentation() {
      const presentation = countryPatchPresentation;
      if (!presentation || presentation.phase !== 'staging') return false;
      if (!geometryRevisionTracker.isCurrent(presentation.token, presentation.geometryRevision)) {
        countryPatchPresentation = null;
        return false;
      }
      presentation.phase = 'promoted';
      completeGeometryDisplay(presentation.ids, presentation.geometryRevision, { renderFrame: false });
      return true;
    }

    function resolveRenderPixelRatio() {
      effectivePixelRatio = resolveRenderPixelRatioValue(window.devicePixelRatio, isMobile(), renderQuality.dprCap);
      return effectivePixelRatio;
    }

    const vertexShaderSourceWebGl2 = `#version 300 es
      precision highp float;
      precision highp int;
      layout(location=0) in ivec2 aCoord;
      layout(location=1) in uint aCountry;
      uniform vec2 uViewport;
      uniform vec2 uTranslate;
      uniform float uScale;
      uniform vec3 uRowX;
      uniform vec3 uRowY;
      uniform vec3 uRowZ;
      uniform vec2 uFlatCenter;
      uniform float uWorldOffset;
      uniform int uMode;
      out float vDepth;
      flat out uint vCountry;
      void main() {
        float lon = float(aCoord.x) * 0.000001 * ${Math.PI / 180};
        float lat = float(aCoord.y) * 0.000001 * ${Math.PI / 180};
        vec2 screenPoint;
        if (uMode == 0) {
          vec3 point = vec3(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat));
          screenPoint = uTranslate + uScale * vec2(dot(uRowX, point), dot(uRowY, point));
          vDepth = dot(uRowZ, point);
        } else {
          screenPoint = uTranslate + uScale * vec2(lon + uWorldOffset - uFlatCenter.x, -(lat - uFlatCenter.y));
          vDepth = 1.0;
        }
        vec2 clip = vec2(screenPoint.x * 2.0 / uViewport.x - 1.0, 1.0 - screenPoint.y * 2.0 / uViewport.y);
        gl_Position = vec4(clip, 0.0, 1.0);
        vCountry = aCountry;
      }`;
    const fillFragmentSourceWebGl2 = `#version 300 es
      precision highp float;
      precision highp int;
      in float vDepth;
      flat in uint vCountry;
      uniform sampler2D uPalette;
      uniform int uMode;
      out vec4 outColor;
      void main() {
        if (uMode == 0 && vDepth < 0.0) discard;
        vec4 color = texelFetch(uPalette, ivec2(int(vCountry), 0), 0);
        if (color.a <= 0.0) discard;
        outColor = color;
      }`;
    const lineFragmentSourceWebGl2 = `#version 300 es
      precision highp float;
      precision highp int;
      in float vDepth;
      flat in uint vCountry;
      uniform sampler2D uPalette;
      uniform int uMode;
      uniform vec4 uBorderColor;
      out vec4 outColor;
      void main() {
        if (uMode == 0 && vDepth < 0.0) discard;
        if (texelFetch(uPalette, ivec2(int(vCountry), 0), 0).a <= 0.0) discard;
        outColor = uBorderColor;
      }`;
    const pickFragmentSourceWebGl2 = `#version 300 es
      precision highp float;
      precision highp int;
      in float vDepth;
      flat in uint vCountry;
      uniform sampler2D uPalette;
      uniform int uMode;
      out vec4 outColor;
      void main() {
        if (uMode == 0 && vDepth < 0.0) discard;
        if (texelFetch(uPalette, ivec2(int(vCountry), 0), 0).a <= 0.0) discard;
        uint id = vCountry + 1u;
        outColor = vec4(float(id & 255u), float((id >> 8u) & 255u), float((id >> 16u) & 255u), 255.0) / 255.0;
      }`;
    const countryStateVertexSourceWebGl2 = `#version 300 es
      precision highp float;
      layout(location=0) in vec2 aPosition;
      out vec2 vUv;
      void main(){vUv=aPosition*0.5+0.5;gl_Position=vec4(aPosition,0.0,1.0);}`;
    const countryStateFragmentSourceWebGl2 = `#version 300 es
      precision highp float;
      uniform sampler2D uCountryIds;
      uniform sampler2D uPalette;
      uniform float uPaletteWidth;
      in vec2 vUv;
      out vec4 outColor;
      void main(){
        vec3 encoded=floor(texture(uCountryIds,vUv).rgb*255.0+0.5);
        float countryIndex=encoded.r+encoded.g*256.0+encoded.b*65536.0-1.0;
        if(countryIndex<0.0)discard;
        vec4 color=texture(uPalette,vec2((countryIndex+0.5)/max(1.0,uPaletteWidth),0.5));
        if(color.a<=0.0)discard;
        outColor=color;
      }`;
    const landMaskFragmentSourceWebGl2 = `#version 300 es
      precision highp float;
      precision highp int;
      in float vDepth;
      uniform int uMode;
      out vec4 outColor;
      void main() {
        if (uMode == 0 && vDepth < 0.0) discard;
        outColor = vec4(1.0);
      }`;
    const vertexShaderSourceWebGl1 = `
      precision highp float;
      precision mediump int;
      attribute vec2 aCoord;
      attribute float aCountry;
      uniform vec2 uViewport;
      uniform vec2 uTranslate;
      uniform float uScale;
      uniform vec3 uRowX;
      uniform vec3 uRowY;
      uniform vec3 uRowZ;
      uniform vec2 uFlatCenter;
      uniform float uWorldOffset;
      uniform int uMode;
      varying float vDepth;
      varying float vCountry;
      void main() {
        float lon = aCoord.x * 0.000001 * ${Math.PI / 180};
        float lat = aCoord.y * 0.000001 * ${Math.PI / 180};
        vec2 screenPoint;
        if (uMode == 0) {
          vec3 point = vec3(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat));
          screenPoint = uTranslate + uScale * vec2(dot(uRowX, point), dot(uRowY, point));
          vDepth = dot(uRowZ, point);
        } else {
          screenPoint = uTranslate + uScale * vec2(lon + uWorldOffset - uFlatCenter.x, -(lat - uFlatCenter.y));
          vDepth = 1.0;
        }
        vec2 clip = vec2(screenPoint.x * 2.0 / uViewport.x - 1.0, 1.0 - screenPoint.y * 2.0 / uViewport.y);
        gl_Position = vec4(clip, 0.0, 1.0);
        vCountry = aCountry;
      }`;
    const hydroRibbonVertexSourceWebGl2 = `#version 300 es
      precision highp float;
      precision highp int;
      layout(location=0) in vec2 aCorner;
      layout(location=1) in ivec2 aStart;
      layout(location=2) in ivec2 aEnd;
      layout(location=3) in uint aCountry;
      layout(location=4) in float aStartWidth;
      layout(location=5) in float aEndWidth;
      uniform vec2 uViewport;
      uniform vec2 uTranslate;
      uniform float uScale;
      uniform vec3 uRowX;
      uniform vec3 uRowY;
      uniform vec3 uRowZ;
      uniform vec2 uFlatCenter;
      uniform float uWorldOffset;
      uniform float uWidthBoost;
      uniform float uWidthScale;
      uniform int uMode;
      out float vDepth;
      flat out uint vCountry;
      void projectCoord(ivec2 coord, out vec2 screenPoint, out float depth) {
        float lon = float(coord.x) * 0.000001 * ${Math.PI / 180};
        float lat = float(coord.y) * 0.000001 * ${Math.PI / 180};
        if (uMode == 0) {
          vec3 point = vec3(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat));
          screenPoint = uTranslate + uScale * vec2(dot(uRowX, point), dot(uRowY, point));
          depth = dot(uRowZ, point);
        } else {
          screenPoint = uTranslate + uScale * vec2(lon + uWorldOffset - uFlatCenter.x, -(lat - uFlatCenter.y));
          depth = 1.0;
        }
      }
      void main() {
        vec2 startPoint;
        vec2 endPoint;
        float startDepth;
        float endDepth;
        projectCoord(aStart, startPoint, startDepth);
        projectCoord(aEnd, endPoint, endDepth);
        vec2 direction = endPoint - startPoint;
        float segmentLength = length(direction);
        direction = segmentLength > 0.0001 ? direction / segmentLength : vec2(1.0, 0.0);
        vec2 normal = vec2(-direction.y, direction.x);
        float width = mix(aStartWidth, aEndWidth, aCorner.x) * uWidthScale;
        vec2 screenPoint = mix(startPoint, endPoint, aCorner.x) + normal * aCorner.y * (width + uWidthBoost) * 0.5;
        vDepth = mix(startDepth, endDepth, aCorner.x);
        vec2 clip = vec2(screenPoint.x * 2.0 / uViewport.x - 1.0, 1.0 - screenPoint.y * 2.0 / uViewport.y);
        gl_Position = vec4(clip, 0.0, 1.0);
        vCountry = aCountry;
      }`;
    const hydroRibbonVertexSourceWebGl1 = `
      precision highp float;
      precision mediump int;
      attribute vec2 aCorner;
      attribute vec2 aStart;
      attribute vec2 aEnd;
      attribute float aCountry;
      attribute float aStartWidth;
      attribute float aEndWidth;
      uniform vec2 uViewport;
      uniform vec2 uTranslate;
      uniform float uScale;
      uniform vec3 uRowX;
      uniform vec3 uRowY;
      uniform vec3 uRowZ;
      uniform vec2 uFlatCenter;
      uniform float uWorldOffset;
      uniform float uWidthBoost;
      uniform float uWidthScale;
      uniform int uMode;
      varying float vDepth;
      varying float vCountry;
      void projectCoord(vec2 coord, out vec2 screenPoint, out float depth) {
        float lon = coord.x * 0.000001 * ${Math.PI / 180};
        float lat = coord.y * 0.000001 * ${Math.PI / 180};
        if (uMode == 0) {
          vec3 point = vec3(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat));
          screenPoint = uTranslate + uScale * vec2(dot(uRowX, point), dot(uRowY, point));
          depth = dot(uRowZ, point);
        } else {
          screenPoint = uTranslate + uScale * vec2(lon + uWorldOffset - uFlatCenter.x, -(lat - uFlatCenter.y));
          depth = 1.0;
        }
      }
      void main() {
        vec2 startPoint;
        vec2 endPoint;
        float startDepth;
        float endDepth;
        projectCoord(aStart, startPoint, startDepth);
        projectCoord(aEnd, endPoint, endDepth);
        vec2 direction = endPoint - startPoint;
        float segmentLength = length(direction);
        direction = segmentLength > 0.0001 ? direction / segmentLength : vec2(1.0, 0.0);
        vec2 normal = vec2(-direction.y, direction.x);
        float width = mix(aStartWidth, aEndWidth, aCorner.x) * uWidthScale;
        vec2 screenPoint = mix(startPoint, endPoint, aCorner.x) + normal * aCorner.y * (width + uWidthBoost) * 0.5;
        vDepth = mix(startDepth, endDepth, aCorner.x);
        vec2 clip = vec2(screenPoint.x * 2.0 / uViewport.x - 1.0, 1.0 - screenPoint.y * 2.0 / uViewport.y);
        gl_Position = vec4(clip, 0.0, 1.0);
        vCountry = aCountry;
      }`;
    const fillFragmentSourceWebGl1 = `
      precision highp float;
      precision mediump int;
      varying float vDepth;
      varying float vCountry;
      uniform sampler2D uPalette;
      uniform float uPaletteWidth;
      uniform int uMode;
      void main() {
        if (uMode == 0 && vDepth < 0.0) discard;
        float index = floor(vCountry + 0.5);
        vec4 color = texture2D(uPalette, vec2((index + 0.5) / uPaletteWidth, 0.5));
        if (color.a <= 0.0) discard;
        gl_FragColor = color;
      }`;
    const lineFragmentSourceWebGl1 = `
      precision highp float;
      precision mediump int;
      varying float vDepth;
      varying float vCountry;
      uniform sampler2D uPalette;
      uniform float uPaletteWidth;
      uniform int uMode;
      uniform vec4 uBorderColor;
      void main() {
        if (uMode == 0 && vDepth < 0.0) discard;
        float index = floor(vCountry + 0.5);
        if (texture2D(uPalette, vec2((index + 0.5) / uPaletteWidth, 0.5)).a <= 0.0) discard;
        gl_FragColor = uBorderColor;
      }`;
    const pickFragmentSourceWebGl1 = `
      precision highp float;
      precision mediump int;
      varying float vDepth;
      varying float vCountry;
      uniform sampler2D uPalette;
      uniform float uPaletteWidth;
      uniform int uMode;
      void main() {
        if (uMode == 0 && vDepth < 0.0) discard;
        float index = floor(vCountry + 0.5);
        if (texture2D(uPalette, vec2((index + 0.5) / uPaletteWidth, 0.5)).a <= 0.0) discard;
        float id = floor(vCountry + 1.5);
        float r = mod(id, 256.0);
        float g = mod(floor(id / 256.0), 256.0);
        float b = mod(floor(id / 65536.0), 256.0);
        gl_FragColor = vec4(r, g, b, 255.0) / 255.0;
      }`;
    const countryStateVertexSourceWebGl1 = `
      precision highp float;
      attribute vec2 aPosition;
      varying vec2 vUv;
      void main(){vUv=aPosition*0.5+0.5;gl_Position=vec4(aPosition,0.0,1.0);}`;
    const countryStateFragmentSourceWebGl1 = `
      precision highp float;
      uniform sampler2D uCountryIds;
      uniform sampler2D uPalette;
      uniform float uPaletteWidth;
      varying vec2 vUv;
      void main(){
        vec3 encoded=floor(texture2D(uCountryIds,vUv).rgb*255.0+0.5);
        float countryIndex=encoded.r+encoded.g*256.0+encoded.b*65536.0-1.0;
        if(countryIndex<0.0)discard;
        vec4 color=texture2D(uPalette,vec2((countryIndex+0.5)/max(1.0,uPaletteWidth),0.5));
        if(color.a<=0.0)discard;
        gl_FragColor=color;
      }`;
    const landMaskFragmentSourceWebGl1 = `
      precision highp float;
      precision mediump int;
      varying float vDepth;
      uniform int uMode;
      void main() {
        if (uMode == 0 && vDepth < 0.0) discard;
        gl_FragColor = vec4(1.0);
      }`;
    const terrainVertexSourceWebGl2 = `#version 300 es
      precision highp float;
      layout(location=0) in vec2 aGrid;
      uniform vec4 uGeoBounds;
      uniform vec4 uUvBounds;
      uniform vec2 uViewport;
      uniform vec2 uTranslate;
      uniform float uScale;
      uniform vec3 uRowX;
      uniform vec3 uRowY;
      uniform vec3 uRowZ;
      uniform vec2 uFlatCenter;
      uniform float uWorldOffset;
      uniform int uMode;
      out vec2 vUv;
      out vec2 vLonLat;
      out float vDepth;
      void main() {
        vLonLat = mix(uGeoBounds.xy, uGeoBounds.zw, aGrid);
        float lon = mix(uGeoBounds.x, uGeoBounds.z, aGrid.x) * ${Math.PI / 180};
        float lat = mix(uGeoBounds.y, uGeoBounds.w, aGrid.y) * ${Math.PI / 180};
        vec2 screenPoint;
        if (uMode == 0) {
          vec3 point = vec3(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat));
          screenPoint = uTranslate + uScale * vec2(dot(uRowX, point), dot(uRowY, point));
          vDepth = dot(uRowZ, point);
        } else {
          screenPoint = uTranslate + uScale * vec2(lon + uWorldOffset - uFlatCenter.x, -(lat - uFlatCenter.y));
          vDepth = 1.0;
        }
        vec2 clip = vec2(screenPoint.x * 2.0 / uViewport.x - 1.0, 1.0 - screenPoint.y * 2.0 / uViewport.y);
        gl_Position = vec4(clip, 0.0, 1.0);
        vUv = mix(uUvBounds.xy, uUvBounds.zw, aGrid);
      }`;
    const terrainFragmentSourceWebGl2 = `#version 300 es
      precision highp float;
      precision highp int;
      in vec2 vUv;
      in float vDepth;
      uniform sampler2D uTerrain;
      uniform int uMode;
      uniform float uPhysicalStyle;
      uniform float uDarkTheme;
      out vec4 outColor;
      void main() {
        if (uMode == 0 && vDepth < 0.0) discard;
        vec4 terrainSample = texture(uTerrain, vUv);
        vec3 neutral = vec3(terrainSample.a);
        vec3 color = mix(neutral, terrainSample.rgb, uPhysicalStyle);
        color = mix(color, color * vec3(0.60, 0.68, 0.76), uDarkTheme * 0.48);
        outColor = vec4(color, 1.0);
      }`;
    const terrainVertexSourceWebGl1 = `
      precision highp float;
      precision mediump int;
      attribute vec2 aGrid;
      uniform vec4 uGeoBounds;
      uniform vec4 uUvBounds;
      uniform vec2 uViewport;
      uniform vec2 uTranslate;
      uniform float uScale;
      uniform vec3 uRowX;
      uniform vec3 uRowY;
      uniform vec3 uRowZ;
      uniform vec2 uFlatCenter;
      uniform float uWorldOffset;
      uniform int uMode;
      varying vec2 vUv;
      varying vec2 vLonLat;
      varying float vDepth;
      void main() {
        vLonLat = mix(uGeoBounds.xy, uGeoBounds.zw, aGrid);
        float lon = mix(uGeoBounds.x, uGeoBounds.z, aGrid.x) * ${Math.PI / 180};
        float lat = mix(uGeoBounds.y, uGeoBounds.w, aGrid.y) * ${Math.PI / 180};
        vec2 screenPoint;
        if (uMode == 0) {
          vec3 point = vec3(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat));
          screenPoint = uTranslate + uScale * vec2(dot(uRowX, point), dot(uRowY, point));
          vDepth = dot(uRowZ, point);
        } else {
          screenPoint = uTranslate + uScale * vec2(lon + uWorldOffset - uFlatCenter.x, -(lat - uFlatCenter.y));
          vDepth = 1.0;
        }
        vec2 clip = vec2(screenPoint.x * 2.0 / uViewport.x - 1.0, 1.0 - screenPoint.y * 2.0 / uViewport.y);
        gl_Position = vec4(clip, 0.0, 1.0);
        vUv = mix(uUvBounds.xy, uUvBounds.zw, aGrid);
      }`;
    const terrainFragmentSourceWebGl1 = `
      precision highp float;
      precision mediump int;
      varying vec2 vUv;
      varying float vDepth;
      uniform sampler2D uTerrain;
      uniform int uMode;
      uniform float uPhysicalStyle;
      uniform float uDarkTheme;
      void main() {
        if (uMode == 0 && vDepth < 0.0) discard;
        vec4 terrainSample = texture2D(uTerrain, vUv);
        vec3 neutral = vec3(terrainSample.a);
        vec3 color = mix(neutral, terrainSample.rgb, uPhysicalStyle);
        color = mix(color, color * vec3(0.60, 0.68, 0.76), uDarkTheme * 0.48);
        gl_FragColor = vec4(color, 1.0);
      }`;
    const hydroFragmentSourceWebGl2 = `#version 300 es
      precision highp float;
      precision highp int;
      in float vDepth;
      flat in uint vCountry;
      uniform sampler2D uHydroVisibility;
      uniform ivec2 uHydroVisibilitySize;
      uniform vec4 uHydroColor;
      uniform int uMode;
      out vec4 outColor;
      void main() {
        if (uMode == 0 && vDepth < 0.0) discard;
        int featureId = int(vCountry);
        ivec2 cell = ivec2(featureId % uHydroVisibilitySize.x, featureId / uHydroVisibilitySize.x);
        if (texelFetch(uHydroVisibility, cell, 0).a <= 0.0) discard;
        outColor = uHydroColor;
      }`;
    const hydroPickFragmentSourceWebGl2 = `#version 300 es
      precision highp float;
      precision highp int;
      in float vDepth;
      flat in uint vCountry;
      uniform sampler2D uHydroVisibility;
      uniform ivec2 uHydroVisibilitySize;
      uniform int uMode;
      out vec4 outColor;
      void main() {
        if (uMode == 0 && vDepth < 0.0) discard;
        int featureId = int(vCountry);
        ivec2 cell = ivec2(featureId % uHydroVisibilitySize.x, featureId / uHydroVisibilitySize.x);
        if (texelFetch(uHydroVisibility, cell, 0).a <= 0.0) discard;
        uint id = vCountry + 1u;
        outColor = vec4(float(id & 255u), float((id >> 8u) & 255u), float((id >> 16u) & 255u), 255.0) / 255.0;
      }`;
    const hydroFragmentSourceWebGl1 = `
      precision highp float;
      precision mediump int;
      varying float vDepth;
      varying float vCountry;
      uniform sampler2D uHydroVisibility;
      uniform vec2 uHydroVisibilitySize;
      uniform vec4 uHydroColor;
      uniform int uMode;
      void main() {
        if (uMode == 0 && vDepth < 0.0) discard;
        float featureId = floor(vCountry + 0.5);
        float x = mod(featureId, uHydroVisibilitySize.x);
        float y = floor(featureId / uHydroVisibilitySize.x);
        if (texture2D(uHydroVisibility, vec2((x + 0.5) / uHydroVisibilitySize.x, (y + 0.5) / uHydroVisibilitySize.y)).a <= 0.0) discard;
        gl_FragColor = uHydroColor;
      }`;
    const hydroPickFragmentSourceWebGl1 = `
      precision highp float;
      precision mediump int;
      varying float vDepth;
      varying float vCountry;
      uniform sampler2D uHydroVisibility;
      uniform vec2 uHydroVisibilitySize;
      uniform int uMode;
      void main() {
        if (uMode == 0 && vDepth < 0.0) discard;
        float featureId = floor(vCountry + 0.5);
        float x = mod(featureId, uHydroVisibilitySize.x);
        float y = floor(featureId / uHydroVisibilitySize.x);
        if (texture2D(uHydroVisibility, vec2((x + 0.5) / uHydroVisibilitySize.x, (y + 0.5) / uHydroVisibilitySize.y)).a <= 0.0) discard;
        float id = featureId + 1.0;
        gl_FragColor = vec4(mod(id, 256.0), mod(floor(id / 256.0), 256.0), mod(floor(id / 65536.0), 256.0), 255.0) / 255.0;
      }`;

    function createProgram(vertexSource, fragmentSource) {
      return lifecycle.retain(gl, 'Program', linkGpuProgram(gl, vertexSource, fragmentSource, { label: 'map' }));
    }

    function ensureTerrainDemProgram() {
      if (terrainDemProgram || !gl) return !!terrainDemProgram;
      const precision = gl.getShaderPrecisionFormat?.(gl.FRAGMENT_SHADER, gl.HIGH_FLOAT);
      if (glVersion === 1 && !(precision?.precision >= 16 && precision.rangeMax >= 16)) return false;
      try {
        terrainDemProgram = createProgram(
          glVersion === 2 ? terrainVertexSourceWebGl2 : terrainVertexSourceWebGl1,
          terrainDemFragmentSource(glVersion),
        );
        primeProgramLocations(terrainDemProgram, GPU_VIEW_UNIFORM_NAMES);
        primeProgramLocations(terrainDemProgram,
          ['uTerrain', 'uTint', 'uGeoBounds', 'uUvBounds', 'uPhysicalStyle', 'uDarkTheme',
            'uTextureSize', 'uLevelSize', 'uLandPass', 'uShadeBlend'], ['aGrid']);
        return true;
      } catch (error) {
        terrainDemProgram = null;
        console.warn('DEM shader unavailable; raster terrain will be used', error);
        return false;
      }
    }

    function cachedUniformLocation(program, name) {
      let locations = uniformLocationCache.get(program);
      if (!locations) {
        locations = new Map();
        uniformLocationCache.set(program, locations);
      }
      if (locations.has(name)) {
        performanceMetrics.uniformCacheHits += 1;
        return locations.get(name);
      }
      const location = gl.getUniformLocation(program, name);
      locations.set(name, location);
      performanceMetrics.uniformCacheMisses += 1;
      return location;
    }

    function cachedAttributeLocation(program, name) {
      let locations = attributeLocationCache.get(program);
      if (!locations) {
        locations = new Map();
        attributeLocationCache.set(program, locations);
      }
      if (locations.has(name)) {
        performanceMetrics.attributeCacheHits += 1;
        return locations.get(name);
      }
      const location = gl.getAttribLocation(program, name);
      locations.set(name, location);
      performanceMetrics.attributeCacheMisses += 1;
      return location;
    }

    function primeProgramLocations(program, uniforms = [], attributes = []) {
      for (const name of uniforms) cachedUniformLocation(program, name);
      for (const name of attributes) cachedAttributeLocation(program, name);
    }

    function attach(nextCanvas) {
      canvas = nextCanvas;
      resizePending = true;
      canvas.className = 'gpu-map-canvas';
      canvas.setAttribute('aria-hidden', 'true');
      canvas.style.position = 'absolute';
      canvas.style.inset = '0';
      canvas.style.width = '100%';
      canvas.style.height = '100%';
      layoutMismatchCount = 0;
    }

    function replaceCanvas() {
      lifecycle.unlisten(canvas);
      releaseGpuContext();
      if (renderDevice || gl) handleSharedGpuContextLost();
      const replacement = rendererUi.createCanvas();
      canvas?.replaceWith(replacement);
      attach(replacement);
      gl = null;
      glVersion = 0;
      renderDevice = null;
      webGlContextKind = '';
      uintIndexExtension = null;
      instancedExtension = null;
      ctx2d = null;
      canvasWorkerBitmapContext = null;
      canvasWorker2dContext = null;
      return replacement;
    }

    function isWebGlRenderer() {
      return rendererMode === 'webgl2' || rendererMode === 'webgl1';
    }

    function connectHydroCanvasWorkers() {
      if (!hydroPreparation.hasWorker() || !canvasWorker || rendererMode !== 'canvas-worker' || typeof MessageChannel !== 'function') return;
      const channel = new MessageChannel();
      canvasWorker.postMessage({ type: 'hydro-port', port: channel.port1 }, [channel.port1]);
      hydroPreparation.connectPort(channel.port2);
    }

    function rendererName() {
      return glVersion === 2 ? 'WebGL2' : glVersion === 1 ? 'WebGL1' : 'Canvas';
    }

    function meshQualityLabel() {
      return canonicalMeshReady ? '무손실' : previewAllowed ? '빠른 미리보기' : '고화질 지도 준비 중';
    }

    function updateRendererStatus(label, reason = '') {
      rendererUi.setEngineStatus(reason ? `${label} · ${reason}` : label);
    }

    function createWebGlResources() {
      uniformLocationCache = new WeakMap();
      attributeLocationCache = new WeakMap();
      terrainDemProgram = null;
      const vertexSource = glVersion === 2 ? vertexShaderSourceWebGl2 : vertexShaderSourceWebGl1;
      fillProgram = createProgram(vertexSource, glVersion === 2 ? fillFragmentSourceWebGl2 : fillFragmentSourceWebGl1);
      landMaskProgram = createProgram(vertexSource, glVersion === 2 ? landMaskFragmentSourceWebGl2 : landMaskFragmentSourceWebGl1);
      lineProgram = createProgram(vertexSource, glVersion === 2 ? lineFragmentSourceWebGl2 : lineFragmentSourceWebGl1);
      pickProgram = createProgram(vertexSource, glVersion === 2 ? pickFragmentSourceWebGl2 : pickFragmentSourceWebGl1);
      countryStateFillProgram = createProgram(
        glVersion === 2 ? countryStateVertexSourceWebGl2 : countryStateVertexSourceWebGl1,
        glVersion === 2 ? countryStateFragmentSourceWebGl2 : countryStateFragmentSourceWebGl1,
      );
      terrainProgram = createProgram(
        glVersion === 2 ? terrainVertexSourceWebGl2 : terrainVertexSourceWebGl1,
        glVersion === 2 ? terrainFragmentSourceWebGl2 : terrainFragmentSourceWebGl1,
      );
      hydroFillProgram = createProgram(vertexSource, glVersion === 2 ? hydroFragmentSourceWebGl2 : hydroFragmentSourceWebGl1);
      hydroLineProgram = createProgram(
        glVersion === 2 ? hydroRibbonVertexSourceWebGl2 : hydroRibbonVertexSourceWebGl1,
        glVersion === 2 ? hydroFragmentSourceWebGl2 : hydroFragmentSourceWebGl1,
      );
      hydroPickProgram = createProgram(vertexSource, glVersion === 2 ? hydroPickFragmentSourceWebGl2 : hydroPickFragmentSourceWebGl1);
      hydroLinePickProgram = createProgram(
        glVersion === 2 ? hydroRibbonVertexSourceWebGl2 : hydroRibbonVertexSourceWebGl1,
        glVersion === 2 ? hydroPickFragmentSourceWebGl2 : hydroPickFragmentSourceWebGl1,
      );
      const viewUniforms = GPU_VIEW_UNIFORM_NAMES;
      for (const program of [fillProgram, landMaskProgram, lineProgram, pickProgram, terrainProgram, hydroFillProgram, hydroLineProgram, hydroPickProgram, hydroLinePickProgram]) {
        primeProgramLocations(program, viewUniforms);
      }
      for (const program of [fillProgram, lineProgram, pickProgram]) primeProgramLocations(program, ['uPalette', 'uPaletteWidth'], ['aCoord', 'aCountry']);
      primeProgramLocations(countryStateFillProgram, ['uCountryIds', 'uPalette', 'uPaletteWidth'], ['aPosition']);
      primeProgramLocations(lineProgram, ['uBorderColor']);
      for (const program of [hydroFillProgram, hydroLineProgram, hydroPickProgram, hydroLinePickProgram]) {
        primeProgramLocations(program, ['uHydroVisibility', 'uHydroVisibilitySize', 'uHydroColor', 'uWidthBoost', 'uWidthScale'], ['aCoord', 'aCountry', 'aCorner', 'aStart', 'aEnd', 'aStartWidth', 'aEndWidth']);
      }
      primeProgramLocations(terrainProgram, ['uTerrain', 'uGeoBounds', 'uUvBounds', 'uPhysicalStyle', 'uDarkTheme'], ['aGrid']);
      if (terrainManifest?.representation === TERRAIN_DEM_FORMAT && !ensureTerrainDemProgram()) {
        queueMicrotask(() => { void fallbackTerrainToRaster('DEM 셰이더 정밀도가 부족합니다.'); });
      }
      paletteTexture = lifecycle.create(gl, 'Texture');
      overridePaletteTexture = lifecycle.create(gl, 'Texture');
      emphasisPaletteTexture = lifecycle.create(gl, 'Texture');
      overrideEmphasisPaletteTexture = lifecycle.create(gl, 'Texture');
      paletteCapacity = 0;
      palettePixels = null;
      paletteDirty.base = true;
      paletteDirty.emphasis = true;
      emphasisPaletteFullDirty = true;
      pendingEmphasisCountryIds.clear();
      hydroVisibilityTexture = lifecycle.create(gl, 'Texture');
      countryStateQuadBuffer = lifecycle.create(gl, 'Buffer');
      gl.bindBuffer(gl.ARRAY_BUFFER, countryStateQuadBuffer);
      gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]), gl.STATIC_DRAW);
      hydroCornerBuffer = lifecycle.create(gl, 'Buffer');
      gl.bindBuffer(gl.ARRAY_BUFFER, hydroCornerBuffer);
      gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([0, -1, 0, 1, 1, -1, 1, 1]), gl.STATIC_DRAW);
      positionBuffer = null;
      countryBuffer = null;
      fillIndexBuffer = null;
      lineIndexBuffer = null;
      overridePositionBuffer = lifecycle.create(gl, 'Buffer');
      overrideCountryBuffer = lifecycle.create(gl, 'Buffer');
      overrideFillIndexBuffer = lifecycle.create(gl, 'Buffer');
      overrideLineIndexBuffer = lifecycle.create(gl, 'Buffer');
      resetGpuNormalBlend(gl);
      gl.disable(gl.DEPTH_TEST);
      pickFramebuffer = null;
      pickTexture = null;
      pickSceneKey = '';
      terrainPreparation.reset();
      hydroPreparation.setContext({ gl, version: glVersion, projectGeneration, contextGeneration: renderDeviceContextRevision, scheduler: uploadScheduler });
      hydroVisibilityDirty = true;
    }

    function initializeSharedGpuPasses() {
      if (!renderDevice) return false;
      const sceneCacheReady = sceneColorCache.initialize(renderDevice);
      interactionFillCache.initialize(renderDevice);
      interactionStrokeCache.initialize(renderDevice);
      const device = renderDevice, revision = renderDeviceContextRevision;
      const enqueue = (name, run) => uploadScheduler.enqueueUpload({
        key: 'shader:' + revision + ':' + name, contextGeneration: revision, priority: 20,
        step: () => {
          if (revision !== renderDeviceContextRevision || device !== renderDevice) throw Object.assign(new Error('Stale shader initialization'), { name: 'AbortError' });
          const start = performance.now(), value = run();
          performanceMetrics.shaderInitializationMs = (performanceMetrics.shaderInitializationMs || 0) + performance.now() - start;
          return { done: true, value };
        },
      });
      const initialize = async () => {
        if (!uploadScheduler) return;
        await enqueue('polygon', () => polygonOverlayPass.initialize(device));
        await strokeRenderer.initializeProgressively(device, enqueue);
        if (selectionPass) await enqueue('selection', () => selectionPass.initialize(device, { strokeRenderer, polygonPass: polygonOverlayPass }));
        prewarmCountryStrokeResources();
        sceneColorCache.invalidate('shared-pass-ready');
        invalidateGpuFrame('shared-pass-ready');
      };
      // At least one paint before optional pass compilation.
      lifecycle.frame(() => lifecycle.frame(() => { void initialize().catch(error => {
        if (error.name !== 'AbortError') console.warn('Optional GPU pass initialization failed', error);
      }); }));
      lastSelectionRenderResult = null;
      return sceneCacheReady;
    }

    function handleSharedGpuContextLost() {
      renderDeviceContextRevision += 1;
      terrainPreparation.reset();
      hydroPreparation.resetGpu();
      lifecycle.releaseContext(gl);
      pendingCanonicalCommit?.reject(Object.assign(new Error('Context lost during canonical commit'), { name: 'AbortError' }));
      pendingCanonicalCommit = null;
      uploadScheduler?.cancelAll();
      sceneColorCache.handleContextLost();
      interactionFillCache.handleContextLost(); interactionStrokeCache.handleContextLost();
      polygonOverlayPass.handleContextLost();
      strokeRenderer.handleContextLost();
      if (!selectionPass?.stats?.().contextLost) selectionPass?.handleContextLost?.();
      lastSelectionRenderResult = null;
      lastBaseSceneResult = null;
      lastSceneFrameContext = null;
      sceneCacheFallbackFrame = false;
    }

    function handleWebGlContextLost(event) {
      if (disposed || event.currentTarget !== canvas) return;
      event.preventDefault();
      handleSharedGpuContextLost();
      webglContextLost = true;
      renderDevice = null;
      rendererMode = 'webgl-recovering';
      lifecycle.cancelTimeout(webglRecoveryTimer);
      updateRendererStatus(`${rendererName()} · GPU를 복구하는 중입니다.`);
      setActionStatus('지도 GPU를 복구하는 중입니다.', 'working', 0);
      rendererUi.onContextStateChange?.('lost');
      webglRecoveryTimer = lifecycle.timeout(() => {
        if (webglContextLost && rendererMode === 'webgl-recovering') {
          activateCanvasFallback('WebGL 컨텍스트 복구 시간 초과');
        }
      }, 5000);
    }

    async function handleWebGlContextRestored(event) {
      if (disposed || event.currentTarget !== canvas || !webglContextLost) return;
      const restoringCanvas = canvas, restoringGeneration = projectGeneration;
      lifecycle.cancelTimeout(webglRecoveryTimer);
      try {
        gl = canvas.getContext(webGlContextKind);
        if (!gl) throw new Error(`복구된 ${rendererName()} 컨텍스트를 가져올 수 없습니다.`);
        if (glVersion === 1) {
          uintIndexExtension = gl.getExtension('OES_element_index_uint');
          if (!uintIndexExtension) throw new Error('WebGL1 32비트 인덱스를 지원하지 않습니다.');
          instancedExtension = gl.getExtension('ANGLE_instanced_arrays');
          if (!instancedExtension) throw new Error('WebGL1 인스턴스 강·호수 렌더링을 지원하지 않습니다.');
        }
        createWebGlResources();
        renderDeviceContextRevision += 1;
        renderDevice = createRenderDevice({
          gl,
          canvas,
          version: glVersion,
          contextRevision: renderDeviceContextRevision,
        });
        initializeSharedGpuPasses();
        for (const entry of meshVariants.values()) entry.resources = await stageMeshResources(entry.mesh);
        if (disposed || canvas !== restoringCanvas || projectGeneration !== restoringGeneration) return;
        activateMeshVariant(activeMeshQuality, { renderFrame: false });
        webglContextLost = false;
        rendererMode = glVersion === 2 ? 'webgl2' : 'webgl1';
        if (overrideMesh) setOverrideMesh(overrideMesh, { stagedResources: await stageMeshResources(overrideMesh) });
        else renderLatestVisualFrame();
        updateRendererStatus(`${rendererName()} · GPU 실시간`);
        setActionStatus('지도 GPU를 복구했습니다.', 'success', 2200);
        rendererUi.onContextStateChange?.('restored');
      } catch (error) {
        if (disposed || canvas !== restoringCanvas || projectGeneration !== restoringGeneration || error.name === 'AbortError') return;
        webglContextLost = false;
        console.error('[PL-GPU-002]', error);
        activateCanvasFallback('WebGL 컨텍스트를 복구하지 못했습니다.');
      }
    }

    function initWebGl(version) {
      const contextKind = version === 2 ? 'webgl2' : 'webgl';
      webGlContextKind = contextKind;
      gl = canvas.getContext(contextKind, {
        alpha: true,
        antialias: true,
        depth: false,
        stencil: true,
        preserveDrawingBuffer: false,
        premultipliedAlpha: true,
        powerPreference: 'high-performance',
      });
      if (!gl && version === 1) {
        webGlContextKind = 'experimental-webgl';
        gl = canvas.getContext(webGlContextKind, { alpha: true, antialias: true, depth: false, stencil: true, preserveDrawingBuffer: false, premultipliedAlpha: true, powerPreference: 'high-performance' });
      }
      if (!gl) throw new Error(`${version === 2 ? 'WebGL2' : 'WebGL1'}를 지원하지 않습니다.`);
      if (!gl.getContextAttributes()?.stencil) throw new Error(`${version === 2 ? 'WebGL2' : 'WebGL1'} 스텐실 마스크를 지원하지 않습니다.`);
      glVersion = version;
      uintIndexExtension = version === 1 ? gl.getExtension('OES_element_index_uint') : true;
      if (version === 1 && !uintIndexExtension) throw new Error('WebGL1 OES_element_index_uint를 지원하지 않습니다.');
      instancedExtension = version === 1 ? gl.getExtension('ANGLE_instanced_arrays') : true;
      if (version === 1 && !instancedExtension) throw new Error('WebGL1 ANGLE_instanced_arrays를 지원하지 않습니다.');
      createWebGlResources();
      renderDeviceContextRevision += 1;
      renderDevice = createRenderDevice({
        gl,
        canvas,
        version,
        contextRevision: renderDeviceContextRevision,
      });
      initializeSharedGpuPasses();
      lifecycle.listen(canvas, 'webglcontextlost', handleWebGlContextLost);
      lifecycle.listen(canvas, 'webglcontextrestored', handleWebGlContextRestored);
      webglContextLost = false;
      rendererMode = version === 2 ? 'webgl2' : 'webgl1';
    }

    function disposeMeshResources(resources) {
      if (!gl || !resources) return;
      if (glVersion === 2) {
        if (resources.fillVao) lifecycle.release(resources.fillVao);
        if (resources.lineVao) lifecycle.release(resources.lineVao);
      }
      for (const buffer of [resources.positionBuffer, resources.countryBuffer, resources.fillIndexBuffer, resources.lineIndexBuffer]) {
        if (buffer) lifecycle.release(buffer);
      }
    }

    function disposeMeshEntry(entry) {
      if (!entry) return;
      if (builtinMeshBaseline?.mesh === entry.mesh) builtinMeshBaseline.resources = null;
      disposeMeshResources(entry.resources);
    }

    function rememberBuiltinMesh(meshValue, countryIds, identity = null) {
      const ids = [...(countryIds || [])].map(String).filter(Boolean);
      if (!meshValue || ids.length !== 258) throw new Error('내장 기본 메시의 국가 슬롯이 올바르지 않습니다.');
      builtinMeshBaseline = {
        mesh: meshValue,
        countryIds: ids,
        identity: identity && typeof identity === 'object'
          ? Object.freeze({
            hash: String(identity.hash || ''),
            header: [...(identity.header || [])].map(Number),
            dataRevision: String(identity.dataRevision || DATA_REVISION),
            countryIds: [...(identity.countryIds || ids)].map(String),
          })
          : null,
        resources: null,
      };
      return builtinMeshBaseline;
    }

    async function ensureBuiltinMeshBaseline(countryIds = []) {
      if (disposed) return false;
      if (builtinMeshBaseline?.mesh) return true;
      const resource = await builtinMeshResourceLoader.load(countryIds);
      if (disposed) return false;
      const decoded = await decodeBuiltInMesh(resource.meshBuffer, countryIds, resource.preparedStroke);
      if (disposed) return false;
      decoded.mesh.spatialBlocks = resource.spatialBlocks;
      rememberBuiltinMesh(decoded.mesh, decoded.ids, resource.identity);
      return true;
    }

    function waitForCanonicalVisualFrame(onStaged = null) {
      return new Promise((resolve, reject) => {
        pendingCanonicalCommit?.reject(Object.assign(new Error('Superseded canonical frame request'), { name: 'AbortError' }));
        pendingCanonicalCommit = { resolve, reject, generation: projectGeneration };
        onStaged?.();
        invalidateGpuFrame('built-in-project-transition-ready');
      });
    }

    async function activateBuiltinMeshBaseline({ projectGeneration: requestedGeneration = projectGeneration, onStaged = null } = {}) {
      if (Number(requestedGeneration) !== projectGeneration || !builtinMeshBaseline?.mesh) return false;
      if (!isWebGlRenderer()) {
        onStaged?.();
        const rebuilt = await rebuildFromCountries(state.countriesData?.features || [], {
          reason: 'new-project-canvas-fallback', projectGeneration: requestedGeneration,
        });
        return rebuilt !== false;
      }
      const baseline = builtinMeshBaseline;
      const activeEntry = meshVariants.get('canonical');
      if (activeEntry?.mesh !== baseline.mesh || !activeEntry.resources) {
        const stagedResources = await stageMeshResources(baseline.mesh, { projectGeneration: requestedGeneration });
        if (Number(requestedGeneration) !== projectGeneration) {
          disposeMeshResources(stagedResources);
          return false;
        }
        setMesh(baseline.mesh, baseline.countryIds, {
          stagedResources,
          renderFrame: false,
          quality: 'canonical',
          preserveOtherVariants: false,
        });
        baseline.resources = meshVariants.get('canonical')?.resources || null;
      } else {
        activateMeshVariant('canonical', { renderFrame: false });
        baseline.resources = activeEntry.resources;
      }
      projectRenderBlocked = false;
      previewAllowed = false;
      canonicalMeshReady = true;
      meshQuality = 'canonical';
      activeMeshQuality = 'canonical';
      markPaletteDirty({ base: true, emphasis: true });
      sceneColorCache.invalidate('built-in-project-transition');
      return waitForCanonicalVisualFrame(onStaged).then(() => true);
    }

    function uploadMeshResources(nextMesh, staged = null) {
      if (!gl || !nextMesh) return null;
      const resources = staged || {
        positionBuffer: lifecycle.create(gl, 'Buffer'),
        countryBuffer: lifecycle.create(gl, 'Buffer'),
        fillIndexBuffer: lifecycle.create(gl, 'Buffer'),
        lineIndexBuffer: lifecycle.create(gl, 'Buffer'),
        fillVao: null,
        lineVao: null,
        byteLength: Number(nextMesh.positions?.byteLength || 0)
          + Number(nextMesh.countryIndices?.byteLength || 0)
          + Number(nextMesh.triangleIndices?.byteLength || 0)
          + Number(nextMesh.lineIndices?.byteLength || 0),
      };
      if (!staged) {
      gl.bindBuffer(gl.ARRAY_BUFFER, resources.positionBuffer);
      if (glVersion === 2) gl.bufferData(gl.ARRAY_BUFFER, nextMesh.positions, gl.STATIC_DRAW);
      else {
        const positionData = Float32Array.from(nextMesh.positions);
        gl.bufferData(gl.ARRAY_BUFFER, positionData, gl.STATIC_DRAW);
      }
      gl.bindBuffer(gl.ARRAY_BUFFER, resources.countryBuffer);
      if (glVersion === 2) gl.bufferData(gl.ARRAY_BUFFER, nextMesh.countryIndices, gl.STATIC_DRAW);
      else {
        const countryData = Float32Array.from(nextMesh.countryIndices);
        gl.bufferData(gl.ARRAY_BUFFER, countryData, gl.STATIC_DRAW);
      }
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, resources.fillIndexBuffer);
      gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, nextMesh.triangleIndices, gl.STATIC_DRAW);
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, resources.lineIndexBuffer);
      gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, nextMesh.lineIndices, gl.STATIC_DRAW);
      }
      if (glVersion === 2) {
        const createVao = indexBuffer => {
          const vao = lifecycle.create(gl, 'VertexArray');
          gl.bindVertexArray(vao);
          gl.bindBuffer(gl.ARRAY_BUFFER, resources.positionBuffer);
          gl.enableVertexAttribArray(0);
          gl.vertexAttribIPointer(0, 2, gl.INT, 0, 0);
          gl.bindBuffer(gl.ARRAY_BUFFER, resources.countryBuffer);
          gl.enableVertexAttribArray(1);
          gl.vertexAttribIPointer(1, 1, gl.UNSIGNED_SHORT, 0, 0);
          gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, indexBuffer);
          gl.bindVertexArray(null);
          return vao;
        };
        resources.fillVao = createVao(resources.fillIndexBuffer);
        resources.lineVao = createVao(resources.lineIndexBuffer);
      }
      return resources;
    }

    async function stageMeshResources(nextMesh, { projectGeneration: generation = projectGeneration, contextGeneration = renderDeviceContextRevision, signal } = {}) {
      if (!gl) return null;
      if (!uploadScheduler) throw new Error('GPU upload scheduler is not connected');
      const uploadGl = gl;
      const resources = { fillVao: null, lineVao: null, byteLength: 0 };
      const tasks = [
        ['positionBuffer', nextMesh.positions, gl.ARRAY_BUFFER, glVersion !== 2],
        ['countryBuffer', nextMesh.countryIndices, gl.ARRAY_BUFFER, glVersion !== 2],
        ['fillIndexBuffer', nextMesh.triangleIndices, gl.ELEMENT_ARRAY_BUFFER, false],
        ['lineIndexBuffer', nextMesh.lineIndices, gl.ELEMENT_ARRAY_BUFFER, false],
      ].map(([key, data, target, convert]) => ({ key, data, target, convert, offset: 0 }));
      return uploadScheduler.enqueueUpload({
        key: `mesh:${++stagingSequence}`, projectGeneration: generation, contextGeneration, priority: 100,
        dispose: () => { for (const task of tasks) if (resources[task.key]) lifecycle.release(resources[task.key]); },
        step: ({ byteBudget }) => {
          if (signal?.aborted || generation !== projectGeneration || contextGeneration !== renderDeviceContextRevision || gl !== uploadGl || gl.isContextLost()) throw Object.assign(new Error('Stale mesh staging'), { name: 'AbortError' });
          const task = tasks.find(item => !item.done);
          if (!task) return { done: true, value: uploadMeshResources(nextMesh, resources) };
          const oldArray = gl.getParameter(gl.ARRAY_BUFFER_BINDING);
          const oldElement = gl.getParameter(gl.ELEMENT_ARRAY_BUFFER_BINDING);
          try {
            const length = task.convert ? task.data.length * 4 : task.data.byteLength;
            if (!resources[task.key]) {
              resources[task.key] = lifecycle.create(gl, 'Buffer');
              if (!resources[task.key]) throw new Error('Mesh staging allocation failed');
              gl.bindBuffer(task.target, resources[task.key]); gl.bufferData(task.target, length, gl.STATIC_DRAW);
              resources.byteLength += length;
              return { bytes: 0 };
            }
            gl.bindBuffer(task.target, resources[task.key]);
            const bytes = Math.min(length - task.offset, Math.floor(byteBudget / 4) * 4);
            const chunk = task.convert ? Float32Array.from(task.data.subarray(task.offset / 4, (task.offset + bytes) / 4)) : new Uint8Array(task.data.buffer, task.data.byteOffset + task.offset, bytes);
            if (bytes) gl.bufferSubData(task.target, task.offset, chunk);
            task.offset += bytes; task.done = task.offset === length;
            return { bytes };
          } finally { gl.bindBuffer(gl.ARRAY_BUFFER, oldArray); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, oldElement); }
        },
      });
    }

    function activateMeshVariant(quality, { renderFrame = true } = {}) {
      const requestedQuality = quality === 'preview' ? 'preview' : 'canonical';
      if (requestedQuality === 'preview') {
        previewActivationCount += 1;
        if (!previewAllowed || canonicalMeshReady || qualityPhase === 'canonical-ready') {
          previewActivationAfterCanonical += canonicalMeshReady || qualityPhase === 'canonical-ready' ? 1 : 0;
          return false;
        }
      }
      const entry = meshVariants.get(requestedQuality) || meshVariants.get('canonical');
      if (!entry) return false;
      const changed = activeMeshQuality !== entry.quality || mesh !== entry.mesh;
      activeMeshQuality = entry.quality;
      meshQuality = entry.quality;
      mesh = entry.mesh;
      meshCountryIds = entry.countryIds;
      if (changed) meshSwitchCount += 1;
      const resources = entry.resources;
      positionBuffer = resources?.positionBuffer || null;
      countryBuffer = resources?.countryBuffer || null;
      fillIndexBuffer = resources?.fillIndexBuffer || null;
      lineIndexBuffer = resources?.lineIndexBuffer || null;
      fillVao = resources?.fillVao || null;
      lineVao = resources?.lineVao || null;
      pickSceneKey = '';
      window.__PANDOLAB_GPU_METRICS__ = getStats();
      if (renderFrame && changed) renderLatestVisualFrame();
      return true;
    }

    function setMesh(nextMesh, countryIds, {
      renderFrame = true,
      quality = meshQuality,
      preserveOtherVariants = false,
      stagedResources = null,
    } = {}) {
      const variantQuality = quality === 'preview' ? 'preview' : 'canonical';
      if (variantQuality === 'preview' && (!previewAllowed || canonicalMeshReady || qualityPhase === 'canonical-ready')) {
        previewActivationAfterCanonical += 1;
        return false;
      }
      if (!preserveOtherVariants) {
        for (const entry of meshVariants.values()) disposeMeshEntry(entry);
        meshVariants.clear();
      } else if (meshVariants.has(variantQuality)) {
        disposeMeshEntry(meshVariants.get(variantQuality));
      }
      nextMesh.metadataCountryIds = [...countryIds];
      nextMesh.triangleRangesByCountryId = createCountryTriangleRangeMap(nextMesh, countryIds);
      const entry = {
        quality: variantQuality,
        mesh: nextMesh,
        countryIds: [...countryIds],
        resources: stagedResources || uploadMeshResources(nextMesh),
      };
      meshVariants.set(variantQuality, entry);
      if (builtinMeshBaseline?.mesh === nextMesh) builtinMeshBaseline.resources = entry.resources;
      activateMeshVariant(variantQuality, { renderFrame });
      prewarmCountryStrokeResources();
      projectRenderBlocked = false;
      sceneColorCache.invalidate('mesh-ready');
    }

    function promoteCanonicalMesh({ frameId = 0 } = {}) {
      canonicalMeshReady = true;
      recordCanonicalFrameReady();
      qualityPhase = 'canonical-ready';
      previewAllowed = false;
      meshQuality = 'canonical';
      canonicalPromotionCount += 1;
      canonicalReadyFrameId = Number(frameId || currentRenderRevision || 0);
      canonicalPromotionError = '';
      const previewEntry = meshVariants.get('preview');
      if (previewEntry) {
        if (previewEntry !== meshVariants.get('canonical')) disposeMeshEntry(previewEntry);
        meshVariants.delete('preview');
      }
      countryStrokePacketCache.preview.mesh = null;
      countryStrokePacketCache.preview.countryIds = null;
      countryStrokePacketCache.preview.revision = '';
      countryStrokePacketCache.preview.resource = null;
      return true;
    }

    function setOverrideMesh(nextMesh, { renderFrame = true, stagedResources = null } = {}) {
      if (nextMesh && !(nextMesh.triangleRangesByCountryId instanceof Map)) {
        nextMesh.triangleRangesByCountryId = createCountryTriangleRangeMap(
          nextMesh,
          nextMesh.metadataCountryIds || meshCountryIds,
        );
      }
      overrideMesh = nextMesh;
      // An override changes the pixels owned by the base scene.  Invalidate
      // the scene cache before uploading (and also when the override is
      // cleared) so a previous scene texture cannot leave the old country
      // border behind after the patch is displayed.
      lastBaseSceneResult = null;
      sceneColorCache.invalidate('country-override-mesh');
      countryStrokePacketCache.override.mesh = null;
      countryStrokePacketCache.override.resource = null;
      if (!gl || !isWebGlRenderer() || !nextMesh) {
        if (renderFrame) renderLatestVisualFrame();
        return;
      }
      const uploadBytes = Number(nextMesh.positions?.byteLength || 0)
        + Number(nextMesh.countryIndices?.byteLength || 0)
        + Number(nextMesh.triangleIndices?.byteLength || 0)
        + Number(nextMesh.lineIndices?.byteLength || 0);
      performanceMetrics.countryPatchUploadBytes += uploadBytes;
      performanceMetrics.lastCountryPatchUploadBytes = uploadBytes;
      if (!stagedResources) throw new Error('Override mesh requires staged GPU resources');
      for (const buffer of [overridePositionBuffer, overrideCountryBuffer, overrideFillIndexBuffer, overrideLineIndexBuffer]) if (buffer) lifecycle.release(buffer);
      if (glVersion === 2) { if (overrideFillVao) lifecycle.release(overrideFillVao); if (overrideLineVao) lifecycle.release(overrideLineVao); }
      overridePositionBuffer = stagedResources.positionBuffer; overrideCountryBuffer = stagedResources.countryBuffer;
      overrideFillIndexBuffer = stagedResources.fillIndexBuffer; overrideLineIndexBuffer = stagedResources.lineIndexBuffer;
      overrideFillVao = stagedResources.fillVao; overrideLineVao = stagedResources.lineVao;
      updatePalette();
      prewarmCountryStrokeResources();
      if (renderFrame) renderLatestVisualFrame();
    }

    const asMeshArray = (Type, value) => value instanceof Type ? value : new Type(value || []);
    function assertPreparedStroke(rawMesh, ids) {
      const prepared = rawMesh?.preparedStroke;
      const owners = new Set(ids.map(String));
      if (!(prepared?.instances instanceof Float32Array) || !(prepared?.nodes instanceof Float32Array)
        || !Object.keys(prepared.inputOwnerRanges || {}).every(id => owners.has(id))) {
        throw Object.assign(new Error('Prepared country stroke is missing or has invalid owners'), { code: 'PL-GPU-STROKE-001' });
      }
    }
    function remapOverrideMesh(rawMesh, localIds, globalIds = meshCountryIds) {
      assertPreparedStroke(rawMesh, localIds);
      const globalIndices = localIds.map(id => {
        const key = String(id);
        let index = globalIds.indexOf(key);
        if (index < 0) {
          globalIds.push(key);
          index = globalIds.length - 1;
        }
        return index;
      });
      const local = asMeshArray(Uint16Array, rawMesh.countryIndices);
      const countryIndices = new Uint16Array(local.length);
      for (let index = 0; index < local.length; index += 1) countryIndices[index] = globalIndices[local[index]];
      const remapped = {
        preparedStroke: rawMesh.preparedStroke,
        spatialBlocks: rawMesh.spatialBlocks,
        positions: asMeshArray(Int32Array, rawMesh.positions),
        countryIndices,
        triangleIndices: asMeshArray(Uint32Array, rawMesh.triangleIndices),
        lineIndices: asMeshArray(Uint32Array, rawMesh.lineIndices),
        strokeStartsEnds: asMeshArray(Float32Array, rawMesh.strokeStartsEnds),
        strokeOwnerRanges: rawMesh.strokeOwnerRanges || null,
        countryTriangleRanges: asMeshArray(Uint32Array, rawMesh.countryTriangleRanges),
        countryBoundaryRanges: asMeshArray(Uint32Array, rawMesh.countryBoundaryRanges),
        countryBounds: asMeshArray(Int32Array, rawMesh.countryBounds),
        countryBoundsFlags: asMeshArray(Uint32Array, rawMesh.countryBoundsFlags),
        metadataCountryIds: [...localIds],
      };
      remapped.triangleRangesByCountryId = createCountryTriangleRangeMap(remapped, localIds);
      return remapped;
    }

    const stopPatchWorkerJobs = reason => patchJobScheduler.cancelAll(reason);

    function completeGeometryDisplay(ids, geometryRevision, { renderFrame = true } = {}) {
      const cleared = geometryRevisionTracker.markDisplayed(ids, geometryRevision);
      if (renderFrame) {
        updatePalette();
        renderLatestVisualFrame();
      }
      for (const id of cleared) state.pendingCountryRenderIds.delete(String(id));
      renderPendingCountryOverlays?.();
      if (lastGeometryCommitTimings && Number(geometryRevision) === geometryRevisionTracker.committedRevision()) {
        lastGeometryCommitTimings.gpuPatchDisplayedAt ||= performance.now();
        if (!geometryRevisionTracker.pendingIds().length) lastGeometryCommitTimings.overlayRemovedAt ||= performance.now();
      }
      window.__PANDOLAB_GPU_METRICS__ = getStats();
      return cleared;
    }

    function normalizeCountryPatchRequest(rawRequest) {
      if (rawRequest && !Array.isArray(rawRequest) && typeof rawRequest === 'object' && rawRequest.ids) {
        const ids = [...new Set([...(rawRequest.ids || [])].map(String).filter(Boolean))];
        const byId = new Map((rawRequest.features || []).map(feature => [
          String(feature?.id || ''),
          feature,
        ]).filter(([id]) => id));
        const removed = new Set((rawRequest.removedIds || []).map(String));
        return {
          ids,
          features: ids.filter(id => !removed.has(id) && byId.has(id)).map(id => byId.get(id)),
          removedIds: ids.filter(id => removed.has(id) || !byId.has(id)),
        };
      }
      const ids = [...new Set([...(rawRequest || [])].map(String).filter(Boolean))];
      const features = [];
      const removedIds = [];
      for (const id of ids) {
        const feature = countryFeatureById(id);
        if (feature && String(feature?.id || '') === id) features.push(feature);
        else removedIds.push(id);
      }
      return { ids, features, removedIds };
    }

    function applyCountryPatch(rawRequest, { presentation = 'replace-scene' } = {}) {
      const { ids, features, removedIds } = normalizeCountryPatchRequest(rawRequest);
      if (!ids.length) return Promise.resolve(true);
      const qualityGate = decideCountryPatchPresentation({
        renderer: rendererMode,
        canonicalMeshReady,
        ids,
      });
      if (qualityGate.mode === 'defer') {
        for (const id of qualityGate.ids) {
          deferredCountryPatchIds.add(id);
          state.pendingCountryRenderIds.add(id);
        }
        renderPendingCountryOverlays?.();
        window.__PANDOLAB_GPU_METRICS__ = getStats();
        return Promise.resolve(true);
      }
      mapWorkScheduler.cancel('country-mesh-compaction');
      const commit = geometryRevisionTracker.beginCommit(ids);
      countryPatchPresentation = presentation === 'preserve-existing-scene'
        && !removedIds.length
        && isWebGlRenderer()
        && sceneColorCache.hasActiveProject?.(projectGeneration)
        ? {
          mode: 'preserve-existing-scene',
          phase: 'waiting-mesh',
          ids: new Set(ids),
          token: commit.token,
          geometryRevision: commit.revision,
          previousBaseResult: lastBaseSceneResult,
          heldFrameCount: 0,
        }
        : null;
      lastGeometryCommitTimings = {
        geometryRevision: commit.revision,
        editCommitAt: performance.now(),
        baseHiddenAt: 0,
        optimisticOverlayShownAt: 0,
        patchWorkerRequestedAt: 0,
        patchWorkerCompletedAt: 0,
        gpuPatchDisplayedAt: 0,
        overlayRemovedAt: 0,
      };
      for (const id of ids) countryOverrideIds.add(id);
      for (const feature of features) {
        const id = String(feature?.id || '');
        if (id) overrideFeatureSnapshots.set(id, deepClone(feature));
      }
      for (const id of removedIds) overrideFeatureSnapshots.delete(String(id));
      for (const id of ids) state.pendingCountryRenderIds.add(id);
      renderPendingCountryOverlays?.();
      lastGeometryCommitTimings.optimisticOverlayShownAt = performance.now();
      if (rendererMode === 'canvas-worker' && canvasWorker) {
        postCanvasWorkerMessage({
          type: 'patch',
          features,
          removedIds,
          ids,
          revision: currentRenderRevision,
          geometryRevision: commit.revision,
          taskToken: commit.token,
        });
        lastGeometryCommitTimings.patchWorkerRequestedAt = performance.now();
        renderViewFrame();
        lastGeometryCommitTimings.baseHiddenAt = performance.now();
        return Promise.resolve(true);
      }
      if (!isWebGlRenderer()) {
        renderViewFrame();
        lastGeometryCommitTimings.baseHiddenAt = performance.now();
        completeGeometryDisplay(ids, commit.revision, { renderFrame: false });
        return Promise.resolve(true);
      }
      updatePalette();
      renderViewFrame();
      if (!countryPatchPresentation) lastGeometryCommitTimings.baseHiddenAt = performance.now();
      const patchFeatures = [...overrideFeatureSnapshots.values()].map(deepClone);
      const snapshotIds = [...countryOverrideIds];
      const token = commit.token;
      // The worker result contains earlier queued patches too. Promote their
      // pending IDs together so a rapid second addition cannot leave a preview.
      if (countryPatchPresentation?.token === token) countryPatchPresentation.ids = new Set(snapshotIds);
      return lifecycle.nextFrame().then(() => {
        if (!geometryRevisionTracker.isCurrent(token, commit.revision)) return false;
        lastGeometryCommitTimings.patchWorkerRequestedAt = performance.now();
        const ticket = patchJobScheduler.enqueue({
          jobKey: 'mesh:country-overrides',
          geometryRevision: commit.revision,
          targetRevision: token,
          priority: 80,
          payload: { token, features: patchFeatures },
        });
        return ticket.promise.then(async next => {
          if (!next || !geometryRevisionTracker.isCurrent(token, commit.revision)) {
            if (countryPatchPresentation?.token === token) countryPatchPresentation = null;
            return false;
          }
          const remapped = remapOverrideMesh(next, next.countryIds || []);
          const stagedResources = await stageMeshResources(remapped);
          if (!geometryRevisionTracker.isCurrent(token, commit.revision)) { disposeMeshResources(stagedResources); return false; }
          if (countryPatchPresentation?.token === token && countryPatchPresentation.geometryRevision === commit.revision) {
            countryPatchPresentation.phase = 'staging';
          }
          setOverrideMesh(remapped, { renderFrame: false, stagedResources });
          if (countryPatchPresentation?.token === token && countryPatchPresentation.geometryRevision === commit.revision) {
            renderLatestVisualFrame();
          } else {
            completeGeometryDisplay(snapshotIds, commit.revision);
          }
          if (countryOverrideIds.size > 48 || (overrideMesh?.countryIndices?.length || 0) > (mesh?.countryIndices?.length || 1) * 0.25) {
            mapWorkScheduler.scheduleIdle('country-mesh-compaction', compactCountryOverrides, 2000);
          }
          return true;
        });
      }).catch(error => {
        if (disposed || error?.name === 'AbortError') return false;
        if (!geometryRevisionTracker.isCurrent(token, commit.revision)) return false;
        if (countryPatchPresentation?.token === token) countryPatchPresentation = null;
        console.error('[PL-GPU-PATCH-002]', error);
        scheduleGpuMeshRebuild(0);
        return false;
      });
    }

    function compactCountryOverrides() {
      if (!countryOverrideIds.size) return;
      rebuildFromCountries(state.countriesData?.features || [], {
        geometryRevision: geometryRevisionTracker.committedRevision(),
        reason: 'compaction',
      });
    }

    async function flushDeferredCountryPatches() {
      if (!canonicalMeshReady || !deferredCountryPatchIds.size) return true;
      const ids = [...deferredCountryPatchIds];
      deferredCountryPatchIds.clear();
      return applyCountryPatch(ids);
    }

    function resetCountryGeometryVisualState({ renderFrame = false, renderPending = true } = {}) {
      mapWorkScheduler.cancel('country-mesh-compaction');
      geometryRevisionTracker.reset();
      countryPatchPresentation = null;
      stopPatchWorkerJobs('geometry-reset');
      patchJobScheduler.cancelRebuild();
      countryOverrideIds.clear();
      deferredCountryPatchIds.clear();
      overrideFeatureSnapshots.clear();
      overrideMesh = null;
      // Clearing an override removes geometry from the base scene as well as
      // from the interaction state.  Drop the cached scene before the next
      // draw so the removed border cannot remain in the framebuffer.
      lastBaseSceneResult = null;
      sceneColorCache.invalidate('country-override-cleared');
      state.pendingCountryRenderIds.clear();
      lastGeometryCommitTimings = null;
      if (renderFrame) {
        updatePalette();
        renderLatestVisualFrame();
      }
      if (renderPending) renderPendingCountryOverlays?.();
      window.__PANDOLAB_GPU_METRICS__ = getStats();
      rendererUi.onContextStateChange?.('fallback');
    }

    function resetProjectRenderState({ generation = null, preserveBuiltinMesh = false } = {}) {
      previewFramePresented = false;
      for (const resolve of previewFrameWaiters) resolve(false);
      previewFrameWaiters.clear();
      pendingCanonicalCommit?.reject(Object.assign(new Error('Project replaced during canonical commit'), { name: 'AbortError' }));
      pendingCanonicalCommit = null;
      uploadScheduler?.cancelAll();
      const requested = Number(generation);
      projectGeneration = Number.isFinite(requested) && requested > projectGeneration
        ? requested
        : projectGeneration + 1;
      projectRenderBlocked = true;
      if (canvasWorker) {
        canvasWorker.terminate(); canvasWorker = null; canvasWorkerNeedsRestart = true;
      }
      canvasDataReplacementResolver?.(); canvasDataReplacementResolver = null;
      void hydroPreparation.restart();
      resetCountryGeometryVisualState({ renderFrame: false, renderPending: false });
      sceneColorCache.reset?.({ dropActive: !preserveBuiltinMesh });
      renderScene = null;
      renderInteractionState = Object.freeze({
        selectionPacket: null,
        genericFillItems: Object.freeze([]),
        previewPackets: Object.freeze([]),
        draftPackets: Object.freeze([]),
      });
      lastSelectionRenderResult = null;
      lastBaseSceneResult = null;
      lastSceneFrameContext = null;
      selectionPass?.clear?.();
      strokeRenderer.cancelPendingUploads();
      countryEmphasis = { primaryId: '', primaryIds: new Set(), priorities: {}, hoverId: '', selectedIds: new Set() };
      countryEmphasisRevision += 1;
      markPaletteDirty({ emphasis: true });
      if (!preserveBuiltinMesh) {
        for (const entry of meshVariants.values()) disposeMeshEntry(entry);
        meshVariants.clear();
        mesh = null;
        meshCountryIds = [];
        qualityPhase = previewAllowed ? 'startup-preview' : 'canonical-loading';
        activeMeshQuality = previewAllowed ? 'preview' : 'canonical';
        meshQuality = previewAllowed ? 'preview' : 'canonical';
        canonicalMeshReady = false;
      }
      terrainPreparation.reset();
      // Keep the previously committed pixels in place while a new project is
      // prepared.  They are replaced only by the prepared canonical frame.
      if (!preserveBuiltinMesh && gl && !gl.isContextLost?.()) {
        try {
          gl.bindFramebuffer(gl.FRAMEBUFFER, null);
          gl.viewport(0, 0, Math.max(1, pixelWidth), Math.max(1, pixelHeight));
          gl.clearColor(0, 0, 0, 0);
          gl.clear(gl.COLOR_BUFFER_BIT | gl.STENCIL_BUFFER_BIT);
        } catch (_) {
          // A reset must not surface a renderer cleanup failure as a runtime error.
        }
      }
      return projectGeneration;
    }

    async function decodeBuiltInMesh(rawBuffer = null, features = null, preparedStroke = null) {
      const buffer = rawBuffer || window.PANDOLAB_GPU_MESH_BUFFER;
      const ids = (features || window.PANDOLAB_COUNTRIES?.features || []).map((feature, index) => String(feature?.id || index));
      const decoded = decodeCountryMesh(buffer, ids);
      decoded.mesh.preparedStroke = preparedStroke || (!rawBuffer ? window.PANDOLAB_GPU_MESH_STROKES : null);
      if (!decoded.mesh.preparedStroke) throw new Error('국가 stroke Worker 결과가 준비되지 않았습니다.');
      if (!rawBuffer) {
        window.PANDOLAB_GPU_MESH_BUFFER = null;
        window.PANDOLAB_GPU_MESH_STROKES = null;
      }
      return decoded;
    }

    function rebuildFromCountries(features, {
      geometryRevision = geometryRevisionTracker.committedRevision(),
      reason = 'full-rebuild',
      projectGeneration: taskProjectGeneration = projectGeneration,
    } = {}) {
      if (Number(taskProjectGeneration) !== projectGeneration) return Promise.resolve(false);
      const task = geometryRevisionTracker.beginTask(geometryRevision);
      stopPatchWorkerJobs(reason);
      const pendingIds = geometryRevisionTracker.pendingIds();
      if (rendererMode === 'canvas-worker' && canvasWorker) {
        meshQuality = 'canonical';
        canonicalMeshReady = true;
        recordCanonicalFrameReady();
        qualityPhase = 'canonical-ready';
        previewAllowed = false;
        canonicalPromotionCount += 1;
        canonicalReadyFrameId = currentRenderRevision;
        projectRenderBlocked = false;
        postCanvasWorkerMessage({
          type: 'data', features, ids: pendingIds,
          revision: currentRenderRevision,
          geometryRevision: task.revision,
          taskToken: task.token,
          reason,
        });
        renderCanvasWorker(currentRenderRevision);
        return Promise.resolve(true);
      }
      if (!isWebGlRenderer()) {
        meshQuality = 'canonical';
        canonicalMeshReady = true;
        recordCanonicalFrameReady();
        qualityPhase = 'canonical-ready';
        previewAllowed = false;
        canonicalPromotionCount += 1;
        canonicalReadyFrameId = currentRenderRevision;
        projectRenderBlocked = false;
        countryOverrideIds.clear();
        overrideFeatureSnapshots.clear();
        overrideMesh = null;
        renderViewFrame();
        completeGeometryDisplay(pendingIds, task.revision, { renderFrame: false });
        return Promise.resolve(true);
      }
      const token = task.token;
      updateRendererStatus(`${rendererName()} · 편집 메시지를 계산하는 중입니다.`);
      return patchJobScheduler.rebuild({ token, projectGeneration: taskProjectGeneration, geometryRevision: task.revision, features },
        () => taskProjectGeneration === projectGeneration && geometryRevisionTracker.isCurrent(token, task.revision)).then(async next => {
          if (!next) return false;
          let stagedResources, nextMesh;
          try {
            assertPreparedStroke(next, next.countryIds || []);
            nextMesh = {
            preparedStroke: next.preparedStroke,
            spatialBlocks: next.spatialBlocks,
            positions: asMeshArray(Int32Array, next.positions),
            countryIndices: asMeshArray(Uint16Array, next.countryIndices),
            triangleIndices: asMeshArray(Uint32Array, next.triangleIndices),
            lineIndices: asMeshArray(Uint32Array, next.lineIndices),
            strokeStartsEnds: asMeshArray(Float32Array, next.strokeStartsEnds),
            strokeOwnerRanges: next.strokeOwnerRanges || null,
            countryTriangleRanges: asMeshArray(Uint32Array, next.countryTriangleRanges),
            countryBoundaryRanges: asMeshArray(Uint32Array, next.countryBoundaryRanges),
            countryBounds: asMeshArray(Int32Array, next.countryBounds),
            countryBoundsFlags: asMeshArray(Uint32Array, next.countryBoundsFlags),
          };
            stagedResources = await stageMeshResources(nextMesh, { projectGeneration: taskProjectGeneration });
          } catch (error) {
            if (error.name !== 'AbortError') console.error('[PL-GPU-MESH-STAGING]', error);
            return false;
          }
          if (taskProjectGeneration !== projectGeneration || !geometryRevisionTracker.isCurrent(token, task.revision)) { disposeMeshResources(stagedResources); return false; }
          countryOverrideIds.clear(); overrideFeatureSnapshots.clear(); overrideMesh = null;
          setMesh(nextMesh, next.countryIds || [], { stagedResources, renderFrame: false, quality: 'canonical', preserveOtherVariants: false });
          completeGeometryDisplay(pendingIds, task.revision);
          promoteCanonicalMesh({ frameId: currentRenderRevision });
          projectRenderBlocked = false;
          sceneColorCache.invalidate('project-mesh-ready');
          invalidateGpuFrame('project-mesh-ready');
          updateRendererStatus(`${rendererName()} · GPU 실시간`);
          return true;
      }).catch(error => {
        console.error('[PL-GPU-004]', error);
        activateCanvasFallback('동적 지도 메시 Worker를 사용할 수 없습니다.');
        return false;
      });
    }

    function parseColor(value) {
      const match = /^#([0-9a-f]{6})$/i.exec(String(value || ''));
      if (!match) return parseColor(defaultCountryColor());
      const n = Number.parseInt(match[1], 16);
      return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
    }

    function colorHex(values) {
      return `#${values.map(value => Math.max(0, Math.min(255, Math.round(value))).toString(16).padStart(2, '0')).join('')}`;
    }

    function countryEmphasisStyle(id) {
      const normalizedId = String(id || '');
      const kind = countryEmphasis.primaryIds.has(normalizedId) ? 'primary'
        : countryEmphasis.selectedIds.has(normalizedId) ? 'secondary'
          : normalizedId === countryEmphasis.hoverId ? 'hover' : '';
      if (!kind) return null;
      const fillAlpha = {
        primary: Math.round(Number(interactionStyle.selection.primary.fillAlpha || 0) * 255),
        secondary: Math.round(Number(interactionStyle.selection.secondary.fillAlpha || 0) * 255),
        hover: Math.round(Number(interactionStyle.hover.fillAlpha || 0) * 255),
      };
      const selectionRgb = parseColor(kind === 'hover' ? interactionStyle.hover.color : interactionStyle.selection.color);
      const styles = {
        primary: { color: selectionRgb, alphaByte: fillAlpha.primary },
        secondary: { color: selectionRgb, alphaByte: fillAlpha.secondary },
        hover: { color: selectionRgb, alphaByte: fillAlpha.hover },
      };
      return { kind, ...styles[kind] };
    }

    function configurePaletteTexture(texture) {
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, texture);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      const internalFormat = glVersion === 2 ? gl.RGBA8 : gl.RGBA;
      gl.texImage2D(gl.TEXTURE_2D, 0, internalFormat, paletteCapacity, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
    }

    function ensurePaletteStorage() {
      const capacity = Math.max(1, meshCountryIds.length);
      if (palettePixels && paletteCapacity === capacity) return;
      paletteCapacity = capacity;
      palettePixels = {
        base: new Uint8Array(capacity * 4),
        override: new Uint8Array(capacity * 4),
        emphasis: new Uint8Array(capacity * 4),
        overrideEmphasis: new Uint8Array(capacity * 4),
      };
      for (const texture of [paletteTexture, overridePaletteTexture, emphasisPaletteTexture, overrideEmphasisPaletteTexture]) configurePaletteTexture(texture);
      paletteDirty.base = true;
      paletteDirty.emphasis = true;
    }

    function uploadPalettePixels(texture, pixels) {
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, texture);
      gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, paletteCapacity, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
      performanceMetrics.paletteUploadCount += 1;
      performanceMetrics.paletteUploadBytes += pixels.byteLength;
    }

    function uploadPaletteRange(texture, pixels, first, count) {
      if (!count) return;
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, texture);
      const offset = first * 4;
      const range = pixels.subarray(offset, offset + count * 4);
      gl.texSubImage2D(gl.TEXTURE_2D, 0, first, 0, count, 1, gl.RGBA, gl.UNSIGNED_BYTE, range);
      performanceMetrics.paletteUploadCount += 1;
      performanceMetrics.paletteUploadBytes += range.byteLength;
    }

    function markPaletteDirty({ base = false, emphasis = false, countryIds = null } = {}) {
      paletteDirty.base ||= base;
      paletteDirty.emphasis ||= emphasis;
      if (emphasis) {
        if (Array.isArray(countryIds) && countryIds.length && !emphasisPaletteFullDirty) {
          for (const id of countryIds) pendingEmphasisCountryIds.add(String(id));
        } else if (!Array.isArray(countryIds) || !countryIds.length) {
          emphasisPaletteFullDirty = true;
          pendingEmphasisCountryIds.clear();
        }
      }
    }

    function flushPaletteUpdates() {
      if (!gl || !meshCountryIds.length || (!paletteDirty.base && !paletteDirty.emphasis)) return false;
      ensurePaletteStorage();
      const { base, override, emphasis, overrideEmphasis } = palettePixels;
      if (paletteDirty.base) {
        pendingOldMeshVisibleCount = 0;
        for (let index = 0; index < meshCountryIds.length; index += 1) {
          const id = meshCountryIds[index];
          const feature = countryFeatureById(id);
          const color = parseColor(feature ? countryColor(feature) : '#000000');
          const offset = index * 4;
          base[offset] = override[offset] = color[0];
          base[offset + 1] = override[offset + 1] = color[1];
          base[offset + 2] = override[offset + 2] = color[2];
          const visible = feature && isCountryVisibleById(id) ? mapTheme().fillAlphaByte : 0;
          const overridden = countryOverrideIds.has(id);
          const pending = geometryRevisionTracker.isPending(id);
          base[offset + 3] = overridden ? 0 : visible;
          override[offset + 3] = overridden && (!pending || countryPatchPresentationCanDrawOverride(id)) ? visible : 0;
          if (pending && (base[offset + 3] || override[offset + 3])) pendingOldMeshVisibleCount += 1;
        }
        uploadPalettePixels(paletteTexture, base);
        uploadPalettePixels(overridePaletteTexture, override);
        paletteDirty.base = false;
        performanceMetrics.paletteRebuildCount += 1;
      }
      if (paletteDirty.emphasis) {
        const indices = emphasisPaletteFullDirty
          ? meshCountryIds.map((_, index) => index)
          : [...pendingEmphasisCountryIds]
            .map(id => meshCountryIds.indexOf(id))
            .filter(index => index >= 0);
        performanceMetrics.paletteChangedCountryCount += indices.length;
        for (const index of indices) {
          const id = meshCountryIds[index];
          const offset = index * 4;
          const visible = base[offset + 3] || override[offset + 3];
          const overridden = countryOverrideIds.has(id);
          const pending = geometryRevisionTracker.isPending(id);
          const entry = countryEmphasisStyle(id);
          const color = entry?.color || [0, 0, 0];
          emphasis[offset] = overrideEmphasis[offset] = color[0];
          emphasis[offset + 1] = overrideEmphasis[offset + 1] = color[1];
          emphasis[offset + 2] = overrideEmphasis[offset + 2] = color[2];
          const alpha = visible && entry ? entry.alphaByte : 0;
          emphasis[offset + 3] = overridden ? 0 : alpha;
          overrideEmphasis[offset + 3] = overridden && (!pending || countryPatchPresentationCanDrawOverride(id)) ? alpha : 0;
        }
        if (emphasisPaletteFullDirty) {
          uploadPalettePixels(emphasisPaletteTexture, emphasis);
          uploadPalettePixels(overrideEmphasisPaletteTexture, overrideEmphasis);
          performanceMetrics.paletteFullRebuildCount += 1;
        } else if (indices.length) {
          const sorted = [...new Set(indices)].sort((a, b) => a - b);
          let start = sorted[0];
          let previous = start;
          for (let index = 1; index <= sorted.length; index += 1) {
            const current = sorted[index];
            if (current !== previous + 1) {
              const count = previous - start + 1;
              uploadPaletteRange(emphasisPaletteTexture, emphasis, start, count);
              uploadPaletteRange(overrideEmphasisPaletteTexture, overrideEmphasis, start, count);
              performanceMetrics.paletteUploadRangeCount += 1;
              start = current;
            }
            previous = current;
          }
        }
        paletteDirty.emphasis = false;
        emphasisPaletteFullDirty = false;
        pendingEmphasisCountryIds.clear();
        performanceMetrics.paletteRebuildCount += 1;
      }
      return true;
    }

    function updatePalette(domains = null) {
      const nextDomains = domains || { base: true, emphasis: true };
      markPaletteDirty(nextDomains);
      if (nextDomains.base) countryPaletteRevision += 1;
      return flushPaletteUpdates();
    }

    function invalidateCountryPalette(domains = null, reason = 'country-palette') {
      const nextDomains = domains || { base: true, emphasis: true };
      markPaletteDirty(nextDomains);
      if (nextDomains.base) countryPaletteRevision += 1;
      if (rendererMode !== 'pending') invalidateGpuFrame(reason);
      return true;
    }

    function getRenderViewState() {
      if (activeRenderViewState && typeof activeRenderViewState === 'object') return activeRenderViewState;
      const projection = state.projection;
      const active = projection === 'globe' ? globeProjection : flatProjection;
      return {
        revision: currentRenderRevision,
        projection,
        flatProjectionKind: 'equirectangular',
        size: { width: state.size.width, height: state.size.height },
        dpr: resolveRenderPixelRatio(),
        translate: active.translate().map(Number),
        scale: Number(active.scale()),
        rotation: projection === 'globe' ? state.view.globeRotation.map(Number) : null,
        projectionCenter: projection === 'flat' ? state.view.flatCenter.map(Number) : null,
        zoom: Number(projection === 'globe' ? state.view.globeZoom : state.view.flatZoom),
      };
    }

    function setViewUniforms(program, worldOffset = 0, frameContext = activeFrameContext || lastVisualFrame) {
      if (!frameContext) return false;
      return setGpuViewUniforms(gl, {
        frameContext,
        worldOffset,
        getLocation: name => cachedUniformLocation(program, name),
      });
    }

    function resize() {
      if (!canvas) return;
      cssWidth = Math.max(1, state.size.width);
      cssHeight = Math.max(1, state.size.height);
      const dpr = resolveRenderPixelRatio();
      const nextWidth = Math.max(1, Math.round(cssWidth * dpr));
      const nextHeight = Math.max(1, Math.round(cssHeight * dpr));
      const backingChanged = pixelWidth !== nextWidth || pixelHeight !== nextHeight || canvas.width !== nextWidth || canvas.height !== nextHeight;
      if (backingChanged) {
        canvas.width = nextWidth;
        canvas.height = nextHeight;
        pickFramebuffer = null;
        pickTexture = null;
        pickSceneKey = '';
        sceneColorCache.invalidate('viewport-resize');
      }
      pixelWidth = nextWidth;
      pixelHeight = nextHeight;
      canvas.style.width = '100%';
      canvas.style.height = '100%';
      resizePending = false;
    }

    function layoutMismatch() {
      const mapElement = rendererUi.getMapElement();
      if (!canvas || !mapElement?.isConnected || !canvas.isConnected) return 0;
      const mapRect = mapElement.getBoundingClientRect();
      const canvasRect = canvas.getBoundingClientRect();
      lastLayoutMismatchCssPx = Math.max(
        Math.abs(mapRect.left - canvasRect.left),
        Math.abs(mapRect.top - canvasRect.top),
        Math.abs(mapRect.width - canvasRect.width),
        Math.abs(mapRect.height - canvasRect.height),
      );
      return lastLayoutMismatchCssPx;
    }

    function verifyLayout() {
      if (!canvas) return true;
      const mismatch = layoutMismatch();
      if (mismatch <= 0.5) {
        layoutMismatchCount = 0;
        return true;
      }
      canvas.style.position = 'absolute';
      canvas.style.inset = '0';
      canvas.style.width = '100%';
      canvas.style.height = '100%';
      resize();
      if (layoutVerificationFrame) lifecycle.cancelFrame(layoutVerificationFrame);
      layoutVerificationFrame = lifecycle.frame(() => {
        layoutVerificationFrame = 0;
        if (layoutMismatch() <= 0.5) {
          layoutMismatchCount = 0;
          renderLatestVisualFrame();
          return;
        }
        layoutMismatchCount += 1;
        if (layoutMismatchCount <= 3) {
          queueMapResize();
          verifyLayout();
        } else {
          console.warn('지도 레이어 크기를 완전히 맞추지 못했습니다.', layoutMismatch());
          layoutMismatchCount = 0;
        }
      });
      return false;
    }

    function bindWebGl1Attributes(program, indexBuffer, resources = null) {
      const coordLocation = cachedAttributeLocation(program, 'aCoord');
      const countryLocation = cachedAttributeLocation(program, 'aCountry');
      gl.bindBuffer(gl.ARRAY_BUFFER, resources?.positionBuffer || positionBuffer);
      if (coordLocation >= 0) {
        gl.enableVertexAttribArray(coordLocation);
        gl.vertexAttribPointer(coordLocation, 2, gl.FLOAT, false, 0, 0);
      }
      gl.bindBuffer(gl.ARRAY_BUFFER, resources?.countryBuffer || countryBuffer);
      if (countryLocation >= 0) {
        gl.enableVertexAttribArray(countryLocation);
        gl.vertexAttribPointer(countryLocation, 1, gl.FLOAT, false, 0, 0);
      }
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, indexBuffer);
      return [coordLocation, countryLocation];
    }

    function drawProgram(program, vao, indexBuffer, indexCount, primitive, resources = null, palette = paletteTexture, lineColor = null, lineWidth = null, drawRanges = null) {
      gl.useProgram(program);
      if (program === fillProgram || program === lineProgram || program === pickProgram) {
        gl.uniform1i(cachedUniformLocation(program, 'uPalette'), 0);
        const paletteWidthLocation = cachedUniformLocation(program, 'uPaletteWidth');
        if (paletteWidthLocation) gl.uniform1f(paletteWidthLocation, Math.max(1, meshCountryIds.length));
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, palette);
      }
      if (program === lineProgram) {
        const theme = mapTheme();
        gl.lineWidth(Math.max(1, Number(lineWidth) || Number(theme.borderWidth) || 1));
        const color = lineColor || [theme.borderGpu[0], theme.borderGpu[1], theme.borderGpu[2], theme.borderAlpha];
        gl.uniform4f(cachedUniformLocation(program, 'uBorderColor'), color[0], color[1], color[2], color[3]);
      }
      const webGl1Locations = glVersion === 2 ? null : bindWebGl1Attributes(program, indexBuffer, resources);
      if (glVersion === 2) gl.bindVertexArray(vao);
      const frameContext = activeFrameContext || lastVisualFrame;
      if (!frameContext) return null;
      const ranges = Array.isArray(drawRanges)
        ? drawRanges
        : [{ first: 0, count: indexCount }];
      for (const worldOffset of frameContext.worldOffsets) {
        setViewUniforms(program, worldOffset, frameContext);
        for (const range of ranges) {
          const count = Math.max(0, Number(range?.count || 0));
          if (count) gl.drawElements(primitive, count, gl.UNSIGNED_INT, Math.max(0, Number(range?.first || 0)) * Uint32Array.BYTES_PER_ELEMENT);
        }
      }
      if (glVersion === 2) gl.bindVertexArray(null);
      else {
        for (const location of webGl1Locations) if (location >= 0) gl.disableVertexAttribArray(location);
      }
    }

    function hydroLineParts(geometry) {
      if (geometry?.type === 'LineString') return [geometry.coordinates || []];
      if (geometry?.type === 'MultiLineString') return geometry.coordinates || [];
      return [];
    }

    function hydroPolygonParts(geometry) {
      if (geometry?.type === 'Polygon') return [geometry.coordinates || []];
      if (geometry?.type === 'MultiPolygon') return geometry.coordinates || [];
      return [];
    }

    function buildHydroEditMesh(features, firstFid) {
      const riverStarts = [], riverEnds = [], riverFeatureIds = [], riverStartWidths = [], riverEndWidths = [];
      const borderRiverStarts = [], borderRiverEnds = [], borderRiverFeatureIds = [], borderRiverStartWidths = [], borderRiverEndWidths = [];
      const lakePositions = [], lakeFeatureIds = [], lakeIndices = [];
      const lakeBoundaryStarts = [], lakeBoundaryEnds = [], lakeBoundaryFeatureIds = [], lakeBoundaryWidths = [];
      for (let featureIndex = 0; featureIndex < features.length; featureIndex += 1) {
        const feature = features[featureIndex];
        const fid = firstFid + featureIndex;
        hydroEditFeatureByFid.set(fid, feature);
        if (feature.properties?.category !== 'lake') {
          const aligned = feature.properties?.border_aligned === true;
          const starts = aligned ? borderRiverStarts : riverStarts;
          const ends = aligned ? borderRiverEnds : riverEnds;
          const ids = aligned ? borderRiverFeatureIds : riverFeatureIds;
          const startWidths = aligned ? borderRiverStartWidths : riverStartWidths;
          const endWidths = aligned ? borderRiverEndWidths : riverEndWidths;
          for (const part of hydroLineParts(feature.geometry)) for (let index = 0; index < part.length - 1; index += 1) {
            starts.push(Math.round(part[index][0] * 1e6), Math.round(part[index][1] * 1e6));
            ends.push(Math.round(part[index + 1][0] * 1e6), Math.round(part[index + 1][1] * 1e6));
            ids.push(fid);
            const width = Number(feature.properties?.stroke_width || 1);
            startWidths.push(width); endWidths.push(width);
          }
          continue;
        }
        for (const polygon of hydroPolygonParts(feature.geometry)) {
          const vertices = [], holes = [];
          for (let ringIndex = 0; ringIndex < polygon.length; ringIndex += 1) {
            const sourceRing = polygon[ringIndex] || [];
            const last = sourceRing[sourceRing.length - 1];
            const ring = sourceRing.length > 1 && sourceRing[0][0] === last?.[0] && sourceRing[0][1] === last?.[1]
              ? sourceRing.slice(0, -1) : sourceRing;
            if (ring.length < 3) continue;
            if (ringIndex) holes.push(vertices.length / 2);
            for (const point of ring) vertices.push(point[0], point[1]);
          }
          if (vertices.length < 6) continue;
          const base = lakePositions.length / 2;
          const triangles = globalThis.earcut?.(vertices, holes, 2) || [];
          for (let index = 0; index < vertices.length; index += 2) {
            lakePositions.push(Math.round(vertices[index] * 1e6), Math.round(vertices[index + 1] * 1e6));
            lakeFeatureIds.push(fid);
          }
          for (const triangle of triangles) lakeIndices.push(base + triangle);
        }
        const outline = countryOutlineFeature(feature);
        for (const line of outline?.geometry?.coordinates || []) for (let index = 0; index < line.length - 1; index += 1) {
          const start = line[index], end = line[index + 1];
          lakeBoundaryStarts.push(Math.round(start[0] * 1e6), Math.round(start[1] * 1e6));
          lakeBoundaryEnds.push(Math.round(end[0] * 1e6), Math.round(end[1] * 1e6));
          lakeBoundaryFeatureIds.push(fid); lakeBoundaryWidths.push(1);
        }
      }
      return {
        riverStarts: new Int32Array(riverStarts), riverEnds: new Int32Array(riverEnds), riverFeatureIds: new Uint32Array(riverFeatureIds),
        riverStartWidths: new Float32Array(riverStartWidths), riverEndWidths: new Float32Array(riverEndWidths),
        borderRiverStarts: new Int32Array(borderRiverStarts), borderRiverEnds: new Int32Array(borderRiverEnds), borderRiverFeatureIds: new Uint32Array(borderRiverFeatureIds),
        borderRiverStartWidths: new Float32Array(borderRiverStartWidths), borderRiverEndWidths: new Float32Array(borderRiverEndWidths),
        lakePositions: new Int32Array(lakePositions), lakeFeatureIds: new Uint32Array(lakeFeatureIds), lakeIndices: new Uint32Array(lakeIndices),
        lakeBoundaryStarts: new Int32Array(lakeBoundaryStarts), lakeBoundaryEnds: new Int32Array(lakeBoundaryEnds),
        lakeBoundaryFeatureIds: new Uint32Array(lakeBoundaryFeatureIds), lakeBoundaryWidths: new Float32Array(lakeBoundaryWidths),
      };
    }

    function setHydroEdits(features = [], revision = 0) {
      const nextRevision = Number(revision || 0);
      if (nextRevision === hydroPreparation.editRevision) return false;
      const entries = [];
      hydroEditFeatureByFid.clear();
      const baseFid = Math.max(0, Number(hydroPreparation.manifest?.stats?.featureCount || 0));
      let nextFid = baseFid;
      const groups = new Map();
      for (const feature of features || []) {
        if (!feature?.geometry) continue;
        const category = feature.properties?.category === 'lake' ? 'lake' : 'river';
        const color = String(feature.properties?.editorColor || hydroDisplayColor(category));
        const key = `${category}:${color}`;
        if (!groups.has(key)) groups.set(key, { category, color, features: [] });
        groups.get(key).features.push(feature);
      }
      for (const group of groups.values()) {
        const meshData = buildHydroEditMesh(group.features, nextFid);
        const entry = { id: `edit:${group.category}:${group.color}`, mesh: meshData, color: group.color, resources: null, uploadQueued: false, lastUsed: performance.now() };
        entry.byteLength = Object.values(meshData).reduce((sum, value) => sum + value.byteLength, 0);
        entries.push(entry);
        nextFid += group.features.length;
      }
      hydroPreparation.replaceEdits(entries, nextRevision);
      hydroVisibilityDirty = true;
      if (rendererMode === 'canvas-worker' && canvasWorker) postCanvasWorkerMessage({ type: 'hydro-edits', revision: hydroPreparation.editRevision, features: features || [] });
      invalidatePhysicalScene('hydro-edit-data');
      return true;
    }

    function updateHydroVisibility() {
      if (!gl || !hydroVisibilityTexture || !hydroVisibilityDirty) return;
      const count = Math.max(1, Number(hydroPreparation.manifest?.stats?.featureCount || 0) + hydroEditFeatureByFid.size);
      hydroVisibilityWidth = Math.min(4096, Math.max(1, count));
      hydroVisibilityHeight = Math.ceil(count / hydroVisibilityWidth);
      const pixels = new Uint8Array(hydroVisibilityWidth * hydroVisibilityHeight * 4);
      for (const [fidValue, feature] of state.hydroFeatureByFid?.entries?.() || []) {
        const fid = Number(fidValue);
        if (!Number.isInteger(fid) || fid < 0 || fid >= count || !isHydroFeatureVisible(feature)) continue;
        const offset = fid * 4;
        pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = pixels[offset + 3] = 255;
      }
      for (const [fid, feature] of hydroEditFeatureByFid) {
        if (!isHydroFeatureVisible(feature)) continue;
        const offset = fid * 4;
        pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = pixels[offset + 3] = 255;
      }
      gl.activeTexture(gl.TEXTURE2);
      gl.bindTexture(gl.TEXTURE_2D, hydroVisibilityTexture);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      const internalFormat = glVersion === 2 ? gl.RGBA8 : gl.RGBA;
      gl.texImage2D(gl.TEXTURE_2D, 0, internalFormat, hydroVisibilityWidth, hydroVisibilityHeight, 0, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
      hydroVisibilityDirty = false;
    }

    function bindLakeAttributes(program, resources) {
      const coordLocation = glVersion === 2 ? 0 : cachedAttributeLocation(program, 'aCoord');
      const featureLocation = glVersion === 2 ? 1 : cachedAttributeLocation(program, 'aCountry');
      gl.bindBuffer(gl.ARRAY_BUFFER, resources.lakePositionBuffer);
      gl.enableVertexAttribArray(coordLocation);
      if (glVersion === 2) gl.vertexAttribIPointer(coordLocation, 2, gl.INT, 0, 0);
      else gl.vertexAttribPointer(coordLocation, 2, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, resources.lakeFeatureBuffer);
      gl.enableVertexAttribArray(featureLocation);
      if (glVersion === 2) gl.vertexAttribIPointer(featureLocation, 1, gl.UNSIGNED_INT, 0, 0);
      else gl.vertexAttribPointer(featureLocation, 1, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, resources.lakeIndexBuffer);
      return [coordLocation, featureLocation];
    }

    function setHydroUniforms(program, color) {
      gl.useProgram(program);
      gl.activeTexture(gl.TEXTURE2);
      gl.bindTexture(gl.TEXTURE_2D, hydroVisibilityTexture);
      gl.uniform1i(cachedUniformLocation(program, 'uHydroVisibility'), 2);
      const sizeLocation = cachedUniformLocation(program, 'uHydroVisibilitySize');
      if (glVersion === 2) gl.uniform2i(sizeLocation, hydroVisibilityWidth, hydroVisibilityHeight);
      else gl.uniform2f(sizeLocation, hydroVisibilityWidth, hydroVisibilityHeight);
      const colorLocation = cachedUniformLocation(program, 'uHydroColor');
      if (colorLocation && color) gl.uniform4fv(colorLocation, color);
    }

    function setInstanceDivisor(location, divisor) {
      if (glVersion === 2) gl.vertexAttribDivisor(location, divisor);
      else instancedExtension.vertexAttribDivisorANGLE(location, divisor);
    }

    function bindRiverAttributes(program, resources, category = 'river') {
      const locations = glVersion === 2 ? [0, 1, 2, 3, 4, 5] : [
        cachedAttributeLocation(program, 'aCorner'), cachedAttributeLocation(program, 'aStart'),
        cachedAttributeLocation(program, 'aEnd'), cachedAttributeLocation(program, 'aCountry'),
        cachedAttributeLocation(program, 'aStartWidth'), cachedAttributeLocation(program, 'aEndWidth'),
      ];
      const [corner, start, end, feature, startWidth, endWidth] = locations;
      const prefix = category === 'lake-boundary' ? 'lakeBoundary' : category === 'border-river' ? 'borderRiver' : 'river';
      gl.bindBuffer(gl.ARRAY_BUFFER, hydroCornerBuffer);
      gl.enableVertexAttribArray(corner);
      gl.vertexAttribPointer(corner, 2, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, resources[`${prefix}StartBuffer`]);
      gl.enableVertexAttribArray(start);
      if (glVersion === 2) gl.vertexAttribIPointer(start, 2, gl.INT, 0, 0);
      else gl.vertexAttribPointer(start, 2, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, resources[`${prefix}EndBuffer`]);
      gl.enableVertexAttribArray(end);
      if (glVersion === 2) gl.vertexAttribIPointer(end, 2, gl.INT, 0, 0);
      else gl.vertexAttribPointer(end, 2, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, resources[`${prefix}FeatureBuffer`]);
      gl.enableVertexAttribArray(feature);
      if (glVersion === 2) gl.vertexAttribIPointer(feature, 1, gl.UNSIGNED_INT, 0, 0);
      else gl.vertexAttribPointer(feature, 1, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, resources[`${prefix}StartWidthBuffer`]);
      gl.enableVertexAttribArray(startWidth);
      gl.vertexAttribPointer(startWidth, 1, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, resources[`${prefix}EndWidthBuffer`]);
      gl.enableVertexAttribArray(endWidth);
      gl.vertexAttribPointer(endWidth, 1, gl.FLOAT, false, 0, 0);
      setInstanceDivisor(corner, 0);
      for (const location of [start, end, feature, startWidth, endWidth]) setInstanceDivisor(location, 1);
      return locations;
    }

    function drawHydroEntry(program, entry, category, color = null, picking = false) {
      const resources = entry.resources;
      if (!resources) return;
      const borderAligned = category === 'border-river';
      const count = category === 'lake'
        ? resources.lakeIndexCount
        : category === 'lake-boundary'
          ? resources.lakeBoundarySegmentCount
          : (borderAligned ? resources.borderRiverSegmentCount : resources.riverSegmentCount);
      if (!count) return;
      setHydroUniforms(program, color);
      const locations = category === 'lake' ? bindLakeAttributes(program, resources) : bindRiverAttributes(program, resources, category);
      const widthBoostLocation = cachedUniformLocation(program, 'uWidthBoost');
      if (widthBoostLocation) gl.uniform1f(widthBoostLocation, picking ? 6 : 0);
      const widthScaleLocation = cachedUniformLocation(program, 'uWidthScale');
      if (widthScaleLocation) {
        const theme = mapTheme();
        const width = category === 'lake-boundary' ? theme.lakeBoundaryWidth : theme.riverWidth;
        gl.uniform1f(widthScaleLocation, Math.max(0.5, Number(width) || 1));
      }
      const frameContext = activeFrameContext || lastVisualFrame;
      if (!frameContext) return false;
      for (const offset of frameContext.worldOffsets) {
        setViewUniforms(program, offset, frameContext);
        if (category === 'lake') gl.drawElements(gl.TRIANGLES, count, gl.UNSIGNED_INT, 0);
        else if (glVersion === 2) gl.drawArraysInstanced(gl.TRIANGLE_STRIP, 0, 4, count);
        else instancedExtension.drawArraysInstancedANGLE(gl.TRIANGLE_STRIP, 0, 4, count);
      }
      if (category !== 'lake') for (const location of locations.slice(1)) setInstanceDivisor(location, 0);
      for (const location of locations) gl.disableVertexAttribArray(location);
      entry.lastUsed = performance.now();
    }

    function drawHydro(category, picking = false) {
      if ((!hydroPreparation.manifest || !hydroPreparation.activeIds().length) && !hydroPreparation.editEntries().length) return;
      const theme = mapTheme();
      const isLake = category === 'lake' || category === 'lake-boundary';
      if (state.layerVisibility[isLake ? 'lakes' : 'rivers'] === false) return;
      const hydroOpacity = Number.isFinite(Number(isLake ? theme.lakeOpacity : theme.riverOpacity))
        ? Math.max(0, Math.min(1, Number(isLake ? theme.lakeOpacity : theme.riverOpacity)))
        : 1;
      if (hydroOpacity <= 0 || (category === 'lake-boundary' && theme.lakeBoundaryVisible === false)) return;
      const program = category === 'river' || category === 'border-river' || category === 'lake-boundary'
        ? (picking ? hydroLinePickProgram : hydroLineProgram)
        : (picking ? hydroPickProgram : hydroFillProgram);
      const rgb = hydroDisplayColor(isLake ? 'lake' : 'river', true);
      const color = [...rgb, hydroOpacity];
      for (const packId of hydroPreparation.activeIds()) {
        const entry = hydroPreparation.pack(packId);
        if (entry) drawHydroEntry(program, entry, category, color, picking);
      }
      for (const entry of picking ? [] : hydroPreparation.editEntries()) {
        const editRgb = parseColor(entry.color).map(value => value / 255);
        drawHydroEntry(program, entry, category, [...editRgb, hydroOpacity], picking);
      }
    }

    let hydroRenderFrame = 0;
    function queueHydroRender(reason = 'hydro-ready') {
      if (hydroRenderFrame) return;
      hydroRenderFrame = lifecycle.frame(() => {
        hydroRenderFrame = 0;
        invalidatePhysicalScene(reason);
      });
    }

    function registerHydroDescriptors(descriptors) {
      const logicalIds = new Set();
      for (const row of descriptors || []) {
        const logicalId = String(row.awId || row.logicalFid);
        let aggregate = state.hydroFeatureCache.get(logicalId);
        if (!aggregate) {
          aggregate = {
            type: 'Feature', id: logicalId, geometry: null,
            properties: {
              pandolab_id: logicalId, __logicalFid: Number(row.logicalFid),
              category: row.category, layer_id: row.layerId,
              name: row.name || '', name_ko: row.name || '', source: row.source || '',
              system_id: row.systemId || '', mainstem_name_ko: row.mainstemNameKo || row.name || '', role: row.role || '',
              source_id: row.sourceId || '', fragment_count: Number(row.fragmentCount || 1),
              min_zoom: Number(row.minZoom ?? 99), stroke_width: Number(row.width || 1), pack_ids: [],
            },
            __awBounds: [Infinity, Infinity, -Infinity, -Infinity],
          };
        }
        aggregate.properties.pack_ids = [...new Set([...(aggregate.properties.pack_ids || []), Number(row.packId)])];
        aggregate.properties.min_zoom = Math.min(Number(aggregate.properties.min_zoom ?? 99), Number(row.minZoom ?? 99));
        const bounds = (row.bounds || []).map(value => Number(value) / 1e6);
        if (bounds.length === 4) {
          aggregate.__awBounds = [
            Math.min(aggregate.__awBounds[0], bounds[0]), Math.min(aggregate.__awBounds[1], bounds[1]),
            Math.max(aggregate.__awBounds[2], bounds[2]), Math.max(aggregate.__awBounds[3], bounds[3]),
          ];
        }
        const b = aggregate.__awBounds;
        aggregate.__awCentroid = [(b[0] + b[2]) / 2, (b[1] + b[3]) / 2];
        aggregate.__awRadius = Math.min(180, Math.hypot(b[2] - b[0], b[3] - b[1]) / 2);
        state.hydroFeatureCache.set(logicalId, aggregate);
        state.hydroFeatureByFid.set(Number(row.fid), aggregate);
        logicalIds.add(logicalId);
      }
      if (logicalIds.size) hydroVisibilityDirty = true;
    }

    function aggregateHydroLogicalFeature(logicalId) {
      const fragments = state.hydroFragmentsByLogicalId.get(logicalId);
      if (!fragments?.size) {
        state.hydroFragmentsByLogicalId.delete(logicalId);
        state.hydroFeatureCache.delete(logicalId);
        return null;
      }
      const ordered = [...fragments.values()].sort((left, right) => (
        Number(left.properties?.fragment_index || 0) - Number(right.properties?.fragment_index || 0)
      ));
      const first = ordered[0];
      let aggregate = first;
      if (first.properties?.category === 'river') {
        const parts = ordered.flatMap(feature => feature.geometry?.type === 'LineString' ? [feature.geometry.coordinates] : feature.geometry?.coordinates || []);
        const widths = ordered.flatMap(feature => feature.properties?.stroke_widths || []);
        const sourceIds = [...new Set(ordered.flatMap(feature => String(feature.properties?.source_id || '').split(',').filter(Boolean)))];
        aggregate = {
          type: 'Feature', id: logicalId,
          properties: {
            ...first.properties,
            source_id: sourceIds.join(','),
            stroke_widths: widths,
            pack_ids: [...new Set(ordered.map(feature => Number(feature.properties?.pack_id)))],
            fragment_count: Math.max(...ordered.map(feature => Number(feature.properties?.fragment_count || 1))),
            loaded_fragment_count: ordered.length,
            min_zoom: Math.min(...ordered.map(feature => Number(feature.properties?.min_zoom ?? 99))),
          },
          geometry: parts.length === 1 ? { type: 'LineString', coordinates: parts[0] } : { type: 'MultiLineString', coordinates: parts },
        };
      }
      prepareHydroFeature(aggregate);
      state.hydroFeatureCache.set(logicalId, aggregate);
      for (const fragment of ordered) state.hydroFeatureByFid.set(Number(fragment.properties?.__fid), aggregate);
      const sourceIds = new Set(String(aggregate.properties?.source_id || '').split(',').filter(Boolean));
      const legacyHidden = Object.keys(state.physicalSettings.hiddenHydroIds || {}).find(id => (
        state.physicalSettings.hiddenHydroIds[id] === true && id !== logicalId && sourceIds.has(String(id).split(':').pop())
      ));
      if (legacyHidden) state.physicalSettings.hiddenHydroIds[logicalId] = true;
      return aggregate;
    }

    function registerHydroFragments(features) {
      const logicalIds = new Set();
      for (const fragment of features) {
        const logicalId = String(fragment.properties?.pandolab_id || fragment.id);
        if (!state.hydroFragmentsByLogicalId.has(logicalId)) state.hydroFragmentsByLogicalId.set(logicalId, new Map());
        state.hydroFragmentsByLogicalId.get(logicalId).set(Number(fragment.properties?.__fid), fragment);
        logicalIds.add(logicalId);
      }
      for (const logicalId of logicalIds) aggregateHydroLogicalFeature(logicalId);
      if (logicalIds.size) hydroVisibilityDirty = true;
    }

    function unregisterHydroFragments(features) {
      const logicalIds = new Set();
      for (const fragment of features || []) {
        const logicalId = String(fragment.properties?.pandolab_id || fragment.id);
        state.hydroFragmentsByLogicalId.get(logicalId)?.delete(Number(fragment.properties?.__fid));
        state.hydroFeatureByFid.delete(Number(fragment.properties?.__fid));
        logicalIds.add(logicalId);
      }
      for (const logicalId of logicalIds) aggregateHydroLogicalFeature(logicalId);
      if (logicalIds.size) hydroVisibilityDirty = true;
    }

    function hydroViewSnapshot(viewState = getRenderViewState()) {
      const projection = viewState?.projection || state.projection;
      const projectionState = projection === 'globe' ? globeProjection : flatProjection;
      return { projection, threshold: hydroVisibilityThreshold(), width: Number(viewState?.size?.width || state.size.width),
        height: Number(viewState?.size?.height || state.size.height), scale: Number(viewState?.scale || projectionState.scale()),
        flatCenter: viewState?.projectionCenter || viewState?.flatCenter || state.view.flatCenter, rotation: viewState?.rotation || state.view.globeRotation };
    }
    function requestHydroView(viewState) { return hydroPreparation.requestView(hydroViewSnapshot(viewState)); }
    function setHydroManifest(manifest, sourceUrl) { return hydroPreparation.setManifest(manifest, sourceUrl); }
    function loadHydroLogicalFeature(fid) { return hydroPreparation.loadFeature(fid).then(feature => feature ? prepareHydroFeature(feature) : null); }
    function queryHydroLogicalFeatures(bounds, options) { return hydroPreparation.queryFeatures(bounds, options); }
    function retryHydroCache() { return hydroPreparation.retry(); }

    function setHydroInteractionActive(active) {
      interactionActive = active === true;
      hydroPreparation.setInteraction(interactionActive);
      if (!interactionActive) {
        terrainPreparation.scheduleUpload();
      }
    }

    function setRenderQuality(nextProfile = {}) {
      const previousRevision = Number(renderQuality.revision || 0);
      const previousTier = renderQuality.tier;
      const previousPhase = renderQuality.phase;
      const previousDprCap = Number(renderQuality.dprCap || Infinity);
      renderQuality = Object.freeze({ ...DEFAULT_RENDER_QUALITY, ...nextProfile });
      if (previousRevision !== Number(renderQuality.revision || 0) || previousTier !== renderQuality.tier) {
        renderQualityChangeCount += 1;
      }
      const overlayBudget = Math.max(8 * 1024 * 1024, Number(renderQuality.overlayGpuBudgetBytes) || DEFAULT_RENDER_QUALITY.overlayGpuBudgetBytes);
      polygonOverlayPass.setByteBudget(Math.floor(overlayBudget * 0.62));
      strokeRenderer.setByteBudget(Math.floor(overlayBudget * 0.38));
      if (previousDprCap !== Number(renderQuality.dprCap || Infinity)) {
        effectivePixelRatio = 0;
        sceneColorCache.invalidate('quality-dpr');
        queueMapResize?.('adaptive-render-quality');
      }
      // Adaptive quality only controls cadence, upload budgets and DPR. Mesh
      // resolution is gated by canonical readiness and never changes during
      // interaction.
      renderQuality = Object.freeze({ ...renderQuality, countryMeshQuality: 'canonical', terrainResolutionScale: 1 });
      if (terrainManifest?.representation === TERRAIN_DEM_FORMAT
          && (previousPhase !== renderQuality.phase || previousTier !== renderQuality.tier)) {
        if (renderQuality.phase === 'interaction') {
          demSettleStartedAt = 0; demShadeBlend = 0;
          if (demShadeTimer !== null) { clearTimeout(demShadeTimer); demShadeTimer = null; }
        }
        invalidatePhysicalScene('terrain-shade-quality');
      }
      return renderQuality;
    }

    function invalidateHydroVisibility() {
      hydroVisibilityDirty = true;
      physicalStyleStateRevision += 1;
      queueHydroRender('hydro-visibility');
    }

    function invalidatePhysicalStyle(reason = 'physical-style') {
      physicalStyleStateRevision += 1;
      invalidatePhysicalScene(reason);
      return true;
    }

    function drawTerrainTile({ spec, sourceSpec, texture, grid, gutter }, pass = 'land') {
      const dem = terrainManifest?.representation === TERRAIN_DEM_FORMAT;
      const program = dem ? terrainDemProgram : terrainProgram;
      if (!program) {
        if (dem) void fallbackTerrainToRaster('DEM 셰이더 정밀도가 부족합니다.');
        return false;
      }
      const frameContext = activeFrameContext || lastVisualFrame;
      if (!frameContext) return false;
      gl.useProgram(program);
      const gridLocation = glVersion === 2 ? 0 : cachedAttributeLocation(program, 'aGrid');
      gl.bindBuffer(gl.ARRAY_BUFFER, grid.vertexBuffer);
      gl.enableVertexAttribArray(gridLocation);
      gl.vertexAttribPointer(gridLocation, 2, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, grid.indexBuffer);
      gl.activeTexture(gl.TEXTURE1);
      gl.bindTexture(gl.TEXTURE_2D, texture);
      gl.uniform1i(cachedUniformLocation(program, 'uTerrain'), 1);
      if (dem) {
        gl.activeTexture(gl.TEXTURE2);
        gl.bindTexture(gl.TEXTURE_2D, terrainPreparation.tintTexture());
        gl.uniform1i(cachedUniformLocation(program, 'uTint'), 2);
      }
      const [west, north, east, south] = spec.bounds;
      gl.uniform4f(cachedUniformLocation(program, 'uGeoBounds'), west, north, east, south);
      const [sourceWest, sourceNorth, sourceEast, sourceSouth] = sourceSpec.bounds;
      const sourceWidth = sourceSpec.pixelWidth + gutter * 2;
      const sourceHeight = sourceSpec.pixelHeight + gutter * 2;
      const u0 = (gutter + (west - sourceWest) / (sourceEast - sourceWest) * sourceSpec.pixelWidth) / sourceWidth;
      const v0 = (gutter + (sourceNorth - north) / (sourceNorth - sourceSouth) * sourceSpec.pixelHeight) / sourceHeight;
      const u1 = (gutter + (east - sourceWest) / (sourceEast - sourceWest) * sourceSpec.pixelWidth) / sourceWidth;
      const v1 = (gutter + (sourceNorth - south) / (sourceNorth - sourceSouth) * sourceSpec.pixelHeight) / sourceHeight;
      gl.uniform4f(cachedUniformLocation(program, 'uUvBounds'), u0, v0, u1, v1);
      gl.uniform1f(cachedUniformLocation(program, 'uPhysicalStyle'),
        state.physicalSettings.terrainStyle === 'physical' && (!dem || terrainPreparation.tintTexture()) ? 1 : 0);
      gl.uniform1f(cachedUniformLocation(program, 'uDarkTheme'), getSystemTheme() === 'dark' ? 1 : 0);
      if (dem) {
        const sourceLevel = terrainManifest.levels[Number(sourceSpec.level)];
        gl.uniform2f(cachedUniformLocation(program, 'uTextureSize'), sourceWidth, sourceHeight);
        gl.uniform2f(cachedUniformLocation(program, 'uLevelSize'), sourceLevel.width, sourceLevel.height);
        gl.uniform1f(cachedUniformLocation(program, 'uLandPass'), pass === 'land' ? 1 : 0);
        gl.uniform1f(cachedUniformLocation(program, 'uShadeBlend'), demShadeBlend);
      }
      for (const offset of frameContext.worldOffsets) {
        setViewUniforms(program, offset, frameContext);
        gl.drawElements(gl.TRIANGLES, grid.indexCount, gl.UNSIGNED_INT, 0);
      }
      gl.disableVertexAttribArray(gridLocation);
      return true;
    }

    function overlayResourceReady(key) {
      if ([...(renderScene?.polygons || []), ...(renderScene?.strokes || [])].some(packet => packet.key === key)) {
        sceneColorCache.invalidate('overlay-resource-ready'); invalidateGpuFrame('overlay-resource-ready');
      } else scheduleGpuInteractionFrame?.('interaction-resource-ready');
    }
    function buildCountryStrokeResource(sourceMesh, countryIds, sourceName, revision) {
      if (!sourceMesh?.lineIndices?.length || !sourceMesh?.positions?.length || !sourceMesh?.countryIndices?.length) return null;
      const cache = countryStrokePacketCache[sourceName];
      if (cache.mesh === sourceMesh && cache.countryIds === countryIds && cache.revision === revision) return cache.resource;
      const preparedGeometry = sourceMesh.preparedStroke;
      if (!preparedGeometry) return null;
      const startsEnds = preparedGeometry.startsEnds;
      const ownerRanges = preparedGeometry.inputOwnerRanges;
      const ownerIds = Object.entries(ownerRanges)
        .filter(([, range]) => Number(range?.count || 0) > 0)
        .map(([id]) => id);
      const segmentOffset = startsEnds.length / 4;
      const packet = Object.freeze({
        key: `country-boundary:${sourceName}`,
        preparedGeometry,
        geometryRevision: String(revision),
        startsEnds,
        segmentCount: segmentOffset,
        ownerRanges: Object.freeze(Object.fromEntries(Object.entries(ownerRanges).map(([id, range]) => [id, Object.freeze(range)]))),
      });
      const resource = Object.freeze({ packet, ownerIds: Object.freeze(ownerIds) });
      cache.mesh = sourceMesh;
      cache.countryIds = countryIds;
      cache.revision = revision;
      cache.resource = resource;
      return resource;
    }

    function countryStrokeMeshRevision(sourceMesh) {
      if (!sourceMesh || typeof sourceMesh !== 'object') return 0;
      let revision = countryStrokeMeshRevisions.get(sourceMesh);
      if (!revision) {
        revision = ++countryStrokeMeshRevisionSequence;
        countryStrokeMeshRevisions.set(sourceMesh, revision);
      }
      return revision;
    }

    function currentCountryStrokeResources() {
      const canonicalEntry = meshVariants.get('canonical');
      const canonicalMesh = canonicalEntry?.mesh || mesh;
      const canonicalCountryIds = canonicalEntry?.countryIds || meshCountryIds;
      const activeRevision = `${activeMeshQuality}:${countryStrokeMeshRevision(mesh)}:${mesh?.lineIndices?.length || 0}`;
      const canonicalRevision = `canonical:${countryStrokeMeshRevision(canonicalMesh)}:${canonicalMesh?.lineIndices?.length || 0}`;
      const revisionOverride = `override:${countryStrokeMeshRevision(overrideMesh)}:${overrideMesh?.lineIndices?.length || 0}`;
      const selectionBase = buildCountryStrokeResource(canonicalMesh, canonicalCountryIds, 'canonical', canonicalRevision);
      const base = mesh === canonicalMesh && meshCountryIds === canonicalCountryIds
        ? selectionBase
        : buildCountryStrokeResource(mesh, meshCountryIds, 'preview', activeRevision);
      return Object.freeze({
        base,
        selectionBase,
        override: buildCountryStrokeResource(overrideMesh, meshCountryIds, 'override', revisionOverride),
      });
    }

    function prewarmCountryStrokeResources() {
      if (!strokeRenderer.isAvailable?.() || !mesh) return false;
      if (meshQuality === 'canonical' && !canonicalMeshReady) return false;
      const packets = new Map();
      for (const resource of Object.values(currentCountryStrokeResources())) {
        if (resource?.packet) packets.set(resource.packet.key, resource.packet);
      }
      for (const packet of packets.values()) strokeRenderer.ensureResource(packet);
      return packets.size > 0;
    }

    function drawCountryBoundaryStrokes(dynamicResources, baseBoundaryDraw, overrideBoundaryDraw) {
      if (!state.layerVisibility.countries) return { succeeded: true, renderedKeys: [], missingKeys: [] };
      drawProgram(lineProgram, lineVao, lineIndexBuffer, mesh.lineIndices.length, gl.LINES, null, paletteTexture, null, null, baseBoundaryDraw.ranges);
      if (overrideMesh?.lineIndices?.length) {
        drawProgram(lineProgram, overrideLineVao, overrideLineIndexBuffer, overrideMesh.lineIndices.length, gl.LINES, dynamicResources, overridePaletteTexture, null, null, overrideBoundaryDraw.ranges);
      }
      return {
        succeeded: true,
        renderedKeys: [],
        missingKeys: [],
      };
    }

    let preparedBaseScene = null;
    function prepareBaseScene() {
      prepareTerrain(activeFrameContext);
      preparedBaseScene = prepareGpuBaseScene({ mesh, overrideMesh, frame: activeFrameContext,
        scene: renderScene, budgetBytes: renderQuality.uploadBudgetBytes }, { polygonOverlayPass, strokeRenderer });
      const { baseTriangleDraw, baseBoundaryDraw, overrideTriangleDraw, overrideBoundaryDraw,
        overlayUploadBytes, deferredOverlayKeys, overrunCount } = preparedBaseScene;
      performanceMetrics.countryBaseIndexCount = baseTriangleDraw.indexCount + overrideTriangleDraw.indexCount;
      performanceMetrics.countryBaseFullIndexCount = baseTriangleDraw.fullIndexCount + overrideTriangleDraw.fullIndexCount;
      performanceMetrics.countryBaseRangeCount = baseTriangleDraw.ranges.length + overrideTriangleDraw.ranges.length;
      performanceMetrics.countryBoundaryIndexCount = baseBoundaryDraw.indexCount + overrideBoundaryDraw.indexCount;
      performanceMetrics.countryBoundaryFullIndexCount = baseBoundaryDraw.fullIndexCount + overrideBoundaryDraw.fullIndexCount;
      performanceMetrics.countryVisibleCount = baseTriangleDraw.visibleCountryCount + overrideTriangleDraw.visibleCountryCount;
      if (baseTriangleDraw.fallback || baseBoundaryDraw.fallback || (overrideMesh && (overrideTriangleDraw.fallback || overrideBoundaryDraw.fallback))) performanceMetrics.countryCullingFallbackCount += 1;
      performanceMetrics.overlayUploadBytes += overlayUploadBytes;
      performanceMetrics.lastOverlayUploadBytes = overlayUploadBytes;
      performanceMetrics.overlayDeferredItemCount = deferredOverlayKeys.size;
      performanceMetrics.uploadBudgetOverrunCount += overrunCount;
      if (deferredOverlayKeys.size) invalidateGpuFrame('overlay-upload-budget');
    }

    function drawBaseSceneContent() {
      if (!gl || !mesh || !activeFrameContext || projectRenderBlocked || !preparedBaseScene) return false;
      lastBaseSceneResult = drawGpuBaseScene({ gl, frame: activeFrameContext, width: pixelWidth, height: pixelHeight,
        terrainVisible: state.physicalSettings.terrainVisible, terrainStyle: state.physicalSettings.terrainStyle,
        terrainRepresentation: terrainManifest?.representation,
        countriesVisible: state.layerVisibility.countries,
        countries: { mesh, overrideMesh, dynamicResources: overrideMesh ? { positionBuffer: overridePositionBuffer, countryBuffer: overrideCountryBuffer } : null, landMaskProgram, fillProgram, fillVao, fillIndexBuffer, overrideFillVao, overrideFillIndexBuffer, paletteTexture, overridePaletteTexture },
        prepared: preparedBaseScene,
      }, { drawProgram, renderTerrain, drawHydro, drawCountryBoundaryStrokes, polygonOverlayPass, strokeRenderer });
      performanceMetrics.baseSceneDrawCount += 1;
      sceneCacheFullDrawCount += 1;
      return lastBaseSceneResult;
    }

    function drawCountryInteractionFills({ base, override }) {
      resetGpuNormalBlend(gl);
      const dynamicResources = overrideMesh ? { positionBuffer: overridePositionBuffer, countryBuffer: overrideCountryBuffer } : null;
      for (const ranges of [base, override]) {
        performanceMetrics.countryInteractionIndexCount += ranges.reduce((sum, range) => sum + Number(range.count || 0), 0);
        performanceMetrics.countryInteractionRangeCount += ranges.length;
      }
      if (base.length) drawProgram(fillProgram, fillVao, fillIndexBuffer, mesh.triangleIndices.length, gl.TRIANGLES, null, emphasisPaletteTexture, null, null, base);
      if (override.length) drawProgram(fillProgram, overrideFillVao, overrideFillIndexBuffer, overrideMesh.triangleIndices.length, gl.TRIANGLES, dynamicResources, overrideEmphasisPaletteTexture, null, null, override);
    }

    function drawCountryBoundaryMask() {
      if (!preparedBaseScene || !state.layerVisibility.countries) return;
      const dynamicResources = overrideMesh ? { positionBuffer: overridePositionBuffer, countryBuffer: overrideCountryBuffer } : null;
      drawCountryBoundaryStrokes(dynamicResources, preparedBaseScene.baseBoundaryDraw, preparedBaseScene.overrideBoundaryDraw);
    }

    function sceneViewSignature(viewState = activeRenderViewState || getRenderViewState()) {
      return [
        renderDeviceContextRevision,
        viewState?.projection,
        viewState?.translate?.join(','),
        viewState?.scale,
        viewState?.rotation?.join(','),
        viewState?.projectionCenter?.join(','),
        pixelWidth,
        pixelHeight,
      ].join(':');
    }

    // The scene cache stores a flat projection in screen pixels.  During a
    // small flat pan/zoom we can reproject that texture with one affine quad
    // instead of traversing every canonical triangle again. Globe frames and
    // large moves fall back to the exact scene rebuild because their mapping
    // is non-linear or would expose pixels outside the cached viewport.
    function flatSceneReprojection(frameContext) {
      const previous = lastSceneFrameContext;
      if (!previous || previous.mode !== 1 || frameContext?.mode !== 1) return null;
      if (previous.viewport?.[0] !== frameContext.viewport?.[0]
        || previous.viewport?.[1] !== frameContext.viewport?.[1]) return null;
      const oldScale = Number(previous.scale || 0);
      const nextScale = Number(frameContext.scale || 0);
      if (!(oldScale > 0) || !(nextScale > 0)) return null;
      const scale = nextScale / oldScale;
      if (scale < 0.85 || scale > 1.15) return null;
      const oldCenter = previous.flatCenter || [0, 0];
      const nextCenter = frameContext.flatCenter || [0, 0];
      const oldTranslate = previous.translate || [0, 0];
      const nextTranslate = frameContext.translate || [0, 0];
      const offsetX = nextTranslate[0] - scale * oldTranslate[0]
        + nextScale * (oldCenter[0] - nextCenter[0]);
      const offsetYScreen = nextTranslate[1] - scale * oldTranslate[1]
        + nextScale * (nextCenter[1] - oldCenter[1]);
      const width = Number(frameContext.viewport?.[0] || 0);
      const height = Number(frameContext.viewport?.[1] || 0);
      if (!(width > 0 && height > 0)
        || Math.abs(offsetX) > width * 0.2
        || Math.abs(offsetYScreen) > height * 0.2) return null;
      return {
        sourceViewport: previous.viewport,
        scale: [scale, scale],
        // Composite coordinates use a bottom-left origin while D3 view
        // translations use a top-left origin.
        offset: [offsetX, height - scale * height - offsetYScreen],
      };
    }

    function renderWebGl(visualFrame, { interactionOnly = false } = {}) {
      if (!gl || !mesh) return null;
      if (!isMapVisualFrame(visualFrame)) throw new TypeError('GPU render requires a MapVisualFrame.');
      if (resizePending) resize();
      activeFrameContext = visualFrame;
      lastVisualFrame = visualFrame;
      performanceMetrics.visualFrameConsumeCount += 1;
      const viewState = visualFrame.viewState;
      const started = performance.now();
      let sceneCacheHit = false;
      let baseResult = null;
      let preservedCountryPatchPromoted = false;
      sceneCacheFallbackFrame = false;
      const viewSignature = sceneViewSignature(viewState);
      const exactSceneCacheHit = sceneColorCache.canComposite?.(viewSignature, projectGeneration) || false;
      const reproject = !exactSceneCacheHit && !sceneColorCache.isDirty?.()
        && sceneColorCache.hasActiveProject?.(projectGeneration)
        ? flatSceneReprojection(activeFrameContext)
        : null;
      const needsBaseScene = !exactSceneCacheHit && !reproject;
      let preparedThisFrame = false;
      const prepareForSubmission = () => {
        if (preparedThisFrame) return;
        prepareBaseScene();
        preparedThisFrame = true;
      };
      // Preparation completes before any base/interaction submission or fallback redraw.
      flushPaletteUpdates();
      updateHydroVisibility();
      prepareGpuInteraction(renderInteractionState, { polygonOverlayPass, strokeRenderer, selectionPass });
      // Even a cache hit may require a direct redraw when compositing fails.
      if (needsBaseScene || !preparedBaseScene) prepareForSubmission();
      if (needsBaseScene) {
        if (interactionOnly) sceneCacheSelectionOnlyBaseDrawCount += 1;
        if (sceneColorCache.beginScene(pixelWidth, pixelHeight, viewSignature, projectGeneration)) {
          const previousBaseResult = lastBaseSceneResult;
          baseResult = drawBaseSceneContent();
          if (baseResult !== false) {
            const holdPreservedScene = shouldHoldCountryPatchScene(viewSignature);
            if (holdPreservedScene && sceneColorCache.composite(pixelWidth, pixelHeight, { clearTarget: true })) {
              const presentation = countryPatchPresentation;
              presentation.heldFrameCount += 1;
              performanceMetrics.countryPatchSceneHoldCount += 1;
              lastBaseSceneResult = presentation.previousBaseResult || previousBaseResult;
              baseResult = lastBaseSceneResult;
            } else {
              // finishScene() only promotes the staging texture and restores
              // the default framebuffer. Present that texture in this frame so
              // the country patch never appears one settled frame late.
              const promoted = sceneColorCache.finishScene(null, viewSignature, projectGeneration);
              lastBaseSceneResult = baseResult;
              lastSceneFrameContext = activeFrameContext;
              if (!promoted || !sceneColorCache.composite(pixelWidth, pixelHeight, { clearTarget: true })) {
                recordSceneCacheFallback();
                gl.bindFramebuffer(gl.FRAMEBUFFER, null);
                baseResult = drawBaseSceneContent();
              }
              preservedCountryPatchPromoted = countryPatchPresentation?.phase === 'staging';
            }
          }
        } else {
          recordSceneCacheFallback();
          // A failed staging allocation must not clear the only visible
          // scene. Keep the last scene when it belongs to this exact view;
          // direct redraw is only safe before the first scene exists.
          if (countryPatchPresentation?.phase === 'staging') {
            gl.bindFramebuffer(gl.FRAMEBUFFER, null);
            baseResult = drawBaseSceneContent();
            preservedCountryPatchPromoted = baseResult !== false;
          } else if (sceneColorCache.hasActiveFor?.(viewSignature, projectGeneration)) {
            baseResult = lastBaseSceneResult;
          } else {
            gl.bindFramebuffer(gl.FRAMEBUFFER, null);
            baseResult = drawBaseSceneContent();
          }
        }
      } else {
        sceneCacheHit = true;
      }
      if (preservedCountryPatchPromoted) completePreservedCountryPatchPresentation();
      if (exactSceneCacheHit || reproject) {
        // This canvas is owned by PandoLab. Clear before compositing so pixels
        // removed from the active scene (for example an edited border) cannot
        // survive in the default framebuffer as an afterimage.
        if (!sceneColorCache.composite(pixelWidth, pixelHeight, { clearTarget: true, reproject })) {
          recordSceneCacheFallback();
          // A failed composite does not make a same-view active scene stale.
          // Preserve the already displayed frame instead of clearing it and
          // exposing a partially redrawn/transparent framebuffer.
          if (sceneColorCache.hasActiveFor?.(viewSignature, projectGeneration)) {
            baseResult = lastBaseSceneResult;
          } else {
            // Do not expose a transparent failed composite. If there is no
            // usable current-view cache, draw the scene directly as the only
            // safe first-frame fallback.
            prepareForSubmission();
            gl.bindFramebuffer(gl.FRAMEBUFFER, null);
            baseResult = drawBaseSceneContent();
          }
        }
        if (reproject) sceneCacheReprojectCount += 1;
        if (!baseResult) baseResult = lastBaseSceneResult;
      } else if (!baseResult) {
        recordSceneCacheFallback();
        if (sceneColorCache.hasActiveFor?.(viewSignature, projectGeneration)) {
          baseResult = lastBaseSceneResult;
        } else {
          prepareForSubmission();
          gl.bindFramebuffer(gl.FRAMEBUFFER, null);
          baseResult = drawBaseSceneContent();
        }
      }
      const preparedInteraction = prepareGpuInteractionPlan({ interaction: renderInteractionState, emphasis: countryEmphasis,
        mesh, overrideMesh, overrideIds: countryOverrideIds, countriesVisible: state.layerVisibility.countries,
        blocked: projectRenderBlocked, isPending: id => geometryRevisionTracker.isPending(id), isVisible: isCountryVisibleById }, polygonOverlayPass);
      const fillTarget = gl.getParameter(gl.FRAMEBUFFER_BINDING);
      const fillTargetReady = interactionFillCache.beginScene(pixelWidth, pixelHeight, '', projectGeneration);
      performanceMetrics.countryInteractionIndexCount = 0;
      performanceMetrics.countryInteractionRangeCount = 0;
      performanceMetrics.countryInteractionFullIndexCount = Number(mesh?.triangleIndices?.length || 0) + Number(overrideMesh?.triangleIndices?.length || 0);
      const interactionResult = drawGpuInteractionPass({ gl, frame: activeFrameContext, viewState,
        viewport: { size: { width: cssWidth, height: cssHeight }, dpr: effectivePixelRatio, pixelWidth, pixelHeight },
        fillTarget, fillTargetReady, prepared: preparedInteraction },
      { fillCache: interactionFillCache, strokeCache: interactionStrokeCache, polygonOverlayPass, strokeRenderer, selectionPass, drawHydro, drawCountryBoundaryMask, drawCountryRanges: drawCountryInteractionFills });
      lastInteractionFillResult = interactionResult.genericFillResult;
      lastSelectionRenderResult = interactionResult.selection;
      sceneCacheInteractionDrawCount += 1;
      performanceMetrics.interactionFrameCount += 1;
      gl.flush();
      displayedRenderRevision = currentRenderRevision;
      markPreviewFramePresented();
      frameTimes.push(performance.now() - started);
      if (frameTimes.length > 240) frameTimes.shift();
      activeFrameContext = null;
      publishLightweightMetrics();
      return {
        succeeded: !webglContextLost,
        frameId: visualFrame.frameId,
        viewRevision: visualFrame.viewRevision,
        projectionRevision: visualFrame.projectionRevision,
        sceneCacheHit,
        baseResult,
        interactionResult,
        selection: interactionResult.selection,
      };
    }

    function renderCanvasHydro(canvasPath, theme, target = ctx2d, reserve = false) {
      const builtIn = [];
      for (const packId of hydroPreparation.activeIds()) builtIn.push(...(hydroPreparation.pack(packId)?.features || []));
      const features = [...builtIn, ...(state.hydroEdits || [])];
      target.lineCap = 'round';
      target.lineJoin = 'round';
      for (const feature of features) {
        if (!feature?.geometry || !isHydroFeatureVisible(feature)) continue;
        const lake = feature.properties?.category === 'lake';
        if (state.layerVisibility[lake ? 'lakes' : 'rivers'] === false) continue;
        const opacity = lake ? theme.lakeOpacity : theme.riverOpacity;
        if (Number(opacity) <= 0) continue;
        const color = feature.properties?.editorColor || hydroDisplayColor(lake ? 'lake' : 'river');
        if (lake) {
          target.beginPath(); canvasPath(feature);
          target.globalAlpha = reserve ? 1 : opacity; target.fillStyle = color; target.fill();
          if (theme.lakeBoundaryVisible !== false) {
            target.beginPath(); canvasPath(countryOutlineFeature(feature));
            target.strokeStyle = color; target.lineWidth = Math.max(0.5, Number(theme.lakeBoundaryWidth) || 1); target.stroke();
          }
          continue;
        }
        const profiles = feature.properties?.stroke_widths || [];
        const fallback = Math.max(0.55, Math.min(2.6, Number(feature.properties?.stroke_width || 0.8)));
        target.globalAlpha = reserve ? 1 : opacity; target.strokeStyle = color;
        for (const [partIndex, part] of hydroLineParts(feature.geometry).entries()) {
          const widths = profiles[partIndex] || [];
          for (let index = 0; index < part.length - 1; index += 1) {
            target.beginPath();
            canvasPath({ type: 'LineString', coordinates: [part[index], part[index + 1]] });
            const start = Number(widths[index] ?? fallback);
            const end = Number(widths[index + 1] ?? start);
            target.lineWidth = (start + end) / 2 * Math.max(0.5, Number(theme.riverWidth) || 1);
            target.stroke();
          }
        }
      }
    }

    let canvasFillSubstrate = null;
    function renderCanvasFallback() {
      if (!ctx2d || !canvas) return;
      if (resizePending) resize();
      const dpr = pixelWidth / cssWidth;
      ctx2d.setTransform(1, 0, 0, 1, 0, 0);
      ctx2d.clearRect(0, 0, pixelWidth, pixelHeight);
      ctx2d.setTransform(dpr, 0, 0, dpr, 0, 0);
      const canvasPath = d3.geo.path().projection(activeProjection()).context(ctx2d);
      const substrate = canvasFillSubstrate ||= document.createElement('canvas');
      if (substrate.width !== pixelWidth || substrate.height !== pixelHeight) {
        substrate.width = pixelWidth;
        substrate.height = pixelHeight;
      }
      substrate.getContext('2d').clearRect(0, 0, pixelWidth, pixelHeight);
      substrate.getContext('2d').drawImage(canvas, 0, 0);
      const theme = mapTheme();
      ctx2d.lineJoin = 'round';
      ctx2d.lineWidth = 0.72 * Math.max(0.5, Number(theme.borderWidth) || 1);
      if (state.layerVisibility.countries) {
        for (const feature of state.countriesData?.features || []) {
          const id = String(feature?.id || '');
          if (!isLayerItemVisible('countries', id)) continue;
          ctx2d.beginPath();
          canvasPath(feature);
          ctx2d.globalAlpha = theme.fillAlpha;
          ctx2d.fillStyle = countryColor(feature);
          ctx2d.fill();
        }
      }
      globalThis.PandoLabCanvasSceneComposition.drawFills(ctx2d, canvasPath, canvasScenePolygons(), substrate, dpr);
      const emphasisEntries = [];
      if (state.layerVisibility.countries) for (const feature of state.countriesData?.features || []) {
        const id = String(feature.id || '');
        const emphasis = countryEmphasisStyle(id);
        if (isLayerItemVisible('countries', id) && emphasis) emphasisEntries.push({ key: `country:${id}`, geometry: feature,
          priority: countryEmphasis.priorities[id] || (countryEmphasis.primaryIds.has(id) ? 4 : countryEmphasis.selectedIds.has(id) ? 3 : 2),
          style: { color: colorHex(emphasis.color), fillAlpha: emphasis.alphaByte / 255 } });
      }
      const packets = new Map((renderScene?.polygons || []).map(packet => [packet.key, packet]));
      for (const item of renderInteractionState.genericFillItems || []) if (packets.has(item.key)) {
        emphasisEntries.push({ ...item, packet: packets.get(item.key) });
      }
      for (const packet of canvasInteractionPolygons()) emphasisEntries.push({ key: packet.key, packet,
        priority: packet.interactionPriority || 5, style: packet.style });
      globalThis.PandoLabCanvasSceneComposition.drawEmphasis(ctx2d, canvasPath, emphasisEntries, dpr, {
        key: [JSON.stringify(getRenderViewState()), physicalStyleStateRevision, hydroPreparation.acceptedRevision, hydroPreparation.editRevision, [...hydroPreparation.activeIds()].join(',')].join(':'),
        draw: mask => renderCanvasHydro(d3.geo.path().projection(activeProjection()).context(mask), theme, mask, true),
      });
      renderCanvasHydro(canvasPath, theme);
      if (state.layerVisibility.countries) for (const feature of state.countriesData?.features || []) {
          const id = String(feature?.id || '');
          if (!isLayerItemVisible('countries', id)) continue;
          ctx2d.beginPath();
          canvasPath(countryOutlineFeature(feature));
          ctx2d.globalAlpha = theme.borderAlpha;
          ctx2d.strokeStyle = theme.border;
          ctx2d.lineWidth = 0.72 * Math.max(0.5, Number(theme.borderWidth) || 1);
          ctx2d.stroke();
        }

      ctx2d.globalAlpha = 1;
      displayedRenderRevision = currentRenderRevision;
      markPreviewFramePresented();
    }

    function canvasInteractionPolygons() {
      return [...(renderInteractionState.previewPackets || []), ...(renderInteractionState.draftPackets || [])]
        .filter(item => item.kind === 'polygon').map(item => item.packet);
    }

    function canvasScenePolygons() {
      return (renderScene?.polygons || []).filter(packet => packet.ringCoordinates).map(packet => ({
        key: packet.key, ringCoordinates: packet.ringCoordinates, ringOffsets: packet.ringOffsets, polygonOffsets: packet.polygonOffsets,
        role: packet.role, territoryDepth: packet.territoryDepth,
        order: packet.order, style: packet.style, blendMode: packet.blendMode,
      }));
    }

    const canvasSentGeometry = new Map();
    function canvasPacketDelta(packets, channel) {
      const next = new Set();
      const result = packets.map(packet => {
        const key = `${channel}:${packet.key}`;
        next.add(key);
        const previous = canvasSentGeometry.get(key);
        canvasSentGeometry.set(key, packet.ringCoordinates);
        if (previous !== packet.ringCoordinates) return packet;
        const { ringCoordinates: _coordinates, ringOffsets: _rings, polygonOffsets: _polygons, ...metadata } = packet;
        return metadata;
      });
      for (const key of canvasSentGeometry.keys()) if (key.startsWith(`${channel}:`) && !next.has(key)) canvasSentGeometry.delete(key);
      return result;
    }

    function canvasWorkerStyleMessage() {
      const colors = {};
      for (const feature of state.countriesData?.features || []) {
        colors[String(feature?.id || '')] = countryColor(feature);
      }
      return {
        type: 'style',
        styleRevision: ++canvasStyleRevision,
        visible: !!state.layerVisibility.countries,
        hiddenCountryIds: Object.keys(state.itemVisibility.countries || {}).filter(id => state.itemVisibility.countries[id] === false),
        colors,
        scenePolygons: canvasPacketDelta(canvasScenePolygons(), 'scene'),
        interactionFillItems: renderInteractionState.genericFillItems || [],
        interactionPolygons: canvasPacketDelta(canvasInteractionPolygons(), 'interaction'),
        countryEmphasis: {
          primaryId: countryEmphasis.primaryId,
          primaryIds: [...countryEmphasis.primaryIds],
          priorities: countryEmphasis.priorities,
          hoverId: countryEmphasis.hoverId,
          selectedIds: [...countryEmphasis.selectedIds],
          primaryColor: interactionStyle.selection.color,
          secondaryColor: interactionStyle.selection.color,
          hoverColor: interactionStyle.hover.color,
          primaryAlpha: interactionStyle.selection.primary.fillAlpha,
          secondaryAlpha: interactionStyle.selection.secondary.fillAlpha,
          hoverAlpha: interactionStyle.hover.fillAlpha,
          boundaryEnabled: false,
        },
        interactionStyle,
        theme: mapTheme(),
        darkTheme: getSystemTheme() === 'dark',
      };
    }

    function canvasWorkerPhysicalStyleMessage() {
      return {
        type: 'physical-style',
        physicalStyleRevision: ++canvasPhysicalStyleRevision,
        riversVisible: !!state.layerVisibility.rivers,
        lakesVisible: !!state.layerVisibility.lakes,
        physicalSettings: deepClone(state.physicalSettings),
        theme: mapTheme(),
        darkTheme: getSystemTheme() === 'dark',
        dataReadiness: state.dataReadiness,
        terrainFetchConcurrency: isMobile() ? 2 : 4,
      };
    }

    function canvasWorkerViewMessage(revision = currentRenderRevision, visualFrame = null) {
      const frame = isMapVisualFrame(visualFrame) ? visualFrame : lastVisualFrame;
      const view = frame?.viewState || visualFrame || getRenderViewState();
      const workerView = {
        ...deepClone(state.view),
        ...deepClone(view),
        flatCenter: view.projectionCenter || state.view.flatCenter,
        globeRotation: view.rotation || state.view.globeRotation,
        flatZoom: view.zoom ?? state.view.flatZoom,
        globeZoom: view.zoom ?? state.view.globeZoom,
      };
      return {
        type: 'view',
        renderProjection: { translate: frame?.cssTranslate || view.translate, scale: frame?.cssScale || view.scale,
          safeInset: frame?.safeInset || null, flatProjectionKind: 'equirectangular' },
        width: Math.max(1, Number(view.size?.width || state.size.width)),
        height: Math.max(1, Number(view.size?.height || state.size.height)),
        dpr: Number(view.dpr || resolveRenderPixelRatio()),
        terrainDpr: Math.min(isMobile() ? 2 : 3, Math.max(1, Number(window.devicePixelRatio || 1))),
        projection: view.projection || state.projection,
        view: workerView,
        revision: Number(revision || 0),
        viewRevision: Number(view.revision || revision || 0),
        frameId: Number(frame?.frameId || revision || 0),
        projectionRevision: Number(frame?.projectionRevision || 0),
        projectGeneration: Number(frame?.projectGeneration || projectGeneration),
        geometryRevision: geometryRevisionTracker.committedRevision(),
      };
    }

    function canvasWorkerInitMessage() {
      canvasSentGeometry.clear();
      const message = {
        ...canvasWorkerViewMessage(currentRenderRevision),
        ...canvasWorkerStyleMessage(),
        ...canvasWorkerPhysicalStyleMessage(),
        geometryRevision: geometryRevisionTracker.committedRevision(),
        terrainManifestUrl: (() => {
          return String(TERRAIN_RASTER_MANIFEST_URL);
        })(),
      };
      message.type = 'init';
      return message;
    }

    function estimateCanvasMessageBytes(message) {
      if (message?.type === 'view') return 192;
      try { return new Blob([JSON.stringify(message)]).size; }
      catch (_) { return 0; }
    }

    function recordCanvasWorkerMessage(message) {
      performanceMetrics.canvasWorkerMessageCount += 1;
      performanceMetrics.canvasWorkerMessageBytes += estimateCanvasMessageBytes(message);
      performanceMetrics.canvasWorkerMessagesByType[message.type] = Number(performanceMetrics.canvasWorkerMessagesByType[message.type] || 0) + 1;
      if (message.type === 'view') performanceMetrics.canvasWorkerViewMessageCount += 1;
      else performanceMetrics.canvasWorkerStateMessageCount += 1;
    }

    function postCanvasWorkerMessage(message) { return canvasWorker?.postMessage(message) || false; }

    function syncCanvasWorkerState() {
      if (!canvasWorker || !canvasWorker.ready) return;
      const styleSignature = [countryPaletteRevision, countryEmphasisRevision, state.layerVisibility.countries, getSystemTheme(),
        renderScene?.revisions?.geometry, renderScene?.revisions?.style, renderScene?.revisions?.overlayOrder].join(':');
      if (styleSignature !== canvasLastStyleSignature) {
        canvasLastStyleSignature = styleSignature;
        postCanvasWorkerMessage(canvasWorkerStyleMessage());
      }
      const theme = mapTheme();
      const physicalSignature = [physicalStyleStateRevision, state.layerVisibility.rivers, state.layerVisibility.lakes,
        theme.riverOpacity, theme.lakeOpacity, theme.lakeBoundaryVisible, theme.ocean,
        state.physicalSettings.terrainVisible, state.physicalSettings.terrainStyle,
        state.dataReadiness].join(':');
      if (physicalSignature !== canvasLastPhysicalStyleSignature) {
        canvasLastPhysicalStyleSignature = physicalSignature;
        postCanvasWorkerMessage(canvasWorkerPhysicalStyleMessage());
      }
    }

    function renderCanvasWorker(revision = currentRenderRevision, visualFrame = null) {
      if (!canvasWorker) return;
      if (resizePending) resize();
      syncCanvasWorkerState();
      canvasWorker.queueFrame(canvasWorkerViewMessage(revision, visualFrame));
    }

    function renderLatestVisualFrame() {
      return lastVisualFrame ? renderFrame(lastVisualFrame) : null;
    }

    function renderFrame(visualFrame, { interactionOnly = false } = {}) {
      if (disposed) return null;
      if (canvasWorkerNeedsRestart && !projectRenderBlocked) activateCanvasFallback(fallbackReason);
      if (!isMapVisualFrame(visualFrame)) throw new TypeError('renderFrame() requires a MapVisualFrame.');
      currentRenderRevision = Math.max(currentRenderRevision, Number(visualFrame.viewRevision || 0));
      lastVisualFrame = visualFrame;
      activeRenderViewState = visualFrame.viewState;
      requestHydroView(activeRenderViewState);
      let result = null;
      if (isWebGlRenderer()) {
        result = renderWebGl(visualFrame, { interactionOnly });
      }
      else if (rendererMode === 'canvas-worker') {
        renderCanvasWorker(currentRenderRevision, visualFrame);
        result = { deferred: true, frameId: visualFrame.frameId, viewRevision: visualFrame.viewRevision };
      }
      else if (rendererMode === 'canvas2d') renderCanvasFallback();
      publishLightweightMetrics();
      return result;
    }

    function renderInteraction(visualFrame = lastVisualFrame) {
      if (!isMapVisualFrame(visualFrame)) return null;
      currentRenderRevision = Math.max(currentRenderRevision, Number(visualFrame.viewRevision || 0));
      activeRenderViewState = visualFrame.viewState;
      if (!isWebGlRenderer()) return renderFrame(visualFrame);
      performanceMetrics.selectionOnlyFrameCount += 1;
      return renderWebGl(visualFrame, { interactionOnly: true });
    }

    function prioritizeLatest() {
      if (rendererMode !== 'canvas-worker' || !canvasWorker) return;
      syncCanvasWorkerState();
      canvasWorker.queueFrame(canvasWorkerViewMessage(currentRenderRevision, activeRenderViewState));
    }

    function failCanvasWorker(message) {
      console.warn('Canvas worker failed', message);
      canvasWorker?.terminate();
      canvasWorker = null;
      replaceCanvas();
      rendererMode = 'canvas2d';
      resize();
      ctx2d = canvas.getContext('2d', { alpha: true });
      if (!ctx2d) throw new Error('Canvas 대체 렌더러도 사용할 수 없습니다.');
      updateRendererStatus(`Canvas · ${meshQualityLabel()} 대체`, fallbackReason);
      setActionStatus(`${meshQualityLabel()} Canvas로 전환했습니다.`, 'working', 4200);
      if (hydroPreparation.manifest && hydroPreparation.sourceUrl) setHydroManifest(hydroPreparation.manifest, hydroPreparation.sourceUrl);
      renderCanvasFallback();
      completeGeometryDisplay(
        geometryRevisionTracker.pendingIds(),
        geometryRevisionTracker.committedRevision(),
        { renderFrame: false },
      );
    }

    function receiveCanvasWorkerMessage(event) {
      const message = event.data || {};
      if (message.type === 'ready') {
        postCanvasWorkerMessage({ type: 'hydro-edits', revision: hydroPreparation.editRevision, features: state.hydroEdits || [] });
        canvasLastStyleSignature = '';
        canvasLastPhysicalStyleSignature = '';
        syncCanvasWorkerState();
        return;
      }
      if (message.type === 'terrain-ready') {
        invalidateGpuFrame('canvas-terrain-ready');
        return;
      }
      if (message.type === 'terrain-warning') {
        console.warn('Canvas 지형 타일을 불러오지 못했습니다.', message.message || '알 수 없는 오류');
        return;
      }
      if (message.type === 'data-ready') {
        const geometryRevision = Number(message.geometryRevision || 0);
        const taskToken = Number(message.taskToken || 0);
        if (taskToken && !geometryRevisionTracker.isCurrent(taskToken, geometryRevision)) return;
        if (lastGeometryCommitTimings && geometryRevision === geometryRevisionTracker.committedRevision()) {
          lastGeometryCommitTimings.patchWorkerCompletedAt ||= performance.now();
        }
        if (message.replaceAll) {
          countryOverrideIds.clear();
          overrideFeatureSnapshots.clear();
          overrideMesh = null;
        }
        canvasDataReplacementResolver?.();
        canvasDataReplacementResolver = null;
        invalidateGpuFrame('canvas-data-ready');
        renderCanvasWorker(Math.max(currentRenderRevision, Number(message.revision || 0)));
        return;
      }
      if (message.type === 'error') {
        failCanvasWorker(message.message || 'Canvas Worker 렌더링 오류');
        return;
      }
      if (message.type !== 'frame') return;
      const revision = Number(message.revision || 0);
      const geometryRevision = Number(message.geometryRevision || 0);
      if (message.bitmap) {
        if (canvasWorkerBitmapContext) {
          canvasWorkerBitmapContext.transferFromImageBitmap(message.bitmap);
        } else if (canvasWorker2dContext) {
          canvasWorker2dContext.setTransform(1, 0, 0, 1, 0, 0);
          canvasWorker2dContext.clearRect(0, 0, canvas.width, canvas.height);
          canvasWorker2dContext.drawImage(message.bitmap, 0, 0, canvas.width, canvas.height);
          message.bitmap.close?.();
        }
        canvasDisplayedStyleRevision = Number(message.styleRevision || 0);
        displayedRenderRevision = revision;
        markPreviewFramePresented();
        framePresentationListener?.({
          frameId: Number(message.frameId || revision || 0),
          viewRevision: Number(message.viewRevision || revision || 0),
          projectionRevision: Number(message.projectionRevision || 0),
          projectGeneration: Number(message.projectGeneration || projectGeneration),
          renderer: 'canvas-worker',
        });
        completeGeometryDisplay(geometryRevisionTracker.pendingIds(), geometryRevision, { renderFrame: false });
        if (message.terrainComplete === false) performanceMetrics.terrainIncompleteFrameCount += 1;
      }
    }

    function activateCanvasFallback(reason) {
      if (disposed) return;
      canvasWorkerNeedsRestart = false;
      canvasSentGeometry.clear();
      const rawReason = String(reason || '');
      if (rawReason && !isSafeKoreanErrorMessage({ message: rawReason })) console.warn('[PL-GPU-005]', rawReason);
      fallbackReason = isSafeKoreanErrorMessage({ message: rawReason }) ? rawReason : 'GPU 렌더러를 사용할 수 없습니다.';
      lifecycle.cancelTimeout(webglRecoveryTimer);
      webglContextLost = false;
      canvasWorker?.terminate();
      canvasWorker = null;
      if (canvasWorkerUrl) URL.revokeObjectURL(canvasWorkerUrl);
      canvasWorkerUrl = null;
      if (canvas) replaceCanvas();
      if (typeof Worker === 'function' && typeof OffscreenCanvas === 'function') {
        try {
          rendererMode = 'canvas-worker';
          resize();
          const canvasRuntimeUrl = runtimeAssetUrl('workers/canvas-render-worker.js');
          canvasRuntimeUrl.searchParams.set('physical', '1');
          canvasWorker = createGpuCanvasWorker({
            worker: workerChannels.create(canvasRuntimeUrl, { name: 'pandolab-canvas-renderer' }),
            generation: projectGeneration, onSend: recordCanvasWorkerMessage,
            acceptFrame: message => Number(message.geometryRevision || 0) >= geometryRevisionTracker.committedRevision()
              && Number(message.styleRevision || 0) === canvasStyleRevision,
            onStale: () => { performanceMetrics.canvasWorkerStaleFrameCount += 1; },
          });
          canvasWorker.queueFrame(canvasWorkerViewMessage(currentRenderRevision));
          canvasWorkerBitmapContext = canvas.getContext('bitmaprenderer');
          if (!canvasWorkerBitmapContext) canvasWorker2dContext = canvas.getContext('2d', { alpha: true });
          if (!canvasWorkerBitmapContext && !canvasWorker2dContext) throw new Error('Canvas 표시 컨텍스트를 만들 수 없습니다.');
          const initMessage = canvasWorkerInitMessage();
          initMessage.features = state.countriesData?.features || [];
          canvasWorker.onmessage = receiveCanvasWorkerMessage;
          canvasWorker.onerror = event => failCanvasWorker(event.message || 'Canvas Worker 실행 오류');
          postCanvasWorkerMessage(initMessage);
          if (hydroPreparation.manifest && hydroPreparation.sourceUrl) setHydroManifest(hydroPreparation.manifest, hydroPreparation.sourceUrl);
          else connectHydroCanvasWorkers();
          updateRendererStatus('Canvas Worker · 완성 프레임 즉시 표시', fallbackReason);
          setActionStatus(`${meshQualityLabel()} Canvas Worker로 전환했습니다.`, 'working', 4200);
          window.__PANDOLAB_GPU_METRICS__ = getStats();
          return;
        } catch (error) {
          console.warn('Canvas worker unavailable', error);
          canvasWorker?.terminate();
          canvasWorker = null;
          replaceCanvas();
        }
      }
      rendererMode = 'canvas2d';
      ctx2d = canvas.getContext('2d', { alpha: true });
      if (!ctx2d) throw new Error('Canvas 대체 렌더러도 사용할 수 없습니다.');
      updateRendererStatus(`Canvas · ${meshQualityLabel()} 대체`, fallbackReason);
      setActionStatus(`${meshQualityLabel()} Canvas로 전환했습니다.`, 'working', 4200);
      if (hydroPreparation.manifest && hydroPreparation.sourceUrl) setHydroManifest(hydroPreparation.manifest, hydroPreparation.sourceUrl);
      renderCanvasFallback();
      completeGeometryDisplay(
        geometryRevisionTracker.pendingIds(),
        geometryRevisionTracker.committedRevision(),
        { renderFrame: false },
      );
      window.__PANDOLAB_GPU_METRICS__ = getStats();
    }

    function ensurePickTarget() {
      if (pickFramebuffer && pickTexture) return;
      pickFramebuffer = lifecycle.create(gl, 'Framebuffer');
      pickTexture = lifecycle.create(gl, 'Texture');
      gl.bindTexture(gl.TEXTURE_2D, pickTexture);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      const internalFormat = glVersion === 2 ? gl.RGBA8 : gl.RGBA;
      gl.texImage2D(gl.TEXTURE_2D, 0, internalFormat, pixelWidth, pixelHeight, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
      gl.bindFramebuffer(gl.FRAMEBUFFER, pickFramebuffer);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, pickTexture, 0);
      if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) throw new Error('GPU 선택 버퍼 생성 실패');
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    }

    function countryIdSceneKey(pickEntry) {
      return [
        Number(window.__PANDOLAB_VIEW_REVISION__ || 0),
        geometryRevisionTracker.displayedRevision(),
        Number(state.layerTreeRevision || 0),
        pixelWidth,
        pixelHeight,
        pickEntry?.quality || activeMeshQuality,
      ].join(':');
    }

    function ensureCountryIdScene() {
      if (!isWebGlRenderer() || !gl || !mesh || !state.layerVisibility.countries) return false;
      try { ensurePickTarget(); } catch (_) { return false; }
      const pickEntry = meshVariants.get(activeMeshQuality) || meshVariants.get(meshQuality);
      const pickMesh = pickEntry?.mesh || mesh;
      const pickResources = pickEntry?.resources;
      const nextSceneKey = countryIdSceneKey(pickEntry);
      gl.bindFramebuffer(gl.FRAMEBUFFER, pickFramebuffer);
      gl.viewport(0, 0, pixelWidth, pixelHeight);
      if (nextSceneKey !== pickSceneKey) {
        flushPaletteUpdates();
        gl.disable(gl.BLEND);
        gl.clearColor(0, 0, 0, 0);
        gl.clear(gl.COLOR_BUFFER_BIT);
        drawProgram(
          pickProgram,
          pickResources?.fillVao || fillVao,
          pickResources?.fillIndexBuffer || fillIndexBuffer,
          pickMesh.triangleIndices.length,
          gl.TRIANGLES,
          pickResources ? { positionBuffer: pickResources.positionBuffer, countryBuffer: pickResources.countryBuffer } : null,
        );
        if (overrideMesh?.triangleIndices?.length) {
          drawProgram(
            pickProgram,
            overrideFillVao,
            overrideFillIndexBuffer,
            overrideMesh.triangleIndices.length,
            gl.TRIANGLES,
            { positionBuffer: overridePositionBuffer, countryBuffer: overrideCountryBuffer },
            overridePaletteTexture,
          );
        }
        pickSceneKey = nextSceneKey;
        pickSceneRenderCount += 1;
      }
      gl.enable(gl.BLEND);
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      return true;
    }

    function pick(screenPoint) {
      if (!isWebGlRenderer() || !gl || !mesh || !state.layerVisibility.countries) return null;
      resize();
      const pickEntry = meshVariants.get(activeMeshQuality) || meshVariants.get(meshQuality);
      if (!ensureCountryIdScene()) return null;
      gl.bindFramebuffer(gl.FRAMEBUFFER, pickFramebuffer);
      gl.viewport(0, 0, pixelWidth, pixelHeight);
      const dpr = pixelWidth / cssWidth;
      const x = Math.max(0, Math.min(pixelWidth - 1, Math.round(screenPoint[0] * dpr)));
      const y = Math.max(0, Math.min(pixelHeight - 1, Math.round(pixelHeight - 1 - screenPoint[1] * dpr)));
      const pixel = new Uint8Array(4);
      const readStartedAt = performance.now();
      gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
      pickLastReadPixelsMs = performance.now() - readStartedAt;
      pickReadPixelsMs += pickLastReadPixelsMs;
      pickCount += 1;
      gl.enable(gl.BLEND);
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      const index = (pixel[0] | (pixel[1] << 8) | (pixel[2] << 16)) - 1;
      return index >= 0 ? pickEntry?.countryIds?.[index] || meshCountryIds[index] || null : null;
    }

    function pickHydro(screenPoint) {
      if (!isWebGlRenderer() || !gl || !hydroPreparation.manifest || !hydroPreparation.activeIds().length || !(state.layerVisibility.rivers || state.layerVisibility.lakes)) return null;
      resize();
      try { ensurePickTarget(); } catch (_) { return null; }
      pickSceneKey = '';
      gl.bindFramebuffer(gl.FRAMEBUFFER, pickFramebuffer);
      gl.viewport(0, 0, pixelWidth, pixelHeight);
      gl.disable(gl.BLEND);
      gl.clearColor(0, 0, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT);
      updateHydroVisibility();
      drawHydro('lake', true);
      drawHydro('river', true);
      drawHydro('border-river', true);
      const dpr = pixelWidth / cssWidth;
      const x = Math.max(0, Math.min(pixelWidth - 1, Math.round(screenPoint[0] * dpr)));
      const y = Math.max(0, Math.min(pixelHeight - 1, Math.round(pixelHeight - 1 - screenPoint[1] * dpr)));
      const pixel = new Uint8Array(4);
      gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
      gl.enable(gl.BLEND);
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      const fid = (pixel[0] | (pixel[1] << 8) | (pixel[2] << 16)) - 1;
      return fid >= 0 ? state.hydroFeatureByFid.get(fid) || null : null;
    }

    function pickHydroAsync(screenPoint) {
      if (rendererMode !== 'canvas-worker' || !canvasWorker) return Promise.resolve(null);
      return canvasWorker.pick(screenPoint).then(fid => fid != null && Number.isFinite(Number(fid))
        ? state.hydroFeatureByFid.get(Number(fid)) || null : null);
    }

    async function initialize({ allowPreview = true, preview = null } = {}) {
      if (disposed) return false;
      if (!allowPreview) {
        previewAllowed = false;
        qualityPhase = 'canonical-loading';
        meshQuality = 'canonical';
        activeMeshQuality = 'canonical';
      }
      if (forcedRenderer === 'canvas') {
        activateCanvasFallback('강제 Canvas 테스트');
        return false;
      }
      let decoded = null;
      const failures = [];
      const versions = forcedRenderer === 'webgl2' ? [2] : forcedRenderer === 'webgl1' ? [1] : [2, 1];
      for (let index = 0; index < versions.length; index += 1) {
        const version = versions[index];
        if (index > 0 || gl) replaceCanvas();
        try {
          initWebGl(version);
          updateRendererStatus(`${rendererName()} · 빠른 GPU 지도를 준비하는 중입니다.`);
          if (!previewAllowed && !meshVariants.has('canonical')) {
            updateRendererStatus(`${rendererName()} · 저장된 지도 준비 중`);
            return true;
          }
          if ((!previewAllowed || canonicalMeshReady) && meshVariants.has('canonical')) {
            qualityPhase = 'canonical-loading';
            if (meshVariants.has('canonical')) activateMeshVariant('canonical', { renderFrame: false });
            return true;
          }
          if (!previewAllowed || canonicalMeshReady) throw new Error('canonical mesh unavailable after startup preview');
          if (!decoded) decoded = preview?.mesh && preview?.countryIds
            ? { mesh: preview.mesh, ids: preview.countryIds }
            : await decodeBuiltInMesh();
          if (disposed) return false;
          setMesh(decoded.mesh, decoded.ids, { quality: 'preview', preserveOtherVariants: false });
          meshQuality = 'preview';
          canonicalMeshReady = false;
          if (isWebGlRenderer()) {
            updateRendererStatus(`${rendererName()} · 빠른 GPU 미리보기`);
            return true;
          }
        } catch (error) {
          const message = error?.message || String(error);
          failures.push(`${version === 2 ? 'WebGL2' : 'WebGL1'}: ${message}`);
          console.warn(`${version === 2 ? 'WebGL2' : 'WebGL1'} renderer unavailable`, error);
        }
      }
      if (gl) replaceCanvas();
      activateCanvasFallback(failures.join(' / ') || 'WebGL을 사용할 수 없습니다.');
      return false;
    }

    async function replaceBuiltInMesh({ meshBuffer, preparedStroke, spatialBlocks, features, countryPatchIds = [], onStaged = null, quality = 'canonical', builtinIdentity = null, projectGeneration: requestedGeneration = projectGeneration }) {
      if (disposed || Number(requestedGeneration) !== projectGeneration) return false;
      const startupPatch = normalizeCountryPatchRequest(countryPatchIds);
      let patchMesh = null;
      if (startupPatch.features.length && isWebGlRenderer()) {
        const ticket = patchJobScheduler.enqueue({ jobKey: 'mesh:startup-country-overrides',
          geometryRevision: geometryRevisionTracker.committedRevision(), targetRevision: requestedGeneration,
          priority: 80, payload: { token: requestedGeneration, features: startupPatch.features } });
        patchMesh = await ticket.promise;
        if (!patchMesh || Number(requestedGeneration) !== projectGeneration) return false;
      }
      const decoded = await decodeBuiltInMesh(meshBuffer, features, preparedStroke);
      decoded.mesh.spatialBlocks = spatialBlocks;
      if (quality === 'canonical' && builtinIdentity) rememberBuiltinMesh(decoded.mesh, decoded.ids, builtinIdentity);
      if (Number(requestedGeneration) !== projectGeneration) return false;
      const stagedResources = await stageMeshResources(decoded.mesh, { projectGeneration: requestedGeneration });
      if (Number(requestedGeneration) !== projectGeneration) { disposeMeshResources(stagedResources); return false; }
      let stagedPatch = null;
      if (patchMesh) {
        const globalIds = [...decoded.ids];
        const remapped = remapOverrideMesh(patchMesh, patchMesh.countryIds || [], globalIds);
        const resources = await stageMeshResources(remapped, { projectGeneration: requestedGeneration });
        if (Number(requestedGeneration) !== projectGeneration) {
          disposeMeshResources(stagedResources); disposeMeshResources(resources); return false;
        }
        stagedPatch = { mesh: remapped, resources, globalIds };
      }
      setMesh(decoded.mesh, decoded.ids, {
        stagedResources,
        renderFrame: false,
        quality,
        preserveOtherVariants: quality === 'canonical' && meshVariants.has('preview'),
      });
      if (startupPatch.ids.length) {
        for (const id of startupPatch.ids) countryOverrideIds.add(id);
        for (const feature of startupPatch.features) overrideFeatureSnapshots.set(String(feature.id), deepClone(feature));
        for (const id of startupPatch.removedIds) overrideFeatureSnapshots.delete(id);
        if (stagedPatch) {
          for (const id of stagedPatch.globalIds) if (!meshCountryIds.includes(id)) meshCountryIds.push(id);
          setOverrideMesh(stagedPatch.mesh, { renderFrame: false, stagedResources: stagedPatch.resources });
        }
      }
      meshQuality = quality;
      if (canvasWorkerNeedsRestart) activateCanvasFallback(fallbackReason);
      if (rendererMode === 'canvas-worker' && canvasWorker) {
        await new Promise(resolve => {
          const timeout = lifecycle.timeout(() => {
            if (canvasDataReplacementResolver === complete) canvasDataReplacementResolver = null;
            resolve();
          }, 3000);
          const complete = () => {
            lifecycle.cancelTimeout(timeout);
            resolve();
          };
          canvasDataReplacementResolver = complete;
          postCanvasWorkerMessage({
            type: 'replace-data',
            revision: Number(currentRenderRevision || 0),
            geometryRevision: geometryRevisionTracker.committedRevision(),
            features: state.countriesData?.features || features || [],
          });
        });
      } else {
        completeGeometryDisplay(
          geometryRevisionTracker.pendingIds(),
          geometryRevisionTracker.committedRevision(),
          { renderFrame: !isWebGlRenderer() },
        );
      }
      updateRendererStatus(isWebGlRenderer()
        ? `${rendererName()} · GPU ${meshQualityLabel()}`
        : `${rendererMode === 'canvas-worker' ? 'Canvas Worker' : 'Canvas'} · ${meshQualityLabel()}`,
        fallbackReason);
      projectRenderBlocked = false;
      sceneColorCache.invalidate('built-in-mesh-ready');
      if (quality === 'canonical' && isWebGlRenderer()) {
        await new Promise((resolve, reject) => {
          pendingCanonicalCommit = { resolve, reject, generation: projectGeneration };
          onStaged?.();
          invalidateGpuFrame('canonical-staging-ready');
        });
        await flushDeferredCountryPatches();
      } else if (quality === 'canonical') promoteCanonicalMesh({ frameId: currentRenderRevision });
      return decoded;
    }

    function setCountryEmphasis({ primaryId = '', primaryIds = [], priorities = {}, hoverId = '', selectedIds = [] } = {}) {
      const nextSelected = new Set((selectedIds || []).map(String).filter(Boolean));
      const nextPrimary = String(primaryId || '');
      const nextPrimaries = new Set([...primaryIds, nextPrimary].map(String).filter(Boolean));
      const nextHover = String(hoverId || '');
      const unchanged = countryEmphasis.primaryId === nextPrimary
        && JSON.stringify(countryEmphasis.priorities) === JSON.stringify(priorities)
        && countryEmphasis.primaryIds.size === nextPrimaries.size
        && [...nextPrimaries].every(id => countryEmphasis.primaryIds.has(id))
        && countryEmphasis.hoverId === nextHover
        && countryEmphasis.selectedIds.size === nextSelected.size
        && [...nextSelected].every(id => countryEmphasis.selectedIds.has(id));
      if (unchanged) {
        performanceMetrics.paletteSkippedUnchangedCount += 1;
        return false;
      }
      const changedIds = new Set([
        countryEmphasis.primaryId,
        countryEmphasis.hoverId,
        ...countryEmphasis.selectedIds,
        ...countryEmphasis.primaryIds,
        ...nextPrimaries,
        nextPrimary,
        nextHover,
        ...nextSelected,
      ].map(String).filter(Boolean));
      countryEmphasis = { primaryId: nextPrimary, primaryIds: nextPrimaries, priorities, hoverId: nextHover, selectedIds: nextSelected };
      countryEmphasisRevision += 1;
      markPaletteDirty({ emphasis: true, countryIds: [...changedIds] });
      if (rendererMode !== 'pending') invalidateGpuInteraction('country-emphasis');
      return true;
    }

    function clearCountryEmphasis() {
      return setCountryEmphasis();
    }

    function setInteractionStyle(nextStyle) {
      if (!nextStyle?.hover || !nextStyle?.selection) return false;
      interactionStyle = nextStyle;
      countryEmphasisRevision += 1;
      markPaletteDirty({ emphasis: true });
      if (rendererMode !== 'pending') invalidateGpuInteraction('interaction-style');
      return true;
    }

    function getCountryInteractionBoundaryData() {
      const pendingIds = geometryRevisionTracker.pendingIds().map(String).sort();
      const overriddenIds = [...countryOverrideIds].map(String).sort();
      const visibleIds = meshCountryIds.filter(id => isCountryVisibleById(id));
      const strokeResources = currentCountryStrokeResources();
      return {
        revision: [activeMeshQuality, geometryRevisionTracker.committedRevision(), geometryRevisionTracker.displayedRevision(), mesh?.lineIndices?.length || 0, overrideMesh?.lineIndices?.length || 0, pendingIds.join(','), overriddenIds.join(','), Number(state.layerTreeRevision || 0)].join(':'),
        base: mesh,
        override: overrideMesh,
        countryIds: meshCountryIds,
        pendingIds,
        overriddenIds,
        visibleIds,
        strokeResources,
      };
    }

    function supportsCountryEmphasis() {
      return isWebGlRenderer();
    }

    function setSelectionPass(nextPass) {
      if (selectionPass === nextPass) return !!selectionPass;
      selectionPass?.dispose?.();
      selectionPass = nextPass || null;
      lastSelectionRenderResult = null;
      if (selectionPass && renderDevice && isWebGlRenderer()) {
        selectionPass.initialize?.(renderDevice, { strokeRenderer, polygonPass: polygonOverlayPass });
      }
      invalidateGpuInteraction('selection-pass');
      return !!selectionPass;
    }

    function retainSceneResources() {
      const interactionPackets = [
        ...(renderInteractionState.previewPackets || []),
        ...(renderInteractionState.draftPackets || []),
      ].map(item => item?.packet).filter(Boolean);
      const protectedKeys = new Set([
        ...(renderScene?.polygons || []).filter(packet => packet.protected).map(packet => packet.key),
        ...(renderScene?.strokes || []).filter(packet => packet.protected).map(packet => packet.key),
        ...(selectionPass?.resourceKeys?.() || []),
      ]);
      polygonOverlayPass.retain([
        ...(renderScene?.polygons || []).map(packet => packet.key),
        ...interactionPackets.filter(packet => packet.positions instanceof Float32Array).map(packet => packet.key),
      ], { protectedKeys });
      const countryStrokeKeys = Object.values(currentCountryStrokeResources())
        .map(resource => resource?.packet?.key).filter(Boolean);
      strokeRenderer.retain([
        ...(renderScene?.strokes || []).map(packet => packet.key),
        ...interactionPackets.filter(packet => packet.startsEnds instanceof Float32Array).map(packet => packet.key),
        ...countryStrokeKeys,
        ...(selectionPass?.resourceKeys?.() || []),
      ], { protectedKeys });
    }

    function nonCountrySceneSignature(scene) {
      const nonCountryPackets = [
        ...(scene?.polygons || []),
        ...(scene?.strokes || []),
      ].filter(packet => !String(packet?.key || '').startsWith('pending-country-'))
        .map(packet => [
          packet.kind,
          packet.key,
          packet.sourceKey,
          packet.order,
          packet.blendMode,
          packet.role,
          packet.ownerId,
          packet.parentId,
          packet.territoryDepth,
          JSON.stringify(packet.style || {}),
        ].join(':')).join('|');
      return scene ? [
        scene.revisions?.style,
        scene.revisions?.overlayOrder,
        scene.revisions?.selection,
        scene.revisions?.editPreview,
        scene.physical?.hydroVisibilityRevision,
        scene.physical?.hydroStyleRevision,
        nonCountryPackets,
      ].join(':') : '';
    }

    function shouldPreserveSceneAcrossCountryPatch(previousScene, nextScene) {
      const presentation = countryPatchPresentation;
      if (!presentation || !['waiting-mesh', 'staging', 'promoted'].includes(presentation.phase)) return false;
      if (presentation.preserveAllowed === false) return false;
      return nonCountrySceneSignature(previousScene) === nonCountrySceneSignature(nextScene);
    }

    function setRenderScene(nextScene) {
      if (nextScene != null && !isRenderScene(nextScene)) return false;
      const previousScene = renderScene;
      const previousBaseSignature = renderScene ? [
        renderScene.revisions?.geometry,
        renderScene.revisions?.style,
        renderScene.revisions?.overlayOrder,
        renderScene.revisions?.countryState,
        renderScene.country?.meshRevision,
        renderScene.country?.overrideRevision,
        renderScene.physical?.hydroVisibilityRevision,
        renderScene.physical?.hydroStyleRevision,
      ].join(':') : '';
      renderScene = nextScene || null;
      renderInteractionState = renderScene?.interaction || Object.freeze({ selectionPacket: null, genericFillItems: Object.freeze([]) });
      lastRenderSceneRevision = Number(renderScene?.revision || 0);
      const nextBaseSignature = renderScene ? [
        renderScene.revisions?.geometry,
        renderScene.revisions?.style,
        renderScene.revisions?.overlayOrder,
        renderScene.revisions?.countryState,
        renderScene.country?.meshRevision,
        renderScene.country?.overrideRevision,
        renderScene.physical?.hydroVisibilityRevision,
        renderScene.physical?.hydroStyleRevision,
      ].join(':') : '';
      retainSceneResources();
      if (previousBaseSignature !== nextBaseSignature) {
        if (!shouldPreserveSceneAcrossCountryPatch(previousScene, renderScene)) {
          // Invalidating the cache alone still leaves its active texture
          // available to canCompositePreserved(). Do not hold that stale scene
          // after a non-country change while waiting for terrain tiles.
          if (countryPatchPresentation) countryPatchPresentation.preserveAllowed = false;
          lastBaseSceneResult = null;
          sceneColorCache.invalidate('render-scene');
        }
        if (countryPatchPresentation?.phase === 'promoted') countryPatchPresentation = null;
      }
      return true;
    }

    let canvasInteractionSignature = '';
    function setInteractionState(nextInteraction = {}) {
      const signature = JSON.stringify([nextInteraction.selectionPacket?.revision, nextInteraction.selectionPacket?.hoverRevision,
        nextInteraction.selectionPacket?.styleRevision, nextInteraction.genericFillItems,
        [...(nextInteraction.previewPackets || []), ...(nextInteraction.draftPackets || [])].map(item => [item.packet?.key, item.packet?.geometryRevision, item.packet?.style])]);
      if (signature !== canvasInteractionSignature) { canvasInteractionSignature = signature; canvasLastStyleSignature = ''; }
      renderInteractionState = Object.freeze({
        selectionPacket: nextInteraction.selectionPacket || null,
        genericFillKeys: Object.freeze([...(nextInteraction.genericFillKeys || [])].map(String)),
        genericFillItems: Object.freeze([...(nextInteraction.genericFillItems || [])]),
        previewPackets: Object.freeze([...(nextInteraction.previewPackets || [])]),
        draftPackets: Object.freeze([...(nextInteraction.draftPackets || [])]),
      });
      retainSceneResources();
      return true;
    }

    function invalidateSceneCache(reason = 'explicit') {
      sceneColorCache.invalidate(reason);
    }

    async function fallbackTerrainToRaster(reason) {
      if (terrainFallbackPending || terrainManifest?.representation !== TERRAIN_DEM_FORMAT) return;
      terrainFallbackPending = true;
      const originalManifest = terrainManifest;
      const generation = projectGeneration;
      try {
        const response = await fetch(TERRAIN_RASTER_MANIFEST_URL);
        if (!response.ok) throw new Error(`Raster terrain HTTP ${response.status}`);
        const fallback = validateTerrainManifest(await response.json());
        if (generation === projectGeneration && terrainManifest === originalManifest) {
          console.warn('DEM terrain fallback:', reason);
          setTerrainManifest(fallback, TERRAIN_RASTER_MANIFEST_URL);
          onTerrainSourceChanged?.(fallback);
        }
      } catch (error) { console.warn('Raster terrain fallback failed', error); }
      finally { terrainFallbackPending = false; }
    }

    function setTerrainManifest(manifest, manifestUrl = TERRAIN_RASTER_MANIFEST_URL) {
      if (demShadeTimer !== null) { clearTimeout(demShadeTimer); demShadeTimer = null; }
      demShadeBlend = 0; demSettleStartedAt = 0;
      terrainManifest = manifest?.levels?.length ? manifest : null;
      terrainManifestUrl = manifestUrl;
      terrainPreparation.setManifest(terrainManifest);
      if (terrainManifest?.representation === TERRAIN_DEM_FORMAT && gl && !ensureTerrainDemProgram()) {
        void fallbackTerrainToRaster('DEM 셰이더 정밀도가 부족합니다.');
      }
      invalidatePhysicalScene('terrain-manifest');
    }

    function activeTerrainSourceInfo() {
      if (rendererMode === 'canvas-worker' || rendererMode === 'canvas2d') {
        return { dataset: TERRAIN_RASTER_DATASET, version: TERRAIN_RASTER_VERSION };
      }
      return { dataset: terrainManifest?.dataset || TERRAIN_RASTER_DATASET,
        version: terrainManifest?.version || TERRAIN_RASTER_VERSION };
    }

    function setFramePresentationListener(listener) {
      framePresentationListener = typeof listener === 'function' ? listener : null;
    }

    function publishLightweightMetrics() {
      const target = window.__PANDOLAB_GPU_METRICS__ ||= {};
      target.renderer = rendererMode;
      target.requestedRevision = currentRenderRevision;
      target.displayedRevision = displayedRenderRevision;
      target.p95CpuSubmitMs = cachedDetailedStats.p95CpuSubmitMs;
      target.p99CpuSubmitMs = cachedDetailedStats.p99CpuSubmitMs;
      target.paletteRebuildCount = performanceMetrics.paletteRebuildCount;
      target.paletteUploadCount = performanceMetrics.paletteUploadCount;
      target.paletteUploadBytes = performanceMetrics.paletteUploadBytes;
      Object.assign(target, hydroPreparation.metrics());
      target.canvasWorkerMessageCount = performanceMetrics.canvasWorkerMessageCount;
      target.canvasWorkerMessageBytes = performanceMetrics.canvasWorkerMessageBytes;
      target.countryBaseIndexCount = performanceMetrics.countryBaseIndexCount;
      target.countryBaseFullIndexCount = performanceMetrics.countryBaseFullIndexCount;
      target.countryBaseRangeCount = performanceMetrics.countryBaseRangeCount;
      target.countryVisibleCount = performanceMetrics.countryVisibleCount;
      target.countryCullingFallbackCount = performanceMetrics.countryCullingFallbackCount;
      target.pendingCountryCount = geometryRevisionTracker.pendingIds().length;
      target.pendingOldMeshVisibleCount = pendingOldMeshVisibleCount;
      target.countryPatchPresentation = countryPatchPresentation ? {
        mode: countryPatchPresentation.mode,
        phase: countryPatchPresentation.phase,
        geometryRevision: countryPatchPresentation.geometryRevision,
        heldFrameCount: countryPatchPresentation.heldFrameCount,
      } : null;
      target.activeWebGlContextCount = renderDevice && isWebGlRenderer() ? 1 : 0;
      target.sceneCacheValid = sceneColorCache.isValid();
      target.projectGeneration = projectGeneration;
      target.projectRenderBlocked = projectRenderBlocked;
      target.mapHost = 'legacy';
    }

    function getRuntimeState() {
      return {
        renderer: rendererMode,
        fallbackReason,
        effectivePixelRatio,
        devicePixelRatio: Math.max(1, Number(window.devicePixelRatio || 1)),
        activeWebGlContextCount: renderDevice && isWebGlRenderer() ? 1 : 0,
        canonicalMeshReady,
        previewAllowed,
        firstCanonicalFrameMs,
        canonicalFrameFallbackCount,
        projectGeneration,
        terrainRepresentation: terrainManifest?.representation || null,
        terrainDatasetVersion: activeTerrainSourceInfo().version,
        terrainShadeBlend: demShadeBlend,
        projectRenderBlocked,
        sceneCacheValid: sceneColorCache.isValid(),
      };
    }

    function getStats({ detailed = true } = {}) {
      if (detailed && performance.now() - cachedDetailedStats.at > 250) {
        const sorted = [...frameTimes].sort((a, b) => a - b);
        const p95 = sorted.length ? sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * 0.95))] : 0;
        const p99 = sorted.length ? sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * 0.99))] : 0;
        cachedDetailedStats = {
          at: performance.now(),
          p95CpuSubmitMs: Number(p95.toFixed(3)),
          p99CpuSubmitMs: Number(p99.toFixed(3)),
        };
      }
      return {
        renderer: rendererMode,
        mapHost: 'legacy',
        assetRevision: ASSET_REVISION,
        dataRevision: DATA_REVISION,
        dataCacheName: `pandolab-data-${DATA_REVISION}`,
        projectGeneration,
        terrainRepresentation: terrainManifest?.representation || null,
        terrainDatasetVersion: activeTerrainSourceInfo().version,
        terrainShadeBlend: demShadeBlend,
        projectRenderBlocked,
        activeWebGlContextCount: renderDevice && isWebGlRenderer() ? 1 : 0,
        renderSceneRevision: lastRenderSceneRevision,
        sceneCacheValid: sceneColorCache.isValid(),
        sceneCache: sceneColorCache.stats(),
        sceneCacheFullDrawCount,
        sceneCacheInteractionDrawCount,
        sceneCacheReprojectCount,
        selectionOnlyBaseDrawCount: sceneCacheSelectionOnlyBaseDrawCount,
        sceneCacheFallbackFrame,
        canonicalFrameFallbackCount,
        firstCanonicalFrameMs,
        polygonOverlay: polygonOverlayPass.stats(),
        stroke: strokeRenderer.stats(),
        selection: selectionPass?.stats?.() || null,
        lastSelectionRenderResult,
        effectivePixelRatio,
        devicePixelRatio: Math.max(1, Number(window.devicePixelRatio || 1)),
        meshQuality,
        activeMeshQuality,
        canonicalMeshReady,
        availableMeshQualities: [...meshVariants.keys()],
        meshVariantBytes: Object.fromEntries([...meshVariants.entries()].map(([quality, entry]) => [quality, Number(entry.resources?.byteLength || 0)])),
        meshRestorePending: false,
        qualityPhase,
        previewAllowed,
        previewActivationCount,
        previewActivationAfterCanonical,
        canonicalPromotionCount,
        canonicalPromotionError,
        canonicalReadyFrameId,
        meshSwitchCount,
        renderQualityChangeCount,
        renderQuality: { ...renderQuality },
        countries: meshCountryIds.length,
        renderVertices: mesh?.countryIndices?.length || 0,
        triangleCount: (mesh?.triangleIndices?.length || 0) / 3,
        lineSegmentCount: (mesh?.lineIndices?.length || 0) / 2,
        p95CpuSubmitMs: cachedDetailedStats.p95CpuSubmitMs,
        p99CpuSubmitMs: cachedDetailedStats.p99CpuSubmitMs,
        pickCount,
        pickReadPixelsMs: Number(pickReadPixelsMs.toFixed(3)),
        pickLastReadPixelsMs: Number(pickLastReadPixelsMs.toFixed(3)),
        pickSceneRenderCount,
        countryEmphasisRevision,
        interactionFillCoverage: lastInteractionFillResult,
        interactionFillItems: (renderInteractionState.genericFillItems || []).map(item => ({ key: item.key, priority: item.priority })),
        countryEmphasis: {
          primaryId: countryEmphasis.primaryId,
          primaryIds: [...countryEmphasis.primaryIds],
          priorities: countryEmphasis.priorities,
          hoverId: countryEmphasis.hoverId,
          selectedIds: [...countryEmphasis.selectedIds],
          boundaryEnabled: false,
          primaryBoundaryColor: interactionStyle.selection.color,
          secondaryBoundaryColor: interactionStyle.selection.color,
        },
        interactionStyle,
        boundaryOwner: 'interaction-overlay',
        visualPassOrder: ['territorial-fill', 'country-fill', 'overlay-fill', 'lake', 'lake-boundary', 'river', 'border-river', 'country-boundary', 'overlay-stroke', 'hover', 'secondary-selection', 'primary-selection'],
        emphasizedCountryCount: countryEmphasis.selectedIds.size,
        viewportCss: [Number(cssWidth.toFixed(3)), Number(cssHeight.toFixed(3))],
        canvasBackingPixels: [pixelWidth, pixelHeight],
        layoutMismatchCssPx: Number(lastLayoutMismatchCssPx.toFixed(3)),
        requestedRevision: currentRenderRevision,
        displayedRevision: displayedRenderRevision,
        committedGeometryRevision: geometryRevisionTracker.committedRevision(),
        displayedGeometryRevision: geometryRevisionTracker.displayedRevision(),
        pendingCountryCount: geometryRevisionTracker.pendingIds().length,
        pendingOldMeshVisibleCount,
        countryPatchPresentation: countryPatchPresentation ? {
          mode: countryPatchPresentation.mode,
          phase: countryPatchPresentation.phase,
          geometryRevision: countryPatchPresentation.geometryRevision,
          heldFrameCount: countryPatchPresentation.heldFrameCount,
        } : null,
        geometryRenderTaskToken: geometryRevisionTracker.taskToken(),
        patchWorkerJobs: patchJobScheduler.stats(),
        patchWorkerOutputBytes: patchJobScheduler.outputBytes(),
        lastGeometryCommitTimings: lastGeometryCommitTimings ? { ...lastGeometryCommitTimings } : null,
        canvasWorkerBusy: !!canvasWorker?.busy,
        canvasStyleRevision, canvasDisplayedStyleRevision,
        canvasWorkerHasPendingFrame: !!canvasWorker?.hasPendingFrame,
        webglContextLost,
        webGlVersion: glVersion || null,
        forcedRenderer: forcedRenderer || null,
        fallbackReason,
        ...terrainPreparation.stats(),
        hydroFeaturesLoaded: state.hydroFeatureCache?.size || 0,
        interactionActive,
        paletteDirty: { ...paletteDirty },
        ...performanceMetrics,
        ...hydroPreparation.stats(),
        canvasWorkerMessagesByType: { ...performanceMetrics.canvasWorkerMessagesByType },
      };
    }

    function releaseGpuContext() {
      hydroPreparation.resetGpu();
      terrainPreparation.reset();
      sceneColorCache.dispose(); interactionFillCache.dispose(); interactionStrokeCache.dispose();
      polygonOverlayPass.dispose(); strokeRenderer.dispose();
      lifecycle.releaseContext(gl);
    }

    function dispose() {
      if (disposed) return;
      if (demShadeTimer !== null) { clearTimeout(demShadeTimer); demShadeTimer = null; }
      disposed = true;
      projectGeneration += 1; renderDeviceContextRevision += 1;
      pendingCanonicalCommit?.reject(Object.assign(new Error('Renderer disposed'), { name: 'AbortError' }));
      pendingCanonicalCommit = null;
      // The rendering domain owns the scheduler; the renderer only cancels its jobs.
      uploadScheduler?.cancelAll();
      stopPatchWorkerJobs('renderer-disposed');
      patchJobScheduler.cancelRebuild();
      canvasDataReplacementResolver?.(); canvasDataReplacementResolver = null;
      hydroPreparation.dispose();
      canvasWorker?.terminate();
      builtinMeshResourceLoader.dispose();
      workerChannels.dispose();
      canvasWorker = null;
      releaseGpuContext(); terrainPreparation.dispose();
      // selectionPass is borrowed from rendering-domain; it owns disposal.
      selectionPass?.handleContextLost?.();
      lifecycle.dispose();
      if (canvasWorkerUrl) URL.revokeObjectURL(canvasWorkerUrl);
      canvasWorkerUrl = null; canvasWorkerBitmapContext = null; canvasWorker2dContext = null;
      meshVariants.clear();
      mesh = null; overrideMesh = null; builtinMeshBaseline = null;
      renderDevice = null; gl = null; ctx2d = null; canvasFillSubstrate = null;
      rendererMode = 'disposed'; framePresentationListener = null;
    }

    return {
      dispose, attach,
      initialize, replaceBuiltInMesh, renderFrame, renderInteraction,
      resize, verifyLayout, pick, pickHydro, pickHydroAsync,
      rebuildFromCountries, applyCountryPatch, compactCountryOverrides, prioritizeLatest, getStats, getRuntimeState, setTerrainManifest, activeTerrainSourceInfo,
      setHydroManifest, loadHydroLogicalFeature, queryHydroLogicalFeatures, retryHydroCache,
      setHydroEdits,
      setHydroInteractionActive, setRenderQuality,
      invalidateHydroVisibility, invalidatePhysicalStyle, resetCountryGeometryVisualState,
      resetProjectRenderState, ensureBuiltinMeshBaseline, activateBuiltinMeshBaseline,
      hasBuiltinMeshBaseline: () => !!builtinMeshBaseline?.mesh,
      getProjectGeneration: () => projectGeneration,
      invalidateCountryPalette,
      setCountryEmphasis, clearCountryEmphasis, supportsCountryEmphasis,
      setInteractionStyle, getCountryInteractionBoundaryData,
      setSelectionPass, setRenderScene, setInteractionState, invalidateSceneCache,
      setFramePresentationListener,
      waitForPreviewFrame,
      getSelectionRenderResult: () => lastSelectionRenderResult,
      getRenderDevice: () => renderDevice,
      getUploadByteBudget: () => renderQuality.uploadBudgetBytes,
      setUploadScheduler: scheduler => {
        uploadScheduler?.cancelAll();
        uploadScheduler = scheduler ? createGpuUploadScope(scheduler) : null;
        strokeRenderer.setUploadScheduler(uploadScheduler);
        polygonOverlayPass.setUploadScheduler(uploadScheduler);
        if (gl) hydroPreparation.setContext({ gl, version: glVersion, projectGeneration, contextGeneration: renderDeviceContextRevision, scheduler: uploadScheduler });
      },
      commitVisualFrame: frame => {
        if (pendingCanonicalCommit?.generation !== projectGeneration || !pendingCanonicalCommit) return;
        const pending = pendingCanonicalCommit; pendingCanonicalCommit = null;
        promoteCanonicalMesh({ frameId: frame.frameId });
        uploadScheduler.defer();
        prewarmCountryStrokeResources();
        pending.resolve();
      },
    };
  })();
}
