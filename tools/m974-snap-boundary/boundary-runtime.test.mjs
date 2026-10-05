import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { loadNodeSources, readPinnedSources } from './sources.mjs';

const module = await import('./boundary-runtime.mjs').catch(error => {
  if (error.code !== 'ERR_MODULE_NOT_FOUND') throw error;
  return {};
});
const source = readPinnedSources().sources['assets/js/modules/app-domain-assembly.js'];

test('boundary callback extraction records the exact original UTF-8 source span and hash', async () => {
  assert.equal(typeof module.extractBoundaryCallbacks, 'function', 'Exact callback extraction is required');
  const extracted = await module.extractBoundaryCallbacks(source);
  assert.equal(extracted.source, Buffer.from(source).subarray(extracted.byteStart, extracted.byteEnd).toString());
  assert.equal(extracted.sha256, createHash('sha256').update(extracted.source).digest('hex'));
  assert.equal(extracted.sourceSha256, createHash('sha256').update(source).digest('hex'));
  assert.deepEqual(extracted.names, ['beginBoundaryGesture', 'moveBoundaryGesture', 'commitBoundaryGesture']);
  const callbacks = Function('dependencies', 'territorialEntityRepository', 'boundaryTouchesGeometry', `return ({${extracted.source}});`)({projectState:{state:{tool:'select'}}}, {}, () => false);
  assert.deepEqual(Object.keys(callbacks), extracted.names);
  assert.equal(callbacks.beginBoundaryGesture({ vertexKey: '1,1' }), false);
});

test('ambiguous or missing callback seams cannot be presented as pinned source evidence', async () => {
  assert.equal(typeof module.extractBoundaryCallbacks, 'function');
  await assert.rejects(module.extractBoundaryCallbacks(source + '\n        beginBoundaryGesture: event => {}'), /ambiguous|unique/i);
  await assert.rejects(module.extractBoundaryCallbacks(''), /missing|source|unique/i);
});

test('browser harness declares its dependencies without importing Node APIs', () => {
  assert.equal(typeof module.boundaryRuntimeSource, 'function');
  const runtimeSource = module.boundaryRuntimeSource();
  const entrypoints = Function(`return (${runtimeSource});`)();
  assert.equal(typeof entrypoints.runBoundaryCase, 'function');
  assert.equal(typeof entrypoints.boundaryCases, 'function');
  assert.ok(module.boundaryEntrypoints.includes('app-country-modes'));
  assert.ok(module.boundaryEntrypoints.includes('editing-domain'));
});

test('boundary corpus exercises topology, descendant effects, no-op and stale canonical workflows', async () => {
  const loaded = await loadNodeSources();
  try {
    const definitions = module.boundaryCases(loaded.api);
    for (const id of ['root-triple','root-two-fixed','root-uneven-multipolygon','child-triple','child-parent-fixed','root-disjoint-pairs','child-auto-seed','root-descendant-effects','child-descendant-effects','root-locked-touching','root-locked-unrelated','root-no-op','root-stale-preview']) assert.ok(definitions.some(row => row.id === id), `Missing ${id}`);
    for (const definition of definitions) {
      const actual = await module.runBoundaryCase(loaded, definition);
      const explain = `${definition.id}: ${JSON.stringify(actual.diagnostics)}`;
      for (const key of ['entry','gesture']) if (definition.expected[key] !== undefined) assert.equal(actual[key]?.ok, definition.expected[key], `${explain}: ${key}`);
      if (definition.expected.prepared !== undefined) assert.equal(actual.stages.prepared.outcome?.ok ?? false, definition.expected.prepared, `${explain}: prepared`);
      if (definition.expected.confirm !== undefined) assert.equal(actual.stages.confirm.outcome?.ok ?? false, definition.expected.confirm, `${explain}: confirm`);
      if (definition.expected.movedOwnerIds) assert.deepEqual([...actual.movedOwnerIds].sort(), [...definition.expected.movedOwnerIds].sort(), `${explain}: moved owners`);
      for (const key of ['reparentedIds','deletedIds']) if (definition.expected[key]) assert.deepEqual([...actual.referenceEffects[key]].sort(),[...definition.expected[key]].sort(),`${explain}: ${key}`);
      assert.ok(!actual.diagnostics.some(row => row.kind === 'error' && /undefined|not a function|ReferenceError|TypeError/.test(row.message)), `${explain}: unexpected harness wiring error`);
      assert.equal(actual.entry.path, definition.expectedPath, `${explain}: entry path`);
      if (actual.stages.confirm.outcome?.ok) {
        assert.deepEqual(actual.stages.confirm.state.history,{undo:1,redo:0},explain);
        assert.deepEqual(actual.stages.cancel.state.document,actual.stages.before.state.document,explain);
        assert.deepEqual(actual.stages.undo.state.document,actual.stages.before.state.document,explain);
        assert.deepEqual(actual.stages.redo.state.document,actual.stages.confirm.state.document,explain);
        const beforeById = new Map(actual.stages.before.state.document.entities.map(row=>[row.id,row]));
        const afterById = new Map(actual.stages.confirm.state.document.entities.map(row=>[row.id,row]));
        for (const id of definition.expected.clippedIds || []) assert.notDeepEqual(afterById.get(id).geometry,beforeById.get(id).geometry,`${explain}: ${id} must be clipped`);
        for (const id of definition.expected.unchangedIds || []) assert.deepEqual(afterById.get(id),beforeById.get(id),`${explain}: ${id} must remain unchanged`);
        if (definition.id === 'root-uneven-multipolygon') {
          const moved = actual.movedFeatures.find(row=>row.id==='A').geometry;
          const source = beforeById.get('A').geometry;
          assert.deepEqual(moved.coordinates[0][1],source.coordinates[0][1],'unrelated hole survives detached node movement exactly');
          assert.deepEqual(moved.coordinates[1],source.coordinates[1],'unrelated polygon component survives detached node movement exactly');
          const handle = actual.preparation.handles.find(row=>row.nodeKey==='1,1');
          assert.ok(handle.virtualRefs.some(row=>row.featureId==='A'));
          assert.ok(handle.refs.some(row=>row.featureId==='B'));
        }
        if (definition.impactCancel) {
          assert.equal(actual.stages.impactCancel.outcome.ok,false);
          assert.deepEqual(actual.stages.impactCancel.state.document,actual.stages.before.state.document,explain);
          assert.deepEqual(actual.stages.impactCancel.state.history,{undo:0,redo:0},explain);
          assert.deepEqual(actual.modalDecisions.map(row=>row.decision),['cancel','confirm']);
        }
      } else assert.deepEqual(actual.stages.settled.state.history,{undo:0,redo:0},explain);
    }
  } finally { loaded.cleanup(); }
});

const triple = api => {
  const polygon = ring => ({ type: 'Polygon', coordinates: [ring] });
  return [
    ['A', [[0,0],[1,0],[1,1],[1,2],[0,2],[0,0]]],
    ['B', [[1,0],[2,0],[2,1],[1,1],[1,0]]],
    ['C', [[1,1],[2,1],[2,2],[1,2],[1,1]]],
  ].map(([id, ring]) => api.createTerritorialFeature({ id, name: id, entityKind: 'general', parentId: '', coverageMode:'explicit', geometry:polygon(ring) }));
};

test('real worker shared drag stages three detached owners and publishes one reversible history step', async () => {
  const loaded = await loadNodeSources();
  try {
    const result = await module.runBoundaryCase(loaded, { id:'root-triple', features:triple(loaded.api), selectedIds:['A','B','C'], seedId:'A', move:{nodeKey:'1,1',coordinate:[1,1.1]},expectedPath:'root' });
    assert.equal(result.entry.ok, true);
    assert.equal(result.stages.cold.worker.ready, false);
    assert.equal(result.stages.pending.preparation.status, 'pending');
    assert.equal(result.preparation.status, 'ready', JSON.stringify({preparation:result.preparation,trace:result.workerTrace,diagnostics:result.diagnostics}));
    assert.equal(result.preparation.handles.find(row => row.nodeKey === '1,1').fixed, false);
    assert.deepEqual(result.preparation.handles.find(row => row.nodeKey === '1,1').ownerIds.sort(), ['A','B','C']);
    const { before, drag, preview, cancel, confirm, undo, redo } = result.stages;
    assert.equal(preview.outcome.ok, true);
    assert.deepEqual(drag.state.document, before.state.document);
    assert.deepEqual(preview.state.document, before.state.document);
    assert.deepEqual(cancel.state.document, before.state.document);
    assert.equal(cancel.state.preview, null);
    assert.equal(confirm.outcome.ok, true, JSON.stringify(result.diagnostics));
    assert.deepEqual(confirm.state.history, {undo:1,redo:0});
    assert.deepEqual(undo.state.document, before.state.document);
    assert.deepEqual(redo.state.document, confirm.state.document);
    assert.deepEqual(result.movedOwnerIds.sort(), ['A','B','C']);
    for (const operation of ['boundary-prepare','boundary-move','territorial-edit','territorial-validation']) assert.ok(result.workerTrace.some(row => row.direction === 'request' && row.operation === operation));
  } finally { loaded.cleanup(); }
});

test('the unselected third owner makes the triple-junction gesture unavailable', async () => {
  const loaded = await loadNodeSources();
  try {
    const result = await module.runBoundaryCase(loaded, { id:'root-fixed', features:triple(loaded.api), selectedIds:['A','B'], seedId:'A', move:{nodeKey:'1,1',coordinate:[1,1.1]},expectedPath:'root' });
    assert.equal(result.preparation.handles.find(row => row.nodeKey === '1,1').fixed, true);
    assert.equal(result.gesture.ok, false);
    assert.equal(result.stages.preview.observed, false);
    assert.equal(result.stages.confirm.observed, false);
    assert.ok(!result.workerTrace.some(row => row.operation === 'boundary-move'));
  } finally { loaded.cleanup(); }
});

test('actual delayed preparation and move replies cannot survive cancellation or generation changes', async () => {
  const loaded = await loadNodeSources();
  try {
    for (const scenario of ['pending-cancel','stale-preparation','stale-move']) {
      const result = await module.runBoundaryCase(loaded, { id:scenario, scenario, features:triple(loaded.api), selectedIds:['A','B','C'], seedId:'A', move:{nodeKey:'1,1',coordinate:[1,1.1]},expectedPath:'root' });
      assert.ok(result.workerTrace.some(row => row.direction === 'held-result'));
      assert.equal(result.stages.settled.state.preview, null);
      assert.deepEqual(result.stages.settled.state.document, result.stages.before.state.document);
      assert.deepEqual(result.stages.settled.state.history,{undo:0,redo:0});
      assert.equal(result.stages.confirm.observed, false);
    }
  } finally { loaded.cleanup(); }
});
