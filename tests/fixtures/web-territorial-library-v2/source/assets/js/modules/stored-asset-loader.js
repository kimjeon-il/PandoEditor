// Shared stored-byte cache, integrity and decoding owner for packed world and catalog assets.
export function createStoredAssetLoader({dataRevision, resolveUrl, report = () => {}, fetchFn = globalThis.fetch, cacheStorage = globalThis.caches}) {
 const DATA_CACHE_PREFIX = 'pandolab-data-';
 const DATA_CACHE_NAME = DATA_CACHE_PREFIX + dataRevision;
 const LEGACY_CORE_CACHE_PREFIX = 'pandolab-core-';
 let dataCachePromise = null;
 let oldCacheCleanupPromise = null;
async function openDataCache() {
  if (!cacheStorage) return null;
  if (!dataCachePromise) dataCachePromise = cacheStorage.open(DATA_CACHE_NAME).catch(() => null);
  return dataCachePromise;
}

async function cleanupOldCoreCaches() {
  if (!cacheStorage) return;
  if (!oldCacheCleanupPromise) {
    oldCacheCleanupPromise = cacheStorage.keys()
      .then(names => Promise.all(names
      .filter(name => (name.startsWith(DATA_CACHE_PREFIX) && name !== DATA_CACHE_NAME)
        || name.startsWith(LEGACY_CORE_CACHE_PREFIX))
        .map(name => cacheStorage.delete(name))))
      .catch(() => []);
  }
  await oldCacheCleanupPromise;
}

async function cacheStoredBuffer(cache, url, storedBuffer, headers = null) {
  if (!cache) return false;
  try {
    await cache.put(url, new Response(storedBuffer, { headers: headers || undefined }));
    return true;
  } catch (_) {
    await cleanupOldCoreCaches();
    try {
      await cache.put(url, new Response(storedBuffer, { headers: headers || undefined }));
      return true;
    } catch (_) {
      return false;
    }
  }
}

function countedStream(stream, onChunk) {
  if (!stream || typeof TransformStream !== 'function') return stream;
  return stream.pipeThrough(new TransformStream({
    transform(chunk, controller) {
      onChunk(chunk.byteLength);
      controller.enqueue(chunk);
    },
  }));
}

async function consumeStoredResponse(response, spec, phase, key, label, source) {
  const startedAt = performance.now();
  let storedBytes = 0;
  const expectedStored = Number(spec.compressedBytes || 0);
  if (response.body && typeof TransformStream === 'function') {
    const stream = countedStream(response.body, length => {
      storedBytes += length;
      report(phase, key, `${label}: ${source === 'cache' ? '저장된 데이터를 읽는' : '다운로드하는'} 중입니다.`, storedBytes, expectedStored);
    });
    const storedBuffer = await new Response(stream).arrayBuffer();
    return {
      storedBuffer,
      source,
      transferredBytes: source === 'network' ? storedBytes : 0,
      storedBytes,
      readMs: performance.now() - startedAt,
    };
  }

  const storedBuffer = await response.arrayBuffer();
  storedBytes = storedBuffer.byteLength;
  return {
    storedBuffer,
    source,
    transferredBytes: source === 'network' ? storedBytes : 0,
    storedBytes,
    readMs: performance.now() - startedAt,
  };
}

async function decodeStoredBuffer(storedBuffer, spec, phase, key, label) {
  const startedAt = performance.now();
  let buffer = storedBuffer;
  if (spec.encoding === 'gzip') {
    if (typeof DecompressionStream !== 'function') throw new Error(`이 브라우저는 ${label} 압축 해제를 지원하지 않습니다.`);
    report(phase, `${key}-decode`, `${label}: 압축을 해제하는 중입니다.`);
    const decoded = new Blob([storedBuffer]).stream().pipeThrough(new DecompressionStream('gzip'));
    buffer = await new Response(decoded).arrayBuffer();
  }
  return {
    buffer,
    decodedBytes: buffer.byteLength,
    decompressMs: performance.now() - startedAt,
  };
}

function resolveAssetUrl(spec) { return resolveUrl(spec); }

async function validateStoredAsset(storedBuffer, spec, label) {
  const expectedBytes = Number(spec.compressedBytes || 0);
  if (expectedBytes > 0 && storedBuffer.byteLength !== expectedBytes) throw new Error(`${label} 저장 크기가 올바르지 않습니다.`);
  const expectedHash = String(spec.sha256 || '').toLowerCase();
  if (!expectedHash) return;
  if (!globalThis.crypto?.subtle) throw new Error(`${label} 무결성을 확인할 수 없습니다.`);
  const digest = await globalThis.crypto.subtle.digest('SHA-256', storedBuffer);
  const actualHash = [...new Uint8Array(digest)].map(value => value.toString(16).padStart(2, '0')).join('');
  if (actualHash !== expectedHash) throw new Error(`${label} 무결성 검증에 실패했습니다.`);
}

function validateAssetLength(result, spec, label) {
  const expected = Number(spec.decodedBytes || 0);
  if (expected <= 0 || result.buffer.byteLength === expected) return;
  if (spec.encoding === 'identity') {
    const normalized = new TextDecoder().decode(result.buffer).replaceAll('\r\n', '\n');
    if (new TextEncoder().encode(normalized).byteLength === expected) return;
  }
  throw new Error(`${label} 압축 해제 크기가 올바르지 않습니다.`);
}

async function loadAsset(spec, phase, key, label, validate, signal = null) {
  const url = resolveAssetUrl(spec);
  const cache = await openDataCache();
  if (cache) {
    const cached = await cache.match(url).catch(() => null);
    if (cached) {
      try {
        const read = await consumeStoredResponse(cached, spec, phase, key, label, 'cache');
        await validateStoredAsset(read.storedBuffer, spec, label);
        const decoded = await decodeStoredBuffer(read.storedBuffer, spec, phase, key, label);
        const result = {
          ...read,
          ...decoded,
          cacheWriteMs: 0,
          milliseconds: read.readMs + decoded.decompressMs,
        };
        validateAssetLength(result, spec, label);
        const value = await validate(result.buffer, label);
        report(phase, key, `${label}: 저장된 데이터를 확인했습니다.`, Number(spec.compressedBytes || result.storedBytes), Number(spec.compressedBytes || result.storedBytes), true, { source: 'cache' });
        return { ...result, value, cacheHit: true };
      } catch (_) {
        await cache.delete(url).catch(() => false);
        report(phase, `${key}-cache-repair`, `${label}: 손상된 저장 데이터를 지우고 다시 받습니다.`, 1, 1, true, { source: 'cache', recovered: true });
      }
    }
  }

  let lastError = null;
  for (let attempt = 1; attempt <= 3; attempt += 1) {
    try {
      const response = await fetchFn(url, { cache: 'default', signal });
      if (!response.ok) throw new Error(`${label} 요청에 실패했습니다. (${response.status})`);
      const responseHeaders = response.headers;
      const read = await consumeStoredResponse(response, spec, phase, key, label, 'network');
      await validateStoredAsset(read.storedBuffer, spec, label);
      const cacheWriteStartedAt = performance.now();
      report(phase, `${key}-cache-write`, `${label}: 다운로드한 데이터를 저장하는 중입니다.`, 0, 1);
      const cached = await cacheStoredBuffer(cache, url, read.storedBuffer, responseHeaders);
      const cacheWriteMs = performance.now() - cacheWriteStartedAt;
      report(phase, `${key}-cache-write`, `${label}: 저장을 마쳤습니다.`, 1, 1, true, { cached });
      const decoded = await decodeStoredBuffer(read.storedBuffer, spec, phase, key, label);
      const result = {
        ...read,
        ...decoded,
        cacheWriteMs,
        milliseconds: read.readMs + cacheWriteMs + decoded.decompressMs,
      };
      validateAssetLength(result, spec, label);
      const value = await validate(result.buffer, label);
      report(phase, key, `${label}: 다운로드를 완료했습니다.`, Number(spec.compressedBytes || result.storedBytes), Number(spec.compressedBytes || result.storedBytes), true, { source: 'network', cached, attempt });
      return { ...result, value, cacheHit: false };
    } catch (error) {
      lastError = error;
      if (error?.name === 'AbortError') throw error;
      if (cache) await cache.delete(url).catch(() => false);
      if (attempt < 3) {
        report(phase, `${key}-retry`, `${label} 준비를 다시 시도합니다. (${attempt + 1}/3)`, attempt, 3, false, { attempt });
        await new Promise(resolve => setTimeout(resolve, 500 * 2 ** (attempt - 1)));
      }
    }
  }
  throw lastError || new Error(`${label}을 준비하지 못했습니다.`);
}


return Object.freeze({loadAsset, cleanupOldCoreCaches});
}
