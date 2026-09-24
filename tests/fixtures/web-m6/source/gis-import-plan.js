const GIS_IMPORT_PLAN_VERSION = 1;

const PLAN_KINDS = new Set(['project-replace', 'country-merge', 'territorial', 'generic', 'distribution']);

const clone = value => {
  if (value === undefined || value === null) return value;
  if (typeof structuredClone === 'function') return structuredClone(value);
  return JSON.parse(JSON.stringify(value));
};

const freeze = value => {
  if (!value || typeof value !== 'object' || Object.isFrozen(value)) return value;
  for (const item of Object.values(value)) freeze(item);
  return Object.freeze(value);
};

export function createGisImportPlan({
  id,
  kind,
  projectGeneration = 0,
  source = {},
  payload = {},
  affectedIds = [],
  render = {},
  summary = {},
} = {}) {
  if (!PLAN_KINDS.has(kind)) throw new TypeError(`지원하지 않는 GIS import plan입니다: ${kind}`);
  const plan = {
    version: GIS_IMPORT_PLAN_VERSION,
    id: String(id || `gis-import:${Date.now()}:${Math.random().toString(36).slice(2)}`),
    kind,
    projectGeneration: Number(projectGeneration || 0),
    source: {
      fileName: String(source.fileName || ''),
      sourceKind: String(source.sourceKind || ''),
    },
    payload: clone(payload),
    affectedIds: [...new Set((affectedIds || []).map(String).filter(Boolean))],
    render: { kind: String(render.kind || ''), domain: String(render.domain || '') },
    summary: clone(summary),
  };
  return freeze(plan);
}

export function assertCurrentGisImportPlan(plan, projectGeneration) {
  if (!plan || plan.version !== GIS_IMPORT_PLAN_VERSION || !PLAN_KINDS.has(plan.kind)) {
    const error = new TypeError('올바른 GIS import plan이 아닙니다.');
    error.code = 'PL-GIS-PLAN-001';
    throw error;
  }
  if (Number(plan.projectGeneration) !== Number(projectGeneration)) {
    const error = new Error('다른 프로젝트에서 만든 GIS import plan은 적용할 수 없습니다.');
    error.code = 'PL-GIS-STALE-PLAN-001';
    throw error;
  }
  return plan;
}
