const RENDER_QUALITY_TIERS = Object.freeze({
  COARSE: 'coarse',
  MEDIUM: 'medium',
  HIGH: 'high',
});

const TIER_ORDER = Object.freeze([
  RENDER_QUALITY_TIERS.COARSE,
  RENDER_QUALITY_TIERS.MEDIUM,
  RENDER_QUALITY_TIERS.HIGH,
]);

const MIB = 1024 * 1024;

function percentile(values, ratio) {
  if (!values.length) return 0;
  const sorted = [...values].sort((left, right) => left - right);
  return sorted[Math.min(sorted.length - 1, Math.max(0, Math.ceil(sorted.length * ratio) - 1))] || 0;
}

function normalizedTier(value) {
  return TIER_ORDER.includes(value) ? value : RENDER_QUALITY_TIERS.MEDIUM;
}

export function resolveInitialRenderQuality({
  mobile = false,
  deviceMemory = null,
  hardwareConcurrency = null,
  saveData = false,
} = {}) {
  const memory = Number(deviceMemory);
  const cores = Number(hardwareConcurrency);
  const knownMemory = Number.isFinite(memory) && memory > 0;
  const knownCores = Number.isFinite(cores) && cores > 0;
  if ((knownMemory && memory <= 2) || (knownCores && cores <= 2)) return RENDER_QUALITY_TIERS.COARSE;
  if (saveData || mobile || (knownMemory && memory <= 4) || (knownCores && cores <= 4)) return RENDER_QUALITY_TIERS.MEDIUM;
  return RENDER_QUALITY_TIERS.HIGH;
}

export function renderQualityProfile(tier, {
  mobile = false,
  phase = 'settle',
  revision = 0,
  metrics = {},
  reason = '',
} = {}) {
  const normalized = normalizedTier(tier);
  const interaction = phase === 'interaction';
  const definitions = {
    [RENDER_QUALITY_TIERS.COARSE]: {
      backgroundLod: 'coarse', dprCap: 1.25, labelDensity: 0.52,
      // Resolution is a data-quality decision, not an interaction budget.
      // Once canonical data is available we keep the same target level while
      // moving and only throttle requests/uploads in the renderer.
      terrainResolutionScale: 1, terrainCacheBudgetBytes: 32 * MIB,
      hydroCacheBudgetBytes: 40 * MIB, overlayGpuBudgetBytes: 48 * MIB,
      renderPacketCacheBudgetBytes: 48 * MIB,
      interactionUploadBudgetBytes: 256 * 1024, settleUploadBudgetBytes: 2 * MIB,
    },
    [RENDER_QUALITY_TIERS.MEDIUM]: {
      backgroundLod: 'medium', dprCap: mobile ? 1.5 : 1.75, labelDensity: 0.76,
      terrainResolutionScale: 1, terrainCacheBudgetBytes: 64 * MIB,
      hydroCacheBudgetBytes: 72 * MIB, overlayGpuBudgetBytes: 96 * MIB,
      renderPacketCacheBudgetBytes: 96 * MIB,
      interactionUploadBudgetBytes: 512 * 1024, settleUploadBudgetBytes: 4 * MIB,
    },
    [RENDER_QUALITY_TIERS.HIGH]: {
      backgroundLod: 'high', dprCap: mobile ? 2 : 3, labelDensity: 1,
      terrainResolutionScale: 1, terrainCacheBudgetBytes: 128 * MIB,
      hydroCacheBudgetBytes: 96 * MIB, overlayGpuBudgetBytes: 192 * MIB,
      renderPacketCacheBudgetBytes: 192 * MIB,
      interactionUploadBudgetBytes: 768 * 1024, settleUploadBudgetBytes: 8 * MIB,
    },
  };
  const selected = definitions[normalized];
  return Object.freeze({
    tier: normalized,
    phase: interaction ? 'interaction' : 'settle',
    revision: Number(revision || 0),
    // Preview geometry is a one-way startup fallback.  Interaction quality
    // may change cadence and upload budgets, but must never select the
    // low-resolution country dataset again after canonical promotion.
    countryMeshQuality: 'canonical',
    activeEditLod: 'high',
    selectedLod: 'high',
    ...selected,
    uploadBudgetBytes: interaction ? selected.interactionUploadBudgetBytes : selected.settleUploadBudgetBytes,
    targetFrameMs: mobile ? 33 : 20,
    p95FrameMs: Number(metrics.p95FrameMs || 0),
    p99FrameMs: Number(metrics.p99FrameMs || 0),
    longFrameCount: Number(metrics.longFrameCount || 0),
    reason: String(reason || ''),
  });
}

export function createAdaptiveRenderQualityController({
  mobile = false,
  deviceMemory = null,
  hardwareConcurrency = null,
  saveData = false,
  evaluationWindow = 20,
  degradeWindows = 2,
  upgradeWindows = 5,
  changeCooldownMs = 2500,
  now = () => globalThis.performance?.now?.() ?? Date.now(),
} = {}) {
  let tier = resolveInitialRenderQuality({ mobile, deviceMemory, hardwareConcurrency, saveData });
  let phase = 'settle';
  let revision = 1;
  let lastReason = 'device-hints';
  let lastChangeAt = -Infinity;
  let overBudgetWindows = 0;
  let underBudgetWindows = 0;
  let samplesSinceEvaluation = 0;
  let qualityChangeCount = 0;
  let longFrameCount = 0;
  const frameSamples = [];
  const maximumTierIndex = saveData || (Number(deviceMemory) > 0 && Number(deviceMemory) <= 2) ? 1 : 2;

  function metrics() {
    return {
      p95FrameMs: percentile(frameSamples, 0.95),
      p99FrameMs: percentile(frameSamples, 0.99),
      longFrameCount,
    };
  }

  function profile() {
    return renderQualityProfile(tier, { mobile, phase, revision, metrics: metrics(), reason: lastReason });
  }

  function changeTier(nextTier, reason) {
    const nextIndex = Math.min(maximumTierIndex, Math.max(0, TIER_ORDER.indexOf(normalizedTier(nextTier))));
    const normalized = TIER_ORDER[nextIndex];
    if (normalized === tier) return false;
    tier = normalized;
    revision += 1;
    qualityChangeCount += 1;
    lastChangeAt = now();
    lastReason = String(reason || 'runtime-frame-budget');
    overBudgetWindows = 0;
    underBudgetWindows = 0;
    return true;
  }

  function setPhase(nextPhase, reason) {
    const normalized = nextPhase === 'interaction' ? 'interaction' : 'settle';
    if (normalized === phase) return false;
    phase = normalized;
    revision += 1;
    lastReason = String(reason || normalized);
    return true;
  }

  function evaluate() {
    const current = metrics();
    const target = mobile ? 33 : 20;
    const expensive = current.p95FrameMs > target || current.p99FrameMs > Math.max(45, target * 1.7);
    const comfortable = current.p95FrameMs > 0 && current.p95FrameMs < target * 0.68
      && current.p99FrameMs < Math.max(28, target * 1.05);
    if (expensive) {
      overBudgetWindows += 1;
      underBudgetWindows = 0;
    } else if (comfortable) {
      underBudgetWindows += 1;
      overBudgetWindows = 0;
    } else {
      overBudgetWindows = 0;
      underBudgetWindows = 0;
    }
    if (now() - lastChangeAt < changeCooldownMs) return false;
    const index = TIER_ORDER.indexOf(tier);
    if (overBudgetWindows >= degradeWindows && index > 0) return changeTier(TIER_ORDER[index - 1], 'frame-p95-over-budget');
    if (phase === 'settle' && underBudgetWindows >= upgradeWindows && index < maximumTierIndex) {
      return changeTier(TIER_ORDER[index + 1], 'frame-budget-stable');
    }
    return false;
  }

  function recordFrame(durationMs, { interaction = phase === 'interaction' } = {}) {
    const duration = Math.max(0, Number(durationMs) || 0);
    if (!duration) return false;
    frameSamples.push(duration);
    if (frameSamples.length > 120) frameSamples.splice(0, frameSamples.length - 120);
    if (duration >= 45) longFrameCount += 1;
    samplesSinceEvaluation += 1;
    if (samplesSinceEvaluation < Math.max(4, Number(evaluationWindow) || 20)) return false;
    samplesSinceEvaluation = 0;
    // Interaction ownership belongs to beginInteraction/endInteraction. A frame
    // sample must not silently mutate the phase without a matching revision.
    void interaction;
    return evaluate();
  }

  return Object.freeze({
    beginInteraction: reason => setPhase('interaction', reason || 'interaction'),
    endInteraction: reason => setPhase('settle', reason || 'settle'),
    recordFrame,
    setTier: (nextTier, reason = 'manual') => changeTier(nextTier, reason),
    profile,
    stats: () => Object.freeze({
      ...profile(),
      sampleCount: frameSamples.length,
      overBudgetWindows,
      underBudgetWindows,
      qualityChangeCount,
      maximumTier: TIER_ORDER[maximumTierIndex],
    }),
  });
}
