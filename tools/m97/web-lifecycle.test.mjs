import assert from 'node:assert/strict';
import test from 'node:test';

// Contract: removing the real service call or snapshot/history integration must fail.
test('parent detach executes the production service and restores timeline state through Undo/Redo', async () => {
  const module = await import('./web-lifecycle.mjs').catch(error => {
    if (error.code === 'ERR_MODULE_NOT_FOUND') return {};
    throw error;
  });
  assert.equal(typeof module.runLifecycleCorpus, 'function', 'lifecycle runner must exist');
  const result = await module.runLifecycleCorpus();
  const row = result.cases.find(row => row.case === 'parent-detach');
  assert.ok(row, 'parent-detach observation exists');
  const before = row.stages.before.state;
  assert.deepEqual(row.stages.undo.state.document, before.document);
  assert.deepEqual(row.stages.redo.state.document, row.stages.confirm.state.document);
  assert.equal(row.stages.confirm.outcome.changed, true);
  assert.equal(row.stages.confirm.state.document.entities.find(entity => entity.id === 'child-a').parentId, '');
  assert.deepEqual(row.stages.confirm.state.document.entities.map(entity => entity.geometry), before.document.entities.map(entity => entity.geometry));
  assert.equal(row.stages.preview.observed, false);
  assert.equal(row.stages.cancel.observed, false);
});

// Contract: bypassing production preview discard/apply, reference rewrite, or history must fail.
test('sibling merge observes real worker preview, cancel, confirmation, reference rewrite, Undo and Redo', async () => {
  const { runLifecycleCorpus } = await import('./web-lifecycle.mjs');
  const corpus = await runLifecycleCorpus();
  const row = corpus.cases.find(row => row.case === 'sibling-merge');
  assert.ok(row, 'sibling-merge must be observed');
  for (const stage of ['before', 'preview', 'cancel', 'confirm', 'undo', 'redo']) assert.equal(row.stages[stage].observed, true);
  assert.deepEqual(row.stages.preview.state.document, row.stages.before.state.document);
  assert.ok(row.stages.preview.state.preview, 'production preview session exists');
  assert.deepEqual(row.stages.cancel.state, row.stages.before.state, 'actual discard leaves no canonical or history mutation');
  assert.equal(row.stages.confirm.state.preview, null);
  const document = row.stages.confirm.state.document;
  assert.ok(!document.entities.some(entity => entity.id === 'child-b'));
  assert.equal(document.entities.find(entity => entity.id === 'grandchild-b').parentId, 'child-a');
  assert.equal(document.distributionEntries.find(entry => entry.id === 'entry-b').territorialUnitId, 'child-a');
  assert.equal(Object.hasOwn(row.stages.confirm.state.presentation.labelSettings, 'territorial:child-b'), false);
  assert.deepEqual(row.stages.undo.state.document, row.stages.before.state.document);
  assert.deepEqual(row.stages.redo.state.document, document);
  assert.deepEqual(row.referenceEffects, { createdIds: [], deletedIds: ['child-b'], retainedIds: ['country-a', 'child-a', 'grandchild-b'], rewrittenDistributionEntryIds: ['entry-b'], reparentedIds: ['grandchild-b'] });
});

test('full sibling annex deletes donor references rather than rewriting distribution entries', async () => {
  const { runLifecycleCorpus } = await import('./web-lifecycle.mjs');
  const row = (await runLifecycleCorpus()).cases.find(row => row.case === 'sibling-annex-full');
  assert.ok(row, 'full sibling annex must be observed');
  assert.equal(row.stages.confirm.outcome.ok, true);
  assert.ok(!row.stages.confirm.state.document.entities.some(entity => entity.id === 'child-b'));
  assert.ok(!row.stages.confirm.state.document.distributionEntries.some(entry => entry.id === 'entry-b'));
  assert.equal(row.stages.confirm.state.document.entities.find(entity => entity.id === 'grandchild-b').parentId, 'child-a');
  assert.deepEqual(row.stages.undo.state.document, row.stages.before.state.document);
  assert.deepEqual(row.stages.redo.state.document, row.stages.confirm.state.document);
});

test('sibling create observes actual new object with cancel no-op and history restoration', async () => {
  const { runLifecycleCorpus } = await import('./web-lifecycle.mjs');
  const row = (await runLifecycleCorpus()).cases.find(row => row.case === 'sibling-create');
  assert.ok(row, 'sibling create must be observed');
  assert.equal(row.stages.confirm.outcome.ok, true);
  assert.deepEqual(row.referenceEffects.createdIds, ['child-new']);
  assert.deepEqual(row.referenceEffects.deletedIds, []);
  assert.deepEqual(row.stages.cancel.state, row.stages.before.state);
  assert.deepEqual(row.stages.undo.state.document, row.stages.before.state.document);
  assert.deepEqual(row.stages.redo.state.document, row.stages.confirm.state.document);
});

test('manifest must cover every executable source before import', async () => {
  const { verifyLifecycleSources } = await import('./web-lifecycle.mjs');
  const { root, manifest } = verifyLifecycleSources();
  const incomplete = structuredClone(manifest);
  incomplete.sources = incomplete.sources.filter(source => !source.path.endsWith('/territorial-service.js'));
  assert.throws(() => verifyLifecycleSources({ root, manifest: incomplete }), /unlisted executable source/i);
});

test('rejecting the real impact confirmation cancels merge without state or history changes', async () => {
  const { runLifecycleCorpus } = await import('./web-lifecycle.mjs');
  const row = (await runLifecycleCorpus()).cases.find(row => row.case === 'sibling-merge');
  assert.equal(row.additionalStages?.impactCancel?.observed, true, 'modal rejection must be observed');
  assert.equal(row.additionalStages.impactCancel.outcome.ok, false);
  assert.deepEqual(row.additionalStages.impactCancel.state, row.stages.before.state);
  assert.deepEqual(row.modalDecisions.map(decision => decision.decision), ['cancel', 'confirm']);
});

test('repeated production execution is deterministic and matches the committed observation corpus', async () => {
  const { readFileSync } = await import('node:fs');
  const { runLifecycleCorpus } = await import('./web-lifecycle.mjs');
  const first = await runLifecycleCorpus();
  const second = await runLifecycleCorpus();
  assert.deepEqual(first, second);
  const recorded = JSON.parse(readFileSync(new URL('../../tests/fixtures/web-m97/lifecycle-observations.json', import.meta.url)));
  assert.deepEqual(first, recorded, 'regenerate the corpus from the pinned production runtime');
});

test('tampered source bytes are rejected before runtime import or worker launch', async () => {
  const { mkdtempSync, cpSync, appendFileSync, rmSync } = await import('node:fs');
  const { tmpdir } = await import('node:os');
  const { join } = await import('node:path');
  const { verifyLifecycleSources, runLifecycleCorpus } = await import('./web-lifecycle.mjs');
  const { root, manifest } = verifyLifecycleSources();
  const temporary = mkdtempSync(join(tmpdir(), 'pando-lifecycle-tamper-'));
  try {
    cpSync(root, temporary, { recursive: true });
    appendFileSync(join(temporary, 'assets/js/modules/territorial-service.js'), '\n// altered\n');
    await assert.rejects(runLifecycleCorpus({ root: temporary, manifest }), /territorial-service.js: Source hash mismatch/);
  } finally { rmSync(temporary, { recursive: true, force: true }); }
});

test('actual history leaves deleted presentation settings deleted while document references return', async () => {
  const { runLifecycleCorpus } = await import('./web-lifecycle.mjs');
  const row = (await runLifecycleCorpus()).cases.find(row => row.case === 'sibling-merge');
  assert.equal(row.stages.before.state.presentation.itemVisibility.subunits['child-b'], false);
  assert.ok(row.stages.before.state.presentation.labelSettings['territorial:child-b']);
  assert.ok(row.stages.before.state.presentation.layerPresentation.objectStyles['territorial:entity:child-b']);
  for (const stage of ['confirm', 'undo', 'redo']) {
    const presentation = row.stages[stage].state.presentation;
    assert.equal(Object.hasOwn(presentation.itemVisibility.subunits, 'child-b'), false);
    assert.equal(Object.hasOwn(presentation.labelSettings, 'territorial:child-b'), false);
    assert.equal(Object.hasOwn(presentation.layerPresentation.objectStyles, 'territorial:entity:child-b'), false);
  }
  assert.equal(row.stages.undo.state.document.distributionEntries.find(entry => entry.id === 'entry-b').territorialUnitId, 'child-b');
});

test('calculation-only hook executes the actual cut worker without inventing lifecycle stages', async () => {
  const module = await import('./web-lifecycle.mjs');
  assert.equal(typeof module.runWorkerCalculation, 'function', 'worker calculation hook must exist');
  const source = { type: 'Polygon', coordinates: [[[0,0],[0,10],[10,10],[10,0],[0,0]]] };
  const observation = await module.runWorkerCalculation('territorial-cut', {
    source, sourceKey: 'simple-cut', coords: [[-1,5],[11,5]], buildPreview: true,
    view: { kind: 'flat', scale: 200, translate: [200,200], rotate: [0,0,0], center: [0,0], size: { width: 800, height: 600 }, coarsePointer: false, snapDistance: { mouse: 10, touch: 18 } },
  });
  assert.equal(observation.valid, true);
  assert.equal(observation.split.candidates.length, 2);
  assert.equal(Object.hasOwn(observation, 'stages'), false);
});
