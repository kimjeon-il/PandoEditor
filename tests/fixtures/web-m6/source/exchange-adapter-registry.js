export const EXCHANGE_TARGETS = Object.freeze({
  PROJECT: 'project',
  COUNTRY: 'country',
  SUBUNIT: 'subunit',
  REGION: 'region',
  DISTRIBUTION: 'distribution',
  GENERIC: 'generic',
});

const text = value => String(value ?? '').trim();
const TARGET_ALIASES = Object.freeze({ admin: EXCHANGE_TARGETS.SUBUNIT, administrative: EXCHANGE_TARGETS.SUBUNIT, territory: EXCHANGE_TARGETS.SUBUNIT });

export const EXCHANGE_TARGET_DESCRIPTORS = Object.freeze({
  [EXCHANGE_TARGETS.PROJECT]: Object.freeze({ target: EXCHANGE_TARGETS.PROJECT, domain: 'project', replaceOnly: true, fallback: false }),
  [EXCHANGE_TARGETS.COUNTRY]: Object.freeze({ target: EXCHANGE_TARGETS.COUNTRY, domain: 'territorial', replaceOnly: false, fallback: false }),
  [EXCHANGE_TARGETS.SUBUNIT]: Object.freeze({ target: EXCHANGE_TARGETS.SUBUNIT, domain: 'territorial', replaceOnly: false, fallback: false }),
  [EXCHANGE_TARGETS.REGION]: Object.freeze({ target: EXCHANGE_TARGETS.REGION, domain: 'territorial', replaceOnly: false, fallback: false }),
  [EXCHANGE_TARGETS.DISTRIBUTION]: Object.freeze({ target: EXCHANGE_TARGETS.DISTRIBUTION, domain: 'distribution', replaceOnly: false, fallback: false }),
  [EXCHANGE_TARGETS.GENERIC]: Object.freeze({ target: EXCHANGE_TARGETS.GENERIC, domain: 'generic', replaceOnly: false, fallback: true }),
});

export function normalizeExchangeTarget(value, fallback = EXCHANGE_TARGETS.GENERIC) {
  const raw = text(value);
  const target = TARGET_ALIASES[raw] || raw;
  return EXCHANGE_TARGET_DESCRIPTORS[target] ? target : fallback;
}

export function exchangeTargetDescriptor(value) {
  return EXCHANGE_TARGET_DESCRIPTORS[normalizeExchangeTarget(value)] || null;
}

export function exchangeDomainForTarget(value) {
  return exchangeTargetDescriptor(value)?.domain || '';
}

function normalizeAdapter(target, adapter = {}) {
  const normalizedTarget = normalizeExchangeTarget(target, '');
  if (!normalizedTarget) throw new TypeError(`Unknown exchange target: ${target}`);
  const importPayload = adapter.importPayload || adapter.import;
  const exportPayload = adapter.exportPayload || adapter.export;
  if (importPayload != null && typeof importPayload !== 'function') throw new TypeError(`${normalizedTarget} import adapter must be a function`);
  if (exportPayload != null && typeof exportPayload !== 'function') throw new TypeError(`${normalizedTarget} export adapter must be a function`);
  return Object.freeze({
    target: normalizedTarget,
    descriptor: EXCHANGE_TARGET_DESCRIPTORS[normalizedTarget],
    importPayload: importPayload || null,
    exportPayload: exportPayload || null,
  });
}

export function createExchangeAdapterRegistry({ adapters = {} } = {}) {
  const registry = new Map();

  function register(target, adapter) {
    const normalized = normalizeAdapter(target, adapter);
    registry.set(normalized.target, normalized);
    return normalized;
  }

  for (const [target, adapter] of Object.entries(adapters || {})) register(target, adapter);

  function requireAdapter(target, capability) {
    const normalizedTarget = normalizeExchangeTarget(target, '');
    const adapter = registry.get(normalizedTarget);
    if (!adapter || typeof adapter[capability] !== 'function') {
      const error = new Error(`${normalizedTarget || target} exchange adapter does not support ${capability}.`);
      error.code = 'PL-EXCHANGE-UNSUPPORTED';
      throw error;
    }
    return adapter;
  }

  return Object.freeze({
    register,
    get: target => registry.get(normalizeExchangeTarget(target, '')) || null,
    has: target => registry.has(normalizeExchangeTarget(target, '')),
    targets: () => [...registry.keys()],
    importPayload(target, payload, context = {}) {
      return requireAdapter(target, 'importPayload').importPayload(payload, context);
    },
    exportPayload(target, value, context = {}) {
      return requireAdapter(target, 'exportPayload').exportPayload(value, context);
    },
  });
}
