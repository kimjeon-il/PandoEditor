import test from 'node:test';
import assert from 'node:assert/strict';
import { createGpuUploadScheduler } from '../../assets/js/modules/gpu-upload-scheduler.js';
import { prepareMeshSpatialBlocks, visibleSpatialBlockRanges } from '../../assets/js/modules/mesh-spatial-blocks.js';

function clock() {
  const frames = []; let time = 0;
  const scheduler = createGpuUploadScheduler({ requestFrame: fn => (frames.push(fn), frames.length), cancelFrame: () => {}, now: () => time, isHidden: () => false, isInputPending: () => false });
  return { scheduler, frames, tick(ms) { time += ms; frames.shift()?.(); }, advance(ms) { time += ms; } };
}
test('upload keys deduplicate and only complete after all chunks', async () => {
  const c = clock(); let chunks = 0;
  const job = { key: 'mesh', step: ({ byteBudget }) => { assert.ok(byteBudget <= 256 * 1024); return { bytes: byteBudget, done: ++chunks === 3 }; } };
  const promise = c.scheduler.enqueueUpload(job);
  assert.equal(c.scheduler.enqueueUpload(job), promise);
  c.tick(16); assert.equal(chunks, 1); c.tick(16); c.tick(16); await promise;
  assert.equal(c.scheduler.getStats().completed, 1);
});
test('input holds uploads until 500ms after release', async () => {
  const c = clock(); let calls = 0;
  const done = c.scheduler.enqueueUpload({ key: 'mesh', step: () => ({ done: ++calls > 0 }) });
  c.scheduler.noteInput(true); c.tick(1000); assert.equal(calls, 0);
  c.scheduler.noteInput(false); c.tick(499); assert.equal(calls, 0); c.tick(1); await done; assert.equal(calls, 1);
});
test('shared time budget stops another consumer after an atomic overrun', async () => {
  const c = clock(), seen = [];
  const a = c.scheduler.enqueueUpload({ key: 'terrain', priority: 10, step: () => { seen.push('terrain'); c.advance(3); return { done: true, bytes: 100 }; } });
  const b = c.scheduler.enqueueUpload({ key: 'hydro', step: () => { seen.push('hydro'); return { done: true }; } });
  c.tick(16); assert.deepEqual(seen, ['terrain']); assert.equal(c.scheduler.getStats().overrunCount, 1);
  c.tick(16); await Promise.all([a, b]); assert.deepEqual(seen, ['terrain', 'hydro']);
});
test('generation cancellation disposes staging and rejects rather than committing', async () => {
  const c = clock(); let disposed = false;
  const p = c.scheduler.enqueueUpload({ key: 'mesh', projectGeneration: 7, step: () => assert.fail('stale upload'), dispose: () => { disposed = true; } });
  c.scheduler.cancelGeneration(7); await assert.rejects(p, { name: 'AbortError' }); assert.equal(disposed, true);
});

function mesh() {
  return { positions: new Int32Array([0, 0, 1000000, 0, 0, 1000000, 179000000, 0, 180000000, 0, 179000000, 1000000]), triangleIndices: new Uint32Array([0, 1, 2, 3, 4, 5]), lineIndices: new Uint32Array([0, 1, 3, 4]), countryTriangleRanges: new Uint32Array([0, 3, 3, 3]), countryBoundaryRanges: new Uint32Array([0, 2, 2, 2]) };
}
test('spatial preparation preserves primitive winding, coordinates and country ranges', () => {
  const m = mesh(), before = structuredClone(m); prepareMeshSpatialBlocks(m);
  assert.deepEqual(m.positions, before.positions); assert.deepEqual(m.triangleIndices, before.triangleIndices);
  assert.deepEqual(m.countryTriangleRanges, before.countryTriangleRanges);
  assert.ok(m.spatialBlocks.triangle.bounds instanceof Float64Array);
});
test('spatial blocks reject rear hemisphere while retaining the front geometry', () => {
  const m = prepareMeshSpatialBlocks(mesh());
  const ranges = visibleSpatialBlockRanges(m, { mode: 0, cssViewport: [400, 400], cssTranslate: [200, 200], cssScale: 100, rowX: [0, 1, 0], rowY: [0, 0, -1], rowZ: [1, 0, 0] }, 'triangle', 0);
  assert.deepEqual(ranges, [{ first: 0, count: 3 }]);
});
test('invalid block ranges fall back rather than submitting invalid indices', () => {
  const m = prepareMeshSpatialBlocks(mesh()); m.spatialBlocks.triangle.ranges[1] = 100;
  assert.equal(visibleSpatialBlockRanges(m, { mode: 0 }), null);
});
