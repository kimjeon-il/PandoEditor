/* Shared by foreground packs and background cache writes in the hydro Worker. */
'use strict';
function createHydroShardStore({ fetchResponse, openCache, resolveUrl, gunzip, digest }) {
  const memory = new Map();
  const queues = new Map();
  const recoveries = new Map();
  function serial(spec, action) {
    const previous = queues.get(spec.id) || Promise.resolve();
    const result = previous.catch(() => {}).then(action);
    queues.set(spec.id, result);
    result.finally(() => { if (queues.get(spec.id) === result) queues.delete(spec.id); }).catch(() => {});
    return result;
  }
  function invalid(message, diagnostic) {
    return Object.assign(new Error(message), { corrupt: true, diagnostic });
  }
  function remember(spec, bytes) {
    memory.set(spec.id, bytes);
    if (memory.size > 2) memory.delete(memory.keys().next().value);
    return bytes;
  }
  async function validate(spec, bytes, diagnostic) {
    if (bytes.length !== spec.bytes) throw invalid('수계 파일 길이가 일치하지 않습니다.', { ...diagnostic, stage: 'length', actualLength: bytes.length });
    const hash = await digest(bytes);
    if (hash !== spec.sha256) throw invalid('수계 파일 해시가 일치하지 않습니다.', { ...diagnostic, stage: 'hash', actualLength: bytes.length });
    return bytes;
  }
  async function save(spec, bytes) {
    const cache = await openCache();
    if (cache) {
      try { await cache.put(resolveUrl(spec.url), new Response(bytes, { headers: { 'Content-Type': 'application/octet-stream' } })); }
      catch (_) { /* Verified data remains available without persistent storage. */ }
    }
    return remember(spec, bytes);
  }
  async function cached(spec) {
    if (memory.has(spec.id)) return { bytes: memory.get(spec.id), source: 'memory' };
    const cache = await openCache();
    let response;
    try { response = await cache?.match(resolveUrl(spec.url)); } catch (_) { return null; }
    if (!response) return null;
    const bytes = new Uint8Array(await response.arrayBuffer());
    await validate(spec, bytes, { shardId: spec.id, source: 'cache' });
    return { bytes: remember(spec, bytes), source: 'cache' };
  }
  async function downloadFull(spec, recovery = false, signal) {
    const diagnostic = { shardId: spec.id, source: recovery ? 'recovery' : 'network-full', recovery };
    const response = await fetchResponse(resolveUrl(spec.url), { cache: 'no-store', signal }, recovery ? { attempts: 1, signal } : { signal });
    diagnostic.status = response.status;
    diagnostic.contentRange = response.headers.get('Content-Range');
    if (response.status !== 200) throw invalid('수계 전체 파일 응답이 올바르지 않습니다.', { ...diagnostic, stage: 'response' });
    const bytes = new Uint8Array(await response.arrayBuffer());
    await validate(spec, bytes, diagnostic);
    return save(spec, bytes);
  }
  async function recover(spec, error) {
    if (recoveries.has(spec.id)) {
      const previous = recoveries.get(spec.id);
      if (previous.error) throw previous.error;
      throw Object.assign(error, { retryable: false });
    }
    const record = {};
    recoveries.set(spec.id, record);
    memory.delete(spec.id);
    const cache = await openCache();
    try { await cache?.delete(resolveUrl(spec.url)); } catch (_) { /* Memory recovery still works. */ }
    try { return await downloadFull(spec, true); }
    catch (failure) {
      failure.retryable = false;
      failure.diagnostic = { ...error.diagnostic, ...failure.diagnostic, recovery: true, stage: failure.diagnostic?.stage || 'recovery-download' };
      record.error = failure;
      throw failure;
    }
  }
  function decode(spec, rows, bytes, base, source, responseDiagnostic = {}) {
    return rows.map(row => {
      const diagnostic = { ...responseDiagnostic, shardId: spec.id, packId: row.id, start: row.offset, end: row.offset + row.length - 1, actualLength: bytes.length, source, stage: 'gzip', recovery: source === 'recovery' };
      const start = row.offset - base;
      if (start < 0 || start + row.length > bytes.length) throw invalid('수계 pack 범위가 올바르지 않습니다.', diagnostic);
      try { return [row.id, gunzip(bytes.subarray(start, start + row.length))]; }
      catch (error) { throw invalid(error.message || 'invalid gzip data', diagnostic); }
    });
  }
  return {
    read(spec, rows, start, end) {
      return serial(spec, async () => {
        try {
          if (recoveries.get(spec.id)?.error) throw recoveries.get(spec.id).error;
          const hit = await cached(spec);
          if (hit) return decode(spec, rows, hit.bytes, 0, hit.source);
          const response = await fetchResponse(resolveUrl(spec.url), { headers: { Range: `bytes=${start}-${end - 1}` } });
          const diagnostic = { shardId: spec.id, packId: rows[0]?.id, start, end: end - 1, source: 'network-range', status: response.status, contentRange: response.headers.get('Content-Range'), stage: 'range' };
          if (!response.ok) throw Object.assign(new Error(`수계 HTTP ${response.status}`), { diagnostic });
          const bytes = new Uint8Array(await response.arrayBuffer());
          diagnostic.actualLength = bytes.length;
          if (response.status === 206) {
            if (diagnostic.contentRange !== `bytes ${start}-${end - 1}/${spec.bytes}` || bytes.length !== end - start) throw invalid('수계 부분 응답 범위가 일치하지 않습니다.', diagnostic);
            return decode(spec, rows, bytes, start, 'network-range', diagnostic);
          }
          if (response.status !== 200) throw invalid('수계 응답 상태가 올바르지 않습니다.', diagnostic);
          await validate(spec, bytes, diagnostic);
          await save(spec, bytes);
          return decode(spec, rows, bytes, 0, 'network-full', diagnostic);
        } catch (error) {
          if (!error.corrupt || error.retryable === false) throw error;
          const bytes = await recover(spec, error);
          try { return decode(spec, rows, bytes, 0, 'recovery'); }
          catch (failure) {
            failure.retryable = false;
            recoveries.get(spec.id).error = failure;
            memory.delete(spec.id);
            const cache = await openCache();
            try { await cache?.delete(resolveUrl(spec.url)); } catch (_) {}
            throw failure;
          }
        }
      });
    },
    full(spec, signal) {
      return serial(spec, async () => {
        if (recoveries.get(spec.id)?.error) throw recoveries.get(spec.id).error;
        try { return (await cached(spec))?.bytes || await downloadFull(spec, false, signal); }
        catch (error) {
          if (!error.corrupt) throw error;
          return recover(spec, error);
        }
      });
    },
    retry() { recoveries.clear(); },
  };
}
self.createHydroShardStore = createHydroShardStore;
