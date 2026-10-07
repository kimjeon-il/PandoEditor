import { isDeepStrictEqual } from 'node:util';
import { sha256 } from './provenance.mjs';

export function expectedCaseIds(registry, selected) {
  return registry.features.filter(feature => selected.includes(feature.id)).flatMap(feature => feature.cases.flatMap(row =>
    row.observationIds ? row.observationIds.map(id => row.id + '/' + id) : [row.id]));
}

export function validateApprovedReport(report, pin) {
  if (typeof report.registryText !== 'string' || sha256(report.registryText) !== pin.sha256?.['tests/fixtures/portability/index.json']) throw new Error('Report registry is not the approved registry');
  if (report.contract?.commit !== pin.webContractCommit) throw new Error('Report contract commit is not approved');
  const files = new Map((report.contract?.files ?? []).map(row => [row.path, row.sha256]));
  for (const [path, hash] of Object.entries(pin.sha256)) if (files.get(path) !== hash) throw new Error('Report contract hash is not approved: ' + path);
  const registry = JSON.parse(report.registryText);
  validateReport(report);
  if (!Array.isArray(report.selected) || new Set(report.selected).size !== report.selected.length || report.selected.some(id => !registry.features.some(row => row.id === id))) throw new Error('Invalid selected features');
  if (!isDeepStrictEqual(expectedCaseIds(registry, report.selected).sort(), report.cases.map(row => row.id).sort())) throw new Error('Registry case accounting mismatch');
  for (const row of report.cases) {
    const feature = registry.features.find(feature => feature.id === row.featureId);
    const definition = feature?.cases.find(item => item.id === row.definitionId);
    if (!definition || !isDeepStrictEqual(row.axes, definition.axes)) throw new Error('Case is assigned to unapproved observation axes');
    const allowedIds = definition.observationIds?.map(id => definition.id + '/' + id) ?? [definition.id];
    if (!allowedIds.includes(row.id)) throw new Error('Observation ID belongs to another definition');
    const kind = definition.observationIds ? 'current' : definition.adapter === 'historical' ? 'historical' : 'supporting';
    if (row.kind !== kind) throw new Error('Case evidence kind does not match the registry');
  }
  const matrix = summarizeFeatures(registry, report.cases, report.selected);
  if (!isDeepStrictEqual(report.matrix, matrix)) throw new Error('Matrix does not match approved registry observations');
  return registry;
}

export function summarizeFeatures(registry, cases, selected) {
  return registry.features.map(feature => ({ id: feature.id, title: feature.title, selected: selected.includes(feature.id),
    axes: Object.fromEntries(['availability', 'rules', 'data', 'state', 'interaction'].map(axis => {
      if (!feature.axes.includes(axis)) return [axis, { status: 'N/A', reason: feature.notApplicable?.[axis]??'Missing N/A justification in registry' }];
      const rows = cases.filter(row => row.featureId === feature.id && row.axes.includes(axis));
      const paired = rows.filter(row => row.kind === 'current');
      const failure = rows.find(row => ['FAIL', 'ERROR'].includes(row.status));
      const missing = rows.find(row => row.kind !== 'historical' && ['NOT_RUN', 'UNSUPPORTED'].includes(row.status));
      const status = failure?.status ?? missing?.status ?? (paired.length ? (paired.find(row => row.status !== 'PASS')?.status ?? 'PASS') : 'NOT_RUN');
      return [axis, { status, evidence: rows.map(row => row.id),
        ...(paired.length ? {} : { reason: 'No complete current Web/App observations for this axis; independent or historical tests do not establish parity.' }) }];
    })) }));
}

export function markdownReport(report) {
  const lines = ['# Web / App Parity', '', `Web: ${report.sources.web.head}`, `App: ${report.sources.app.head}`, '',
    `Regression: **${report.gates.regression}**; behavioral parity: **${report.gates.behavioral}**.`, '',
    `Executed checks: ${report.checks.length}; current observations: ${report.cases.filter(row => row.kind === 'current').length}.`, '',
    '| Feature | Availability | Rules | Data | State | Input/display |', '| --- | --- | --- | --- | --- | --- |'];
  for (const row of report.matrix) lines.push(`| ${row.id} | ${Object.values(row.axes).map(axis => axis.status).join(' | ')} |`);
  lines.push('', '## Differences and missing evidence', '');
  for (const row of report.cases.filter(row => row.status !== 'PASS')) {
    lines.push(`- ${row.id}: ${row.status}${row.reason ? ` — ${row.reason}` : ''}`);
    for (const difference of row.differences ?? []) lines.push(`  - ${difference.path}: ${JSON.stringify(difference)}`);
  }
  for (const problem of report.problems) lines.push(`- ${problem}`);
  return lines.join('\n') + '\n';
}

export function validateReport(report) {
  if (report.schema !== 'web-app-parity-report' || report.version !== 1) throw new Error('Unsupported parity report');
  const ids = report.cases.map(row => row.id);
  if (!ids.length || new Set(ids).size !== ids.length) throw new Error('Missing or duplicate case results');
  if (!Array.isArray(report.expectedCaseIds) || !isDeepStrictEqual([...report.expectedCaseIds].sort(), [...ids].sort())) throw new Error('Incomplete case accounting');
  if (!['web','app'].every(side => /^[a-f0-9]{40}$/.test(report.sources?.[side]?.head ?? '')) || !report.contract?.files?.length) throw new Error('Missing source provenance');
  if (!report.provenanceVerified) throw new Error('Unverified source/build provenance');
  if (!Array.isArray(report.matrix) || !report.matrix.length) throw new Error('Missing parity matrix');
  const statuses = new Set(['PASS','FAIL','KNOWN_DIFFERENCE','UNSUPPORTED','NOT_RUN','ERROR','N/A']);
  for (const row of report.cases) if (!statuses.has(row.status)) throw new Error('Invalid case status');
  const featureIds = report.matrix.map(row => row.id);
  if (new Set(featureIds).size !== featureIds.length) throw new Error('Duplicate feature results');
  for (const row of report.matrix) {
    if (!row.id || !row.axes || Object.keys(row.axes).length !== 5) throw new Error('Incomplete matrix axes');
    for (const axis of ['availability','rules','data','state','interaction']) {
      const result = row.axes[axis];
      if (!statuses.has(result?.status)) throw new Error('Invalid matrix status');
      if (result.status === 'N/A' && !result.reason) throw new Error('N/A requires a reason');
      if (result.status === 'PASS' && (!result.evidence?.length || result.evidence.some(id => !ids.includes(id)))) throw new Error('Matrix PASS without evidence');
      if (result.status === 'PASS' && !report.cases.some(item => item.featureId === row.id && item.kind === 'current' && item.axes?.includes(axis) && item.status === 'PASS')) throw new Error('Matrix PASS without a current comparison');
    }
  }
  return report;
}
