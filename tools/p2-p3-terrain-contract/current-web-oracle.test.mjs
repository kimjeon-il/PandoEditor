import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { fixture, realGrid, visualFrame, settle } from './current-web-fixture.mjs';

const fixtureRoot = new URL('../../tests/fixtures/web-p2-p3-current-source/', import.meta.url);
test('all eight fixed-Web source blobs retain declared literal byte identities', async () => {
  const manifestBytes = await readFile(new URL('source-manifest.json', fixtureRoot));
  assert.equal(createHash('sha256').update(manifestBytes).digest('hex'), '1545469046c47808454f91dc9ff949f0cc005db0d6abb10235f6faff5034cfdd');
  const manifest = JSON.parse(manifestBytes.toString('utf8'));
  assert.equal(manifest.webCommit, 'ebcfae4d27b29cbbea6416a7045a4806930204be');
  assert.equal(manifest.files.length, 8);
  for (const entry of manifest.files) {
    const bytes = await readFile(new URL(`source/${entry.path}`, fixtureRoot));
    assert.equal(bytes.length, entry.bytes, entry.path);
    assert.equal(createHash('sha256').update(bytes).digest('hex'), entry.sha256, entry.path);
    assert.equal(createHash('sha1').update(Buffer.from(`blob ${bytes.length}\0`)).update(bytes).digest('hex'), entry.gitBlob, entry.path);
  }
});

test('pinned mobile detection is the window-layout max-width799 query, independent of touch/OS/map width', async () => {
  const environment = await readFile(new URL('source/assets/js/modules/app-environment.js', fixtureRoot), 'utf8');
  const surfaces = await readFile(new URL('source/assets/js/modules/app-workspace-surfaces.js', fixtureRoot), 'utf8');
  assert.match(environment, /mobile: window\.matchMedia\('\(max-width: 799px\)'\)/);
  assert.match(surfaces, /detectLayoutMode = \(\) => dependencies\.platformConfigurationB\.LAYOUT_QUERIES\.mobile\.matches \? 'mobile'/);
  assert.match(surfaces, /isMobile = \(\) => layoutMode === 'mobile'/);
});

for (const projection of ['flat', 'globe']) for (const [windowWidth, mobile, expected] of [[799, true, 1], [800, false, 2]]) {
  test(`${projection}: window${windowWidth}, sourceDPR3 selects literal L${expected} at177.45CSSpx`, t => {
    const f = fixture(t, { mobile });
    for (const dpr of [1, 1.5, 2, 3]) for (const meshQuality of ['preview', 'canonical']) {
      // Map panel intentionally remains390px while window/layout changes799→800.
      f.prepare(visualFrame({ projection, scale: 177.45, dpr, width: 390 }), { devicePixelRatio: 3, meshQuality });
      assert.equal(f.owner.stats().terrainLevel, expected);
    }
  });
}

for (const [scale, expected] of [[100, 0], [200, 1], [400, 2], [800, 3], [1600, 4], [3200, 5], [1000000, 5]]) {
  test(`real six-level grid: ${scale}CSSpx/sourceDPR1 → L${expected}`, t => {
    const f = fixture(t);
    f.prepare(visualFrame({ scale }), { devicePixelRatio: 1 });
    assert.equal(f.owner.stats().terrainLevel, expected);
  });
}

for (const projection of ['flat', 'globe']) {
  test(`${projection}: literal1.12LOD boundary straddles191.8385474768381CSSpx`, t => {
    const f = fixture(t);
    f.prepare(visualFrame({ projection, scale: 191.8385 }), { devicePixelRatio: 1 });
    assert.equal(f.owner.stats().terrainLevel, 0);
    f.prepare(visualFrame({ projection, scale: 191.8386 }), { devicePixelRatio: 1 });
    assert.equal(f.owner.stats().terrainLevel, 1);
  });
}

test('literal DEM base dimensions include real gutters and both tiles are required for first display', async t => {
  const f = fixture(t);
  const frame = visualFrame({ scale: 100, width: 5000, height: 5000 });
  assert.deepEqual(f.prepare(frame, { devicePixelRatio: 1 }), []);
  await f.decode('0/0-0');
  assert.equal(f.owner.stats().terrainPendingDecodedBytes, 1026 * 677 * 4);
  assert.deepEqual(f.prepare(frame, { devicePixelRatio: 1 }), [], 'CPU-ready is not GPU-ready');
  f.drain(); await settle();
  assert.deepEqual(f.prepare(frame, { devicePixelRatio: 1 }), [], 'one GPU-ready base tile is not the whole world');
  await f.decode('0/1-0');
  assert.equal(f.owner.stats().terrainPendingDecodedBytes, 328 * 677 * 4);
  assert.deepEqual(f.prepare(frame, { devicePixelRatio: 1 }), []);
  f.drain(); await settle();
  assert.deepEqual(f.prepare(frame, { devicePixelRatio: 1 }).map(row => row.spec.key), ['0/0-0', '0/1-0']);
  assert.equal(f.owner.stats().terrainCacheBytes, 3666632 + 4, 'height reserve plus neutral tint sampler');
});

test('uploaded fine geometry before both base tiles is not first-display authorization', async t => {
  const f = fixture(t), frame = visualFrame({ scale: 400, width: 390, height: 200 });
  f.prepare(frame, { devicePixelRatio: 1 });
  const fine = f.requests.find(row => row.key.startsWith('2/'));
  assert.ok(fine);
  await f.upload(fine.key);
  assert.deepEqual(f.prepare(frame, { devicePixelRatio: 1 }), []);
  assert.ok(f.owner.stats().terrainTargetTilesLoaded > 0);
  await f.upload('0/0-0');
  assert.deepEqual(f.prepare(frame, { devicePixelRatio: 1 }), []);
  await f.upload('0/1-0');
  const draw = f.prepare(frame, { devicePixelRatio: 1 });
  assert.ok(draw.some(row => row.spec.key === fine.key));
  assert.deepEqual(draw.filter(row => row.spec.level === 0).map(row => row.spec.key), ['0/0-0', '0/1-0']);
});

test('partial target uploads retain complete world reserve; CPU decode does not replace it', async t => {
  const f = fixture(t), frame = visualFrame({ scale: 400, width: 5000, height: 5000 });
  await f.primeBase(frame);
  const base = f.prepare(frame);
  assert.deepEqual(base.map(row => row.spec.key), ['0/0-0', '0/1-0']);
  const key = f.requests.find(row => row.key.startsWith('3/'))?.key;
  // sourceDPR2 and scale400 selectL3 directly; noL1/L2 sequential downloads.
  assert.ok(key);
  assert.ok(f.requests.every(row => /^(0|3)\//.test(row.key)));
  await f.decode(key);
  assert.deepEqual(f.prepare(frame).map(row => row.spec.key), ['0/0-0', '0/1-0']);
  f.drain(); await settle();
  const partial = f.prepare(frame, { cacheBudgetBytes: 4 });
  assert.deepEqual(partial.slice(0, 2).map(row => row.spec.key), ['0/0-0', '0/1-0']);
  assert.ok(partial.some(row => row.spec.key === key));
  assert.ok(base.every(row => !f.deleted.includes(row.texture)));
});

test('source reset closes late decoded data without current upload/cache insertion', async t => {
  const f = fixture(t), frame = visualFrame({ scale: 100 });
  f.prepare(frame);
  // Resolve fetch, reset epoch before its asynchronous decode continuation.
  const request = f.requests.find(row => row.key === '0/0-0');
  request.resolve({ ok: true, blob: async () => ({ key: request.key }) });
  f.owner.setContext({ ...f.context, projectGeneration: 2 });
  await settle();
  assert.deepEqual([...f.jobs.keys()], []);
  assert.equal(f.owner.stats().terrainTilesLoaded, 0);
  assert.equal(f.owner.stats().terrainPendingDecodedBytes, 0);
  assert.deepEqual(f.closed, ['0/0-0']);
});

test('real source grid literals preserve tile-count inventory1280 and baseRGBA3666632', () => {
  assert.deepEqual(realGrid.levels.map(level => level.columns * level.rows), [2, 6, 18, 66, 242, 946]);
  assert.equal(realGrid.levels.reduce((sum, level) => sum + level.columns * level.rows, 0), 1280);
  assert.equal((1026 + 328) * 677 * 4, 3666632);
  assert.equal(4096 * 2048 * 4, 33554432);
});
