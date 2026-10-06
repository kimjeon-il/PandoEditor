import test from 'node:test';
import assert from 'node:assert/strict';
import { createGpuTerrainPreparation } from '../../assets/js/modules/gpu-terrain-preparation.js';
import { createMapVisualFrame } from '../../assets/js/modules/map-visual-frame.js';

function fixture(t, onUnusable = () => {}, { mobile = false, geoDistance = () => 0, tintUrl } = {}) {
  const requests = [];
  const jobs = [];
  const deleted = [];
  const gl = { createTexture: () => ({}), deleteTexture: value => deleted.push(value), createBuffer: () => ({}) };
  for (const name of ['bindTexture', 'pixelStorei', 'texParameteri', 'texImage2D', 'bindBuffer', 'bufferData', 'deleteBuffer']) gl[name] = () => {};
  t.mock.method(globalThis, 'fetch', (url, options) => new Promise((resolve, reject) => {
    requests.push({ url, options, resolve, reject });
    options.signal.addEventListener('abort', () => reject(Object.assign(new Error('aborted'), { name: 'AbortError' })), { once: true });
  }));
  const owner = createGpuTerrainPreparation({
    tileUrl: spec => `https://example.test/${spec.key}`,
    tintUrl,
    onUnusable,
    isMobile: () => mobile,
    invalidate: () => {},
    geoDistance,
  });
  owner.setContext({ gl, ready: true, projectGeneration: 1, contextGeneration: 1,
    scheduler: { enqueueUpload: job => { jobs.push(job); return Promise.resolve(); } } });
  return { owner, requests, jobs, deleted };
}
const settle = async () => { for (let i = 0; i < 12; i++) await Promise.resolve(); };

async function primeBase({ owner, requests, jobs }, frame, view) {
  const previousBitmap = globalThis.createImageBitmap;
  globalThis.createImageBitmap = async () => ({ width: 1024, height: 1024, close() {} });
  try {
    owner.prepare(frame, view);
    for (const request of requests.filter(request => /\/0\//.test(request.url))) {
      request.resolve({ ok: true, blob: async () => ({}) });
      await settle();
      for (const job of jobs.splice(0)) while (!job.step().done) { /* Upload world base. */ }
    }
    owner.prepare(frame, view);
  } finally {
    if (previousBitmap) globalThis.createImageBitmap = previousBitmap;
    else delete globalThis.createImageBitmap;
  }
}

const mobileManifest = {
  representation: 'dem-relief-v1', gutter: 0,
  levels: [
    { id: 0, width: 2048, height: 1024, columns: 2, rows: 1, tileSize: 1024 },
    { id: 1, width: 4096, height: 2048, columns: 4, rows: 2, tileSize: 1024 },
    { id: 2, width: 8192, height: 4096, columns: 8, rows: 4, tileSize: 1024 },
  ],
};
const mobileFrame = (rotation = [0, 0]) => ({
  mode: 0, scale: 1200, dpr: 1, viewport: [390, 844],
  viewState: { projection: 'globe', rotation },
});
const mobileView = () => ({
  visible: true, physicalStyle: 'political', projection: 'globe',
  width: 390, height: 844, devicePixelRatio: 2,
  cacheBudgetBytes: 128 * 1024 * 1024,
});

function assertWorldCoverage(prepared) {
  for (let lat = -80; lat <= 80; lat += 20) for (let lon = -170; lon <= 170; lon += 20) {
    assert.ok(prepared.some(({ spec: { bounds: [west, north, east, south] } }) =>
      lon >= west && lon <= east && lat >= south && lat <= north), `uncovered terrain at ${lon},${lat}`);
  }
}

for (const representation of ['dem-relief-v1', 'raster-rgba-v1']) {
  test(`${representation} cannot publish fine coverage ahead of the complete world reserve`, async t => {
    const { owner, requests, jobs } = fixture(t);
    t.after(() => owner.dispose());
    globalThis.createImageBitmap = async () => ({ width: 1024, height: 1024, close() {} });
    t.after(() => { delete globalThis.createImageBitmap; });
    owner.setManifest({ ...mobileManifest, representation });
    const frame = { mode: 1, scale: 1200, dpr: 1, viewport: [1000, 800],
      viewState: { projection: 'flat', projectionCenter: [0, 0] } };
    const view = { ...mobileView(), projection: 'flat', flatCenter: [0, 0], devicePixelRatio: 1 };
    owner.prepare(frame, view);
    const upload = async request => {
      request.resolve({ ok: true, blob: async () => ({}) });
      await settle();
      for (const job of jobs.splice(0)) while (!job.step().done) { /* Real owner uploads. */ }
    };
    const fine = requests.filter(request => request.url.includes('/2/'));
    assert.equal(fine.length, owner.stats().terrainTargetTileCount);
    for (const request of fine) await upload(request);
    assert.deepEqual(owner.prepare(frame, view), [], 'complete current detail still needs a world reserve before publication');
    assert.equal(owner.stats().terrainTargetTilesLoaded, owner.stats().terrainTargetTileCount);
    await upload(requests[0]);
    assert.deepEqual(owner.prepare(frame, view), [], 'half a world reserve is insufficient');
    await upload(requests[1]);
    const ready = owner.prepare(frame, view);
    assert.equal(ready.length, fine.length);
    const distant = owner.prepare({ ...frame, viewState: { ...frame.viewState, projectionCenter: [160, 60] } },
      { ...view, flatCenter: [160, 60] });
    assertWorldCoverage(distant);
  });

  test(`${representation} starts with complete base coverage and retains it when rotating into uncached terrain`, async t => {
    const { owner, requests, jobs, deleted } = fixture(t, () => {}, { mobile: true });
    globalThis.createImageBitmap = async () => ({ width: 1024, height: 1024, close() {} });
    t.after(() => { delete globalThis.createImageBitmap; });
    t.after(() => owner.dispose());
    owner.setManifest({ ...mobileManifest, representation });
    const frame = { mode: 1, scale: 1200, dpr: 1, viewport: [1000, 800],
      viewState: { projection: 'flat', projectionCenter: [0, 0] } };
    const view = { ...mobileView(), projection: 'flat', flatCenter: [0, 0], devicePixelRatio: 1,
      cacheBudgetBytes: 8 * 1024 * 1024 };
    owner.prepare(frame, view);
    assert.deepEqual(requests.map(request => new URL(request.url).pathname), ['/0/0-0', '/0/1-0'],
      'world coverage must enter the mobile fetch slots before detail');
    const upload = async request => {
      request.resolve({ ok: true, blob: async () => ({}) });
      await settle();
      for (const job of jobs.splice(0)) while (!job.step().done) { /* Real owner GPU uploads. */ }
    };
    await upload(requests[0]);
    assert.equal(owner.prepare(frame, view).length, 0, 'do not publish half of the initial base');
    await upload(requests[1]);
    const base = owner.prepare(frame, view);
    assertWorldCoverage(base);
    const textures = base.map(tile => tile.texture);
    const detail = requests.find(request => request.url.includes('/2/'));
    await upload(detail);
    assertWorldCoverage(owner.prepare(frame, view));
    for (const style of ['political', 'physical']) {
      const distant = owner.prepare({ ...frame, viewState: { ...frame.viewState, projectionCenter: [160, 60] } },
        { ...view, physicalStyle: style, flatCenter: [160, 60] });
      assertWorldCoverage(distant);
      assert.ok(distant.some(tile => textures.includes(tile.texture)));
    }
    assert.ok(textures.every(texture => !deleted.includes(texture)), 'base survives cache pressure and a changed view');
  });
}

test('country preview cannot cap terrain detail requested by a high-DPR camera', async t => {
  const setup = fixture(t, () => {}, { mobile: true });
  const { owner, requests } = setup;
  owner.setManifest(mobileManifest);
  await primeBase(setup, mobileFrame(), mobileView());
  assert.equal(owner.stats().terrainLevel, 2);
  assert.equal(owner.stats().terrainFetchConcurrency, 2);
  assert.ok(requests.length > 0);
  const detail = requests.slice(2);
  assert.ok(detail.length > 0);
  assert.ok(detail.every(request => new URL(request.url).pathname.startsWith('/2/')));
  owner.dispose();
});

test('terrain camera LOD uses the visual frame DPR when the backing canvas lowers its DPR', t => {
  const { owner } = fixture(t, () => {}, { mobile: true });
  t.after(() => owner.dispose());
  owner.setManifest({ ...mobileManifest, levels: [
    { id: 0, width: 1350, height: 675, columns: 2, rows: 1, tileSize: 1024 },
    { id: 1, width: 2700, height: 1350, columns: 3, rows: 2, tileSize: 1024 },
    { id: 2, width: 5400, height: 2700, columns: 6, rows: 3, tileSize: 1024 },
  ] });
  for (const dpr of [2, 1.5, 1]) {
    const frame = createMapVisualFrame({ frameId: 1, viewRevision: 1,
      viewState: { projection: 'globe', dpr, scale: 177.45, translate: [195, 409],
        size: { width: 390, height: 844 }, rotation: [-15, -25, 0] } });
    owner.prepare(frame, mobileView());
    assert.equal(owner.stats().terrainLevel, 1, '177.45 CSS px and source DPR 2 require 2700px, independent of backing canvas');
  }
});

for (const failDetail of [false, true]) {
test(`exhausted base${failDetail ? ' and detail' : ''} failures report unusable coverage instead of waiting forever`, async t => {
  const failures = [];
  const { owner, requests, jobs } = fixture(t, reason => failures.push(reason));
  t.after(() => owner.dispose());
  globalThis.createImageBitmap = async () => ({ width: 1024, height: 1024, close() {} });
  t.after(() => { delete globalThis.createImageBitmap; });
  t.mock.timers.enable({ apis: ['setTimeout'] });
  let now = 0;
  t.mock.method(performance, 'now', () => now);
  owner.setManifest(mobileManifest);
  const frame = mobileFrame(), view = mobileView();
  owner.prepare(frame, view);
  const unavailable = ['/0/0-0'];
  if (failDetail) unavailable.push(new URL(requests.find(request => request.url.includes('/2/')).url).pathname);
  const failedRequests = new Set(), responded = new Set();
  for (let attempt = 0; attempt < 4; attempt++) {
    for (let index = 0; index < requests.length; index++) {
      const request = requests[index];
      if (responded.has(request)) continue;
      responded.add(request);
      if (unavailable.includes(new URL(request.url).pathname)) {
        failedRequests.add(request);
        request.resolve({ ok: false, status: 503 });
      } else request.resolve({ ok: true, blob: async () => ({}) });
      await settle();
      for (const job of jobs.splice(0)) while (!job.step().done) { /* Upload usable coverage. */ }
    }
    await settle();
    owner.prepare(frame, view);
    if (attempt < 3) { now += 5000; t.mock.timers.tick(5000); await settle(); }
  }
  assert.equal(owner.prepare(frame, view).length, 0);
  assert.equal(failedRequests.size, unavailable.length * 4, 'unavailable coverage paths exhaust their real retries');
  assert.equal(failures.length, 1, 'report through the existing source failure boundary');
  assert.match(failures[0], /0\/0-0: 지형 타일 HTTP 503/, 'preserve the failing tile and technical cause');
  owner.prepare(frame, view);
  assert.equal(failures.length, 1, 'report once until the source resets');
});
}

test('after world base the camera requests target terrain directly without intermediate detail levels', async t => {
  const setup = fixture(t, () => {}, { mobile: true });
  const { owner, requests } = setup;
  owner.setManifest(mobileManifest);
  await primeBase(setup, mobileFrame(), mobileView());
  assert.equal(owner.stats().terrainLevel, 2);
  assert.ok(requests.length > 0);
  const detail = requests.slice(2);
  assert.ok(detail.length > 0);
  assert.ok(detail.every(request => new URL(request.url).pathname.startsWith('/2/')));
  owner.dispose();
});

test('complete world base stays protected while GPU-ready detail replaces visible regions', async t => {
  const { owner, requests, jobs, deleted } = fixture(t);
  globalThis.createImageBitmap = async () => ({ width: 1024, height: 1024, close() {} });
  t.after(() => { delete globalThis.createImageBitmap; owner.dispose(); });
  owner.setManifest(mobileManifest);
  const view = { ...mobileView(), devicePixelRatio: 1, projection: 'flat', flatCenter: [0, 0], cacheBudgetBytes: 8 * 1024 * 1024 };
  const frame = { mode: 1, scale: 100, dpr: 1, viewport: [5000, 5000], viewState: { projection: 'flat', projectionCenter: [0, 0] } };
  const drain = () => { for (const job of jobs.splice(0)) while (!job.step().done) { /* Real uploads. */ } };
  owner.prepare(frame, view);
  for (const request of requests) request.resolve({ ok: true, blob: async () => ({}) });
  await settle(); drain();
  const coarse = owner.prepare(frame, view);
  assert.deepEqual(coarse.map(tile => tile.spec.key), ['0/0-0', '0/1-0']);
  const detailedFrame = { ...frame, scale: 500 };
  assert.equal(owner.prepare(detailedFrame, view).length, 2);
  // Upload the four western target tiles. Eastern downloads remain stalled.
  for (const request of requests.filter(request => /\/1\/[01]-[01]$/.test(request.url))) {
    request.resolve({ ok: true, blob: async () => ({}) });
    await settle(); drain();
  }
  const partial = owner.prepare(detailedFrame, view);
  assert.equal(owner.stats().terrainTargetTilesLoaded, 4);
  assert.equal(owner.stats().terrainTargetTileCount, 8);
  assert.deepEqual(partial.filter(tile => tile.spec.level === 0).map(tile => tile.spec.key), ['0/0-0', '0/1-0']);
  assertWorldCoverage(partial);
  owner.request({ key: 'pressure', pixelWidth: 1024, pixelHeight: 1024 });
  requests.find(request => request.url.endsWith('/pressure')).resolve({ ok: true, blob: async () => ({}) });
  await settle(); drain();
  assert.ok(!deleted.includes(coarse[0].texture), 'world coverage reserve survives even where current detail is ready');
  assert.ok(!deleted.includes(coarse[1].texture), 'fallback still drawing eastern coverage must survive cache pressure');
  assert.ok(owner.prepare(detailedFrame, view).some(tile => tile.texture === coarse[1].texture));
});

test('zoom keeps loaded terrain until replacement tiles finish decoding and GPU upload', async t => {
  const setup = fixture(t, () => {}, { mobile: true });
  const { owner, requests, jobs } = setup;
  globalThis.createImageBitmap = async () => ({ width: 1024, height: 1024, close() {} });
  t.after(() => { delete globalThis.createImageBitmap; });
  owner.setManifest(mobileManifest);
  const previousFrame = { ...mobileFrame(), scale: 250 };
  await primeBase(setup, previousFrame, mobileView());
  for (let index = 2; index < requests.length; index++) {
    requests[index].resolve({ ok: true, blob: async () => ({}) });
    await settle();
    for (const job of jobs.splice(0)) while (!job.step().done) { /* Drain the real upload owner. */ }
  }
  const previous = owner.prepare(previousFrame, mobileView());
  assert.equal(previous.length, 8);
  assert.ok(previous.every(tile => tile.spec.level === 1));
  assert.equal(owner.stats().terrainRenderedLevel, 1);
  const priorRequestCount = requests.length;
  const promoted = owner.prepare(mobileFrame(), mobileView());
  assertWorldCoverage(promoted);
  assert.ok(previous.every(old => promoted.some(tile => tile.texture === old.texture)));
  assert.equal(owner.stats().terrainRenderedLevel, 1);
  assert.equal(owner.stats().terrainLevel, 2);
  await settle();
  assert.ok(requests.slice(priorRequestCount).every(request => new URL(request.url).pathname.startsWith('/2/')));
  requests[priorRequestCount].resolve({ ok: true, blob: async () => ({}) });
  await settle();
  const decoded = owner.prepare(mobileFrame(), mobileView());
  assert.deepEqual(decoded.map(tile => tile.spec.key), promoted.map(tile => tile.spec.key), 'decoding alone must not replace fallback coverage');
  for (const job of jobs.splice(0)) while (!job.step().done) { /* Drain the real upload owner. */ }
  const ready = owner.prepare(mobileFrame(), mobileView());
  assert.equal(ready.length, decoded.length + 1, 'partial detail must paint over the retained terrain');
  assert.equal(ready.at(-1).spec.level, 2);
  assertWorldCoverage(ready);
  assert.equal(owner.stats().terrainRenderedLevel, 2);
  for (let index = priorRequestCount + 1; index < requests.length; index++) {
    requests[index].resolve({ ok: true, blob: async () => ({}) });
    await settle();
    for (const job of jobs.splice(0)) while (!job.step().done) { /* Drain target uploads. */ }
  }
  const complete = owner.prepare(mobileFrame(), mobileView());
  assert.equal(owner.stats().terrainTargetTilesLoaded, owner.stats().terrainTargetTileCount);
  assert.ok(complete.length > 0);
  assert.ok(complete.every(tile => tile.spec.level === 2), 'ready target tiles replace the retained terrain');
  owner.dispose();
});

test('rotating the mobile globe drops queued tiles from the old view', async t => {
  const longitudeDistance = (left, right) => Math.abs((((left[0] - right[0]) + 540) % 360) - 180) * Math.PI / 180;
  const setup = fixture(t, () => {}, { mobile: true, geoDistance: longitudeDistance });
  const { owner, requests, jobs } = setup;
  globalThis.createImageBitmap = async () => ({ width: 1024, height: 1024, close() {} });
  t.after(() => { delete globalThis.createImageBitmap; });
  owner.setManifest(mobileManifest);
  await primeBase(setup, mobileFrame([-90, 0]), mobileView());
  owner.prepare(mobileFrame([90, 0]), mobileView());
  await settle();
  assert.ok(requests.slice(2, 4).every(request => request.options.signal.aborted));
  for (let index = 4; index < 12; index += 1) {
    assert.ok(requests[index], `request ${index} must have started`);
    requests[index].resolve({ ok: true, blob: async () => ({}) });
    await settle();
    for (const job of jobs.splice(0)) job.step();
  }
  const laterPaths = requests.slice(4).map(request => new URL(request.url).pathname);
  assert.ok(laterPaths.some(path => /^\/2\/[0123]-/.test(path)), 'new-view detail must start');
  assert.deepEqual(laterPaths.filter(path => /^\/2\/[567]-/.test(path)), [], 'old-view queued tiles must not start');
  owner.dispose();
});

test('a new viewport starts its requests without waiting for obsolete in-flight downloads', async t => {
  const longitudeDistance = (left, right) => Math.abs((((left[0] - right[0]) + 540) % 360) - 180) * Math.PI / 180;
  const setup = fixture(t, () => {}, { mobile: true, geoDistance: longitudeDistance });
  const { owner, requests } = setup;
  owner.setManifest(mobileManifest);
  await primeBase(setup, mobileFrame([-90, 0]), mobileView());
  assert.equal(requests.length, 4);
  owner.prepare(mobileFrame([90, 0]), mobileView());
  await settle();
  assert.ok(requests.length >= 6, 'new-view downloads must start while old responses remain unresolved');
  assert.ok(requests.slice(2, 4).every(request => request.options.signal.aborted));
  assert.ok(requests.slice(0, 2).every(request => !request.options.signal.aborted), 'base is retained across views');
  assert.equal(owner.stats().terrainFailureCount, 0, 'view cancellation is not a failed tile');
  owner.dispose();
});

test('a request batch starts center tiles before viewport-edge tiles', t => {
  const { owner, requests } = fixture(t);
  owner.setManifest(mobileManifest);
  const frame = { mode: 1, scale: 1200, dpr: 1, viewport: [3000, 2000], viewState: { projection: 'flat', projectionCenter: [0, 0] } };
  owner.prepare(frame, { ...mobileView(), devicePixelRatio: 1, projection: 'flat', flatCenter: [0, 0] });
  assert.deepEqual(requests.slice(0, 2).map(request => new URL(request.url).pathname), ['/0/0-0', '/0/1-0']);
  const first = requests.slice(2, 6).map(request => new URL(request.url).pathname).sort();
  assert.deepEqual(first, ['/2/3-1', '/2/3-2', '/2/4-1', '/2/4-2']);
  owner.dispose();
});

test('desktop starts six visible downloads while missing current tiles; mobile remains at two', t => {
  const { owner, requests } = fixture(t);
  owner.setManifest(mobileManifest);
  owner.prepare({ mode: 1, scale: 1200, dpr: 1, viewport: [3000, 2000],
    viewState: { projection: 'flat', projectionCenter: [0, 0] } },
  { ...mobileView(), devicePixelRatio: 1, projection: 'flat', flatCenter: [0, 0] });
  assert.equal(owner.stats().terrainFetchConcurrency, 6);
  assert.equal(requests.length, 6);
  owner.dispose();
  const mobile = fixture(t, () => {}, { mobile: true });
  mobile.owner.setManifest(mobileManifest);
  mobile.owner.prepare(mobileFrame(), mobileView());
  assert.equal(mobile.owner.stats().terrainFetchConcurrency, 2);
  assert.equal(mobile.requests.length, 2);
  mobile.owner.dispose();
});

test('in-flight neighbouring prefetch cannot block queued tiles in the current viewport', async t => {
  const setup = fixture(t);
  const { owner, requests, jobs } = setup;
  globalThis.createImageBitmap = async () => ({ width: 1024, height: 1024, close() {} });
  t.after(() => { delete globalThis.createImageBitmap; });
  owner.setManifest(mobileManifest);
  const frame = { mode: 1, scale: 1200, dpr: 1, viewport: [1000, 800], viewState: { projection: 'flat', projectionCenter: [0, 0] } };
  const view = { ...mobileView(), devicePixelRatio: 1, projection: 'flat', flatCenter: [0, 0] };
  await primeBase(setup, frame, view);
  for (let index = 2; index < 6; index++) {
    requests[index].resolve({ ok: true, blob: async () => ({}) });
    await settle();
    for (const job of jobs.splice(0)) while (!job.step().done) { /* Drain uploads. */ }
  }
  const before = requests.length;
  assert.equal(before, 12, 'complete base and four visible tiles followed by six neighbouring downloads');
  owner.prepare({ ...frame, viewState: { ...frame.viewState, projectionCenter: [0, 30] } }, view);
  await settle();
  assert.ok(requests.length >= before + 2, 'both missing visible tiles must start before neighbouring responses finish');
  assert.equal(owner.stats().terrainFailureCount, 0);
  owner.dispose();
});

for (const [previousLevel, nextLevel, nextScale] of [[1, 2, 1200], [2, 1, 500]]) {
  test(`zoom transition ${previousLevel} to ${nextLevel} preserves loaded detail until replacement arrives`, async t => {
    const { owner, requests, jobs } = fixture(t);
    globalThis.createImageBitmap = async () => ({ width: 1024, height: 1024, close() {} });
    t.after(() => { delete globalThis.createImageBitmap; });
    owner.setManifest(mobileManifest);
    const view = { ...mobileView(), devicePixelRatio: 1 };
    const previousFrame = { ...mobileFrame(), scale: previousLevel === 1 ? 500 : 1200 };
    owner.prepare(previousFrame, view);
    for (let index = 0; index < requests.length; index++) {
      requests[index].resolve({ ok: true, blob: async () => ({}) });
      await settle();
      for (const job of jobs.splice(0)) while (!job.step().done) { /* Drain uploads. */ }
    }
    const before = owner.prepare(previousFrame, view);
    assert.ok(before.length > 0);
    assert.ok(before.every(tile => tile.spec.level === previousLevel));
    const priorRequestCount = requests.length;
    const nextFrame = { ...mobileFrame(), scale: nextScale };
    const waiting = owner.prepare(nextFrame, view);
    assert.equal(owner.stats().terrainLevel, nextLevel);
    assert.ok(waiting.length > 0, 'loaded detail must keep painting during the zoom transition');
    assert.ok(before.every(old => waiting.some(tile => tile.texture === old.texture)), 'previous detail is retained above the world base');
    assertWorldCoverage(waiting);
    assert.ok(requests.slice(priorRequestCount).every(request => new URL(request.url).pathname.startsWith(`/${nextLevel}/`)));
    requests[priorRequestCount].resolve({ ok: true, blob: async () => ({}) });
    await settle();
    for (const job of jobs.splice(0)) while (!job.step().done) { /* Drain uploads. */ }
    const partial = owner.prepare(nextFrame, view);
    assert.ok(partial.some(tile => tile.spec.level === previousLevel));
    assert.equal(partial.at(-1).spec.level, nextLevel, 'newly ready target tiles paint over retained detail');
    for (let index = priorRequestCount + 1; index < requests.length; index++) {
      requests[index].resolve({ ok: true, blob: async () => ({}) });
      await settle();
      for (const job of jobs.splice(0)) while (!job.step().done) { /* Drain uploads. */ }
    }
    const completed = owner.prepare(nextFrame, view);
    assert.ok(completed.every(tile => tile.spec.level === nextLevel));
    assert.equal(owner.stats().terrainTargetTilesLoaded, owner.stats().terrainTargetTileCount);
    owner.dispose();
  });
}

test('an already queued tile gains current-view priority instead of keeping its prefetch priority', async t => {
  const { owner, requests } = fixture(t, () => {}, { mobile: true });
  globalThis.createImageBitmap = async () => ({ width: 2, height: 2, close() {} });
  t.after(() => { delete globalThis.createImageBitmap; });
  for (const key of ['active-a', 'active-b']) owner.request({ key }, 30_000);
  owner.request({ key: 'promoted' }, 1_000);
  owner.request({ key: 'other' }, 15_000);
  owner.request({ key: 'promoted' }, 20_000);
  requests[0].resolve({ ok: true, blob: async () => ({}) });
  await settle();
  assert.equal(new URL(requests[2].url).pathname, '/promoted');
  owner.dispose();
});

test('project reset rejects old terrain failures without retry or replacement request corruption', async t => {
  const { owner, requests } = fixture(t);
  owner.request({ key: 'same', pixelWidth: 2, pixelHeight: 3 });
  owner.reset();
  owner.request({ key: 'same', pixelWidth: 2, pixelHeight: 3 });
  requests[0].reject(new Error('old project failure'));
  await settle();
  assert.equal(requests[0].options.signal.aborted, true);
  assert.equal(owner.stats().terrainTilesLoading, 1);
  assert.equal(owner.stats().terrainFailureCount, 0);
  owner.dispose();
});

test('context loss closes a late decoded bitmap and never queues it for upload', async t => {
  const { owner, requests, jobs } = fixture(t);
  let decode;
  let closed = 0;
  globalThis.createImageBitmap = () => new Promise(resolve => { decode = resolve; });
  t.after(() => { delete globalThis.createImageBitmap; });
  owner.request({ key: 'tile' });
  requests[0].resolve({ ok: true, blob: async () => ({}) });
  await settle();
  owner.reset();
  decode({ width: 2, height: 3, close: () => closed++ });
  await settle();
  assert.equal(closed, 1);
  assert.equal(jobs.length, 0);
  owner.dispose();
});

test('terrain upload measures the decoded bitmap before close and deduplicates requests', async t => {
  const { owner, requests, jobs, deleted } = fixture(t);
  const bitmap = { width: 8, height: 6, close() { this.width = 0; this.height = 0; } };
  globalThis.createImageBitmap = async () => bitmap;
  t.after(() => { delete globalThis.createImageBitmap; });
  owner.request({ key: 'tile' }); owner.request({ key: 'tile' });
  assert.equal(requests.length, 1);
  requests[0].resolve({ ok: true, blob: async () => ({}) });
  await settle();
  assert.equal(jobs.length, 1);
  assert.equal(jobs[0].step().bytes, 192);
  assert.equal(owner.stats().terrainCacheBytes, 192);
  assert.equal(bitmap.width, 0);
  owner.dispose(); owner.dispose();
  assert.equal(deleted.length, 1);
});

test('terrain cache evicts the least recently used unretained tile when the byte budget is exceeded', async t => {
  const { owner, requests, jobs, deleted } = fixture(t);
  owner.prepare(null, { cacheBudgetBytes: 8 * 1024 * 1024 });
  globalThis.createImageBitmap = async () => ({ width: 1024, height: 1024, close() {} });
  t.after(() => { delete globalThis.createImageBitmap; });
  for (let index = 0; index < 3; index++) {
    owner.request({ key: String(index) });
    requests[index].resolve({ ok: true, blob: async () => ({}) });
    await settle();
    jobs[index].step();
  }
  assert.equal(owner.stats().terrainCacheBytes, 8 * 1024 * 1024);
  assert.equal(owner.stats().terrainTilesLoaded, 2);
  assert.equal(deleted.length, 1);
  owner.dispose();
  assert.equal(deleted.length, 3);
});

test('DEM decoding never retries with color-managed bitmap defaults', async t => {
  const failures = [];
  const { owner, requests } = fixture(t, reason => failures.push(reason));
  owner.setManifest({ representation: 'dem-relief-v1', levels: [{ id: 0 }] });
  let decodeCalls = 0;
  globalThis.createImageBitmap = async () => { decodeCalls += 1; throw new Error('options unsupported'); };
  t.after(() => { delete globalThis.createImageBitmap; });
  owner.request({ key: 'dem', pixelWidth: 2, pixelHeight: 2 });
  requests[0].resolve({ ok: true, blob: async () => ({}) });
  await settle();
  assert.equal(decodeCalls, 1);
  assert.equal(owner.stats().terrainFailureCount, 1);
  assert.deepEqual(failures, ['options unsupported']);
  owner.dispose();
});

test('DEM tint upload is counted in the same budget and released on reset', async t => {
  const { owner, requests, jobs, deleted } = fixture(t);
  // The fixture has no tint URL. Exercise the same owner through the explicit
  // tint hook with a fresh instance instead of introducing a second cache.
  owner.dispose();
  const renderer = createGpuTerrainPreparation({
    tileUrl: () => '', tintUrl: () => 'https://example.test/tint.webp',
    isMobile: () => false, invalidate: () => {}, geoDistance: () => 0,
  });
  const gl = { createTexture: () => ({}), deleteTexture: value => deleted.push(value) };
  for (const name of ['bindTexture', 'pixelStorei', 'texParameteri', 'texImage2D']) gl[name] = () => {};
  renderer.setContext({ gl, ready: true, projectGeneration: 1, contextGeneration: 1,
    scheduler: { enqueueUpload: job => { jobs.push(job); return Promise.resolve(); } } });
  renderer.setManifest({ representation: 'dem-relief-v1', levels: [{ id: 0 }], tint: { width: 2, height: 2 } });
  globalThis.createImageBitmap = async () => ({ width: 2, height: 2, close() {} });
  t.after(() => { delete globalThis.createImageBitmap; });
  renderer.prepare(null, { visible: true, physicalStyle: 'political', cacheBudgetBytes: 32 * 1024 * 1024 });
  assert.equal(requests.length, 0, 'grayscale DEM must not decode unused tint');
  renderer.prepare(null, { visible: true, physicalStyle: 'physical', cacheBudgetBytes: 32 * 1024 * 1024 });
  requests[0].resolve({ ok: true, blob: async () => ({}) });
  await settle();
  jobs[0].step();
  assert.equal(renderer.stats().terrainCacheBytes, 16);
  assert.equal(renderer.stats().terrainTintReady, true);
  renderer.reset();
  assert.equal(renderer.stats().terrainCacheBytes, 0);
  assert.equal(deleted.length, 1);
  renderer.dispose();
});

test('DEM has a valid neutral tint sampler while color is delayed without changing cached height tiles', async t => {
  const { owner, requests, jobs, deleted } = fixture(t, () => {}, { tintUrl: () => 'https://example.test/tint.webp' });
  t.after(() => owner.dispose());
  globalThis.createImageBitmap = async () => ({ width: 1024, height: 1024, close() {} });
  t.after(() => { delete globalThis.createImageBitmap; });
  owner.setManifest({ ...mobileManifest, tint: { width: 1024, height: 1024 } });
  const frame = { ...mobileFrame(), scale: 100 };
  const view = { ...mobileView(), devicePixelRatio: 1 };
  owner.prepare(frame, view);
  for (const request of requests) request.resolve({ ok: true, blob: async () => ({}) });
  await settle();
  for (const job of jobs.splice(0)) while (!job.step().done) { /* Upload heights. */ }
  const gray = owner.prepare(frame, view);
  assert.equal(gray.length, 2);
  const neutralTint = owner.tintTexture();
  assert.ok(neutralTint, 'physical shader needs a complete sampler before the color image arrives');
  assert.equal(owner.stats().terrainTintReady, false);
  const uploads = owner.stats().terrainUploadCount;
  const colored = owner.prepare(frame, { ...view, physicalStyle: 'physical' });
  assert.deepEqual(colored.map(tile => tile.texture), gray.map(tile => tile.texture));
  assert.equal(owner.stats().terrainUploadCount, uploads, 'style switch must not reupload heights');
  assert.equal(requests.filter(request => request.url.endsWith('/tint.webp')).length, 1);
  requests.find(request => request.url.endsWith('/tint.webp')).resolve({ ok: true, blob: async () => ({}) });
  await settle();
  for (const job of jobs.splice(0)) while (!job.step().done) { /* Upload tint. */ }
  assert.equal(owner.stats().terrainTintReady, true);
  assert.notEqual(owner.tintTexture(), neutralTint);
  assert.ok(deleted.includes(neutralTint));
  assert.deepEqual(owner.prepare(frame, view).map(tile => tile.texture), gray.map(tile => tile.texture));
});
