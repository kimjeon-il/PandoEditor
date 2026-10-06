// Synthetic resource adapter for exact, pinned Web production code. No native/GPU proof.
import { createGpuTerrainPreparation } from '../../tests/fixtures/web-p2-p3-terrain-source/source/assets/js/modules/gpu-terrain-preparation.js';
import { createMapVisualFrame } from '../../tests/fixtures/web-p2-p3-terrain-source/source/assets/js/modules/map-visual-frame.js';

export const realGrid = Object.freeze({
  representation: 'dem-relief-v1', gutter: 1,
  tint: { width: 4096, height: 2048 },
  levels: [
    { id: 0, width: 1350, height: 675, columns: 2, rows: 1, tileSize: 1024 },
    { id: 1, width: 2700, height: 1350, columns: 3, rows: 2, tileSize: 1024 },
    { id: 2, width: 5400, height: 2700, columns: 6, rows: 3, tileSize: 1024 },
    { id: 3, width: 10800, height: 5400, columns: 11, rows: 6, tileSize: 1024 },
    { id: 4, width: 21600, height: 10800, columns: 22, rows: 11, tileSize: 1024 },
    { id: 5, width: 43200, height: 21600, columns: 43, rows: 22, tileSize: 1024 },
  ],
});
export const settle = async () => { for (let i = 0; i < 20; i++) await Promise.resolve(); };

export function visualFrame({ scale = 177.45, dpr = 1, projection = 'flat', center = [0, 0], width = 390, height = 844 } = {}) {
  return createMapVisualFrame({ frameId: 1, viewRevision: 1,
    viewState: { projection, dpr, scale, size: { width, height },
      translate: [width / 2, height / 2], projectionCenter: center,
      rotation: [-center[0], -center[1], 0] } });
}

export function fixture(t, { mobile = false, manifest = realGrid } = {}) {
  const requests = [], deleted = [], closed = [], invalidations = [], jobs = new Map();
  let nextTexture = 1;
  const gl = { createTexture: () => ({ syntheticTextureId: nextTexture++ }),
    deleteTexture: value => deleted.push(value), createBuffer: () => ({}) };
  for (const name of ['bindTexture', 'pixelStorei', 'texParameteri', 'texImage2D', 'bindBuffer', 'bufferData', 'deleteBuffer']) gl[name] = () => {};
  t.mock.method(globalThis, 'fetch', (url, options) => new Promise((resolve, reject) => {
    const request = { url, options, key: new URL(url).pathname.slice(1), answered: false,
      resolve(value) { request.answered = true; resolve(value); } };
    requests.push(request);
    options.signal.addEventListener('abort', () => reject(Object.assign(new Error('aborted'), { name: 'AbortError' })), { once: true });
  }));
  const previousBitmap = globalThis.createImageBitmap;
  globalThis.createImageBitmap = async ({ key }) => {
    if (key === 'tint.webp') return { ...manifest.tint, close: () => closed.push(key) };
    const [levelId, cell] = key.split('/'), [column, row] = cell.split('-').map(Number);
    const level = manifest.levels[Number(levelId)];
    return { width: Math.min(level.tileSize, level.width - column * level.tileSize) + 2 * manifest.gutter,
      height: Math.min(level.tileSize, level.height - row * level.tileSize) + 2 * manifest.gutter,
      close: () => closed.push(key) };
  };
  const scheduler = {
    enqueueUpload(input) {
      if (jobs.has(input.key)) return jobs.get(input.key).promise;
      const job = { ...input };
      job.promise = new Promise((resolve, reject) => { job.resolve = resolve; job.reject = reject; });
      jobs.set(job.key, job); return job.promise;
    },
    cancelKey(key) {
      const job = jobs.get(key); if (!job) return;
      jobs.delete(key); job.dispose?.(); job.reject(Object.assign(new Error('cancelled'), { name: 'AbortError' }));
    },
  };
  const owner = createGpuTerrainPreparation({ tileUrl: spec => `https://terrain-contract.invalid/${spec.key}`,
    tintUrl: () => 'https://terrain-contract.invalid/tint.webp', onUnusable: reason => invalidations.push(reason),
    isMobile: () => mobile, invalidate: reason => invalidations.push(reason),
    geoDistance(left, right) {
      const radians = Math.PI / 180;
      const a = left[1] * radians, b = right[1] * radians, longitude = (left[0] - right[0]) * radians;
      return Math.acos(Math.max(-1, Math.min(1, Math.sin(a) * Math.sin(b) + Math.cos(a) * Math.cos(b) * Math.cos(longitude))));
    } });
  const context = { gl, ready: true, projectGeneration: 1, contextGeneration: 1, scheduler };
  owner.setContext(context); owner.setManifest(manifest);
  const view = { visible: true, physicalStyle: 'political', devicePixelRatio: 2, cacheBudgetBytes: 128 * 1024 * 1024 };
  const prepare = (frame, overrides = {}) => owner.prepare(frame, { ...view, ...overrides });
  const drain = () => {
    for (const [key, job] of [...jobs]) {
      let count = 0;
      while (!job.step().done) if (++count > 2000) throw new Error('synthetic upload drain did not terminate');
      jobs.delete(key); job.resolve();
    }
  };
  const decode = async key => {
    const request = requests.find(row => row.key === key && !row.answered && !row.options.signal.aborted);
    if (!request) throw new Error(`missing active synthetic request ${key}`);
    request.resolve({ ok: true, blob: async () => ({ key }) }); await settle();
  };
  const upload = async key => { await decode(key); drain(); await settle(); };
  const primeBase = async frame => { prepare(frame); await upload('0/0-0'); await upload('0/1-0'); };
  t.after(async () => {
    owner.dispose(); await settle();
    if (previousBitmap === undefined) delete globalThis.createImageBitmap; else globalThis.createImageBitmap = previousBitmap;
  });
  return { owner, requests, deleted, closed, invalidations, jobs, context, prepare, drain, decode, upload, primeBase };
}
