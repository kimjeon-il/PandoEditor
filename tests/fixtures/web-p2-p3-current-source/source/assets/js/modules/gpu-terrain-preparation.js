// Owns terrain requests, retries, decoded bitmaps, upload queue, textures and grids.
// prepare() returns ready draw records; drawing never initiates preparation.
export function createGpuTerrainPreparation({ tileUrl, tintUrl, onUnusable, isMobile, invalidate, geoDistance }) {
  const PI = Math.PI;
  let gl = null, uploadScheduler = null, ready = false, disposed = false;
  let epoch = 0, projectGeneration = 0, contextRevision = 0;
  let activeFrameContext = null, view = {};
  let terrainManifest = null, cacheBudgetBytes = 128 * 1024 * 1024, terrainUploadCount = 0;
  let tint = null, tintFallback = null, tintPending = false, pendingDecodedBytes = 0, unusableReported = false;
  const controllers = new Set(), retryTimers = new Set(), uploadKeys = new Set();
  const isWebGlRenderer = () => ready;
    const terrainTiles = new Map();
    const terrainTileRequests = new Map();
    const terrainFetchQueue = [];
    const terrainFetchQueuedEntries = new Map();
    let terrainActiveFetches = 0;
    const terrainTileFailures = new Map();
    const terrainTileQueuedKeys = new Set();
    const terrainUploadQueue = [];
    const terrainGridMeshes = new Map();
    let terrainLastLevel = -1;
    let terrainRenderedLevel = -1;
    let terrainTargetTileCount = 0;
    let terrainTargetTilesLoaded = 0;
    let terrainTargetTileKeys = new Set();
    let terrainRetentionKeys = new Set();
    function terrainLevelForView(frameContext = activeFrameContext) {
      if (!terrainManifest?.levels?.length) return null;
      const physicalScale = Number(frameContext?.scale) || Number(activeFrameContext?.scale) || 1;
      // The render canvas may lower its DPR under load, but that must not
      // choose a blurrier source terrain level for an unchanged map view.
      // Physical scale belongs to the shared frame, so divide by its own DPR.
      const frameDpr = Math.max(1, Number(frameContext.dpr));
      const sourceDpr = Math.min(isMobile() ? 2 : 3, Math.max(1, Number(view.devicePixelRatio || 1)));
      const desiredWidth = Math.max(1, 2 * PI * (physicalScale / frameDpr) * sourceDpr);
      return terrainManifest.levels.find(level => level.width >= desiredWidth * 1.12)
        || terrainManifest.levels[terrainManifest.levels.length - 1];
    }

    function terrainTileSpec(level, column, row) {
      const x0 = column * level.tileSize;
      const y0 = row * level.tileSize;
      const x1 = Math.min(level.width, x0 + level.tileSize);
      const y1 = Math.min(level.height, y0 + level.tileSize);
      return {
        key: `${level.id}/${column}-${row}`,
        level: level.id,
        column,
        row,
        pixelWidth: x1 - x0,
        pixelHeight: y1 - y0,
        bounds: [
          -180 + x0 / level.width * 360,
          90 - y0 / level.height * 180,
          -180 + x1 / level.width * 360,
          90 - y1 / level.height * 180,
        ],
      };
    }

    function terrainNeighbourSpecs(level, specs) {
      const output = [];
      const seen = new Set(specs.map(spec => spec.key));
      for (const spec of specs) {
        for (let row = Math.max(0, spec.row - 1); row <= Math.min(level.rows - 1, spec.row + 1); row += 1) {
          for (let column = Math.max(0, spec.column - 1); column <= Math.min(level.columns - 1, spec.column + 1); column += 1) {
            const neighbour = terrainTileSpec(level, column, row);
            if (seen.has(neighbour.key)) continue;
            seen.add(neighbour.key);
            output.push(neighbour);
          }
        }
      }
      return output;
    }

    function visibleTerrainTileSpecs(level, includeAll = false, frameContext = activeFrameContext) {
      const specs = [];
      const viewport = frameContext?.viewport || [view.width, view.height];
      const scale = Number(frameContext?.scale) || 1;
      const flatHalfLon = viewport[0] / Math.max(1, scale) * 90 / PI;
      const flatHalfLat = viewport[1] / Math.max(1, scale) * 90 / PI;
      const rotation = frameContext?.viewState?.rotation || view.rotation;
      const flatCenter = frameContext?.viewState?.projectionCenter || view.flatCenter;
      const globeCenter = [-Number(rotation?.[0] || 0), -Number(rotation?.[1] || 0)];
      const globeRadius = Math.asin(Math.min(1, Math.hypot(viewport[0], viewport[1]) * 0.5 / Math.max(1, scale)));
      for (let row = 0; row < level.rows; row += 1) {
        for (let column = 0; column < level.columns; column += 1) {
          const spec = terrainTileSpec(level, column, row);
          if (includeAll) {
            specs.push(spec);
            continue;
          }
          const [west, north, east, south] = spec.bounds;
          const center = [(west + east) / 2, (north + south) / 2];
          const halfLon = (east - west) / 2;
          const halfLat = (north - south) / 2;
          const projectionKind = frameContext?.viewState?.projection || view.projection;
          if (projectionKind === 'flat') {
            const deltaLon = Math.abs((((center[0] - flatCenter[0]) + 540) % 360) - 180);
            const deltaLat = Math.abs(center[1] - flatCenter[1]);
            if (deltaLon <= flatHalfLon + halfLon + 2 && deltaLat <= flatHalfLat + halfLat + 2) specs.push(spec);
          } else {
            const padding = Math.hypot(halfLon, halfLat) * PI / 180;
            if (geoDistance(globeCenter, center) <= globeRadius + padding + 0.04) specs.push(spec);
          }
        }
      }
      return specs;
    }

    function requestTerrainTile(spec, priority = 0, pump = true) {
      if (!gl || disposed || terrainTiles.has(spec.key) || terrainTileRequests.has(spec.key)
          || terrainTileQueuedKeys.has(spec.key)) return;
      const queued = terrainFetchQueuedEntries.get(spec.key);
      if (queued) {
        if (queued.priority !== Number(priority || 0)) {
          queued.priority = Number(priority || 0);
          sortTerrainFetchQueue();
        }
        return;
      }
      const previousFailure = terrainTileFailures.get(spec.key);
      if (previousFailure?.retryAt > performance.now()) return;
      const entry = { spec, priority: Number(priority || 0) };
      terrainFetchQueuedEntries.set(spec.key, entry);
      terrainFetchQueue.push(entry);
      sortTerrainFetchQueue();
      if (pump) pumpTerrainFetchQueue();
    }

    function sortTerrainFetchQueue() {
      terrainFetchQueue.sort((left, right) => right.priority - left.priority || left.spec.key.localeCompare(right.spec.key));
    }

    function pruneTerrainFetchQueue() {
      let retainedCount = 0;
      for (const entry of terrainFetchQueue) {
        if (!terrainRetentionKeys.has(entry.spec.key)) {
          terrainFetchQueuedEntries.delete(entry.spec.key);
          continue;
        }
        terrainFetchQueue[retainedCount++] = entry;
      }
      terrainFetchQueue.length = retainedCount;
    }

    const terrainFetchConcurrency = () => isMobile() ? 2 : terrainTargetTilesLoaded < terrainTargetTileCount ? 6 : 4;
    function pumpTerrainFetchQueue() {
      const concurrency = terrainFetchConcurrency();
      while (terrainActiveFetches < concurrency && pendingDecodedBytes < 32 * 1024 * 1024 && terrainFetchQueue.length) {
        const next = terrainFetchQueue.shift();
        terrainFetchQueuedEntries.delete(next.spec.key);
        startTerrainTileRequest(next.spec, next.priority);
      }
    }

    function startTerrainTileRequest(spec, priority = 0) {
      const previousFailure = terrainTileFailures.get(spec.key);
      const requestGeneration = epoch;
      const retainedAtStart = terrainRetentionKeys.has(spec.key);
      const controller = new AbortController();
      controllers.add(controller);
      terrainActiveFetches += 1;
      const request = (async () => {
        const response = await fetch(tileUrl(spec), { signal: controller.signal });
        if (!response.ok) throw new Error(`지형 타일 HTTP ${response.status}`);
        const blob = await response.blob();
        let bitmap;
        try { bitmap = await createImageBitmap(blob, { premultiplyAlpha: 'none', colorSpaceConversion: 'none' }); }
        catch (error) {
          if (terrainManifest?.representation === 'dem-relief-v1') {
            error.demDecodeFailure = true;
            throw error;
          }
          bitmap = await createImageBitmap(blob);
        }
        if (terrainManifest?.representation === 'dem-relief-v1'
            && (bitmap.width !== spec.pixelWidth + terrainManifest.gutter * 2
              || bitmap.height !== spec.pixelHeight + terrainManifest.gutter * 2)) {
          bitmap.close?.();
          const error = new Error(`DEM tile dimensions differ from manifest: ${spec.key}`);
          error.demDecodeFailure = true;
          throw error;
        }
        if (requestGeneration !== epoch || disposed || controller.signal.aborted) {
          bitmap.close?.();
          return;
        }
        if (!gl || disposed || !isWebGlRenderer()) {
          bitmap.close?.();
          return;
        }
        terrainTileFailures.delete(spec.key);
        terrainTileQueuedKeys.add(spec.key);
        pendingDecodedBytes += bitmap.width * bitmap.height * 4;
        terrainUploadQueue.push({ spec, bitmap });
        scheduleTerrainUpload();
      })().catch(error => {
        if (requestGeneration !== epoch || disposed || controller.signal.aborted) return;
        if (error.demDecodeFailure && !unusableReported) {
          unusableReported = true;
          onUnusable?.(String(error?.message || error));
        }
        const attempts = Number(previousFailure?.attempts || 0) + 1;
        const retryDelay = attempts <= 3 ? Math.min(4000, 400 * 2 ** (attempts - 1)) : 30000;
        terrainTileFailures.set(spec.key, { attempts, retryAt: performance.now() + retryDelay, error });
        if (attempts <= 3) {
          const timer = setTimeout(() => {
            retryTimers.delete(timer);
            if (requestGeneration === epoch && !disposed
                && (!retainedAtStart || terrainRetentionKeys.has(spec.key))) requestTerrainTile(spec, priority);
          }, retryDelay + 16);
          retryTimers.add(timer);
        }
        if (terrainRetentionKeys.has(spec.key)) invalidate('terrain-tile-failed');
        console.warn(`지형 타일을 불러오지 못했습니다: ${spec.key}`, error);
      }).finally(() => {
        controllers.delete(controller);
        if (requestGeneration !== epoch || disposed) return;
        if (terrainTileRequests.get(spec.key)?.promise === request) terrainTileRequests.delete(spec.key);
        terrainActiveFetches = Math.max(0, terrainActiveFetches - 1);
        if (controller.signal.aborted && terrainTargetTileKeys.has(spec.key)) requestTerrainTile(spec, priority, false);
        pumpTerrainFetchQueue();
      });
      terrainTileRequests.set(spec.key, { promise: request, controller });
    }

    function uploadTerrainTile(next) {
      if (!next) return false;
      const { spec, bitmap } = next;
      const byteLength = Math.max(1, Number(bitmap.width || spec?.pixelWidth || 1))
        * Math.max(1, Number(bitmap.height || spec?.pixelHeight || 1)) * 4;
      pendingDecodedBytes = Math.max(0, pendingDecodedBytes - byteLength);
      if (spec) terrainTileQueuedKeys.delete(spec.key);
      if (!gl || disposed || !isWebGlRenderer()) {
        bitmap.close?.();
        return false;
      }
        const texture = gl.createTexture();
        const dem = terrainManifest?.representation === 'dem-relief-v1';
        const oldPremultiply = gl.getParameter?.(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL);
        const oldColorSpace = dem ? gl.getParameter?.(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL) : undefined;
        try {
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
        if (dem) gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, gl.NONE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, bitmap);
        } catch (error) { gl.deleteTexture(texture); throw error; }
        finally {
          if (oldPremultiply !== undefined) gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, oldPremultiply);
          if (oldColorSpace !== undefined) gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, oldColorSpace);
          bitmap.close?.();
        }
        if (next.tint) {
          if (tint) gl.deleteTexture(tint.texture);
          if (tintFallback) gl.deleteTexture(tintFallback.texture);
          tintFallback = null;
          tint = { texture, byteLength };
          tintPending = false;
          invalidate('terrain-tint-ready');
        } else terrainTiles.set(spec.key, { texture, lastUsed: performance.now(), byteLength });
        terrainUploadCount += 1;
        let terrainBytes = [...terrainTiles.values()].reduce((sum, entry) => sum + Number(entry.byteLength || 0), 0);
        terrainBytes += tint?.byteLength || tintFallback?.byteLength || 0;
        const terrainBudget = Math.max(8 * 1024 * 1024, Number(cacheBudgetBytes) || 128 * 1024 * 1024);
        while (terrainBytes > terrainBudget) {
          let oldest = null;
          for (const item of terrainTiles.entries()) {
            if (terrainRetentionKeys.has(item[0])) continue;
            if (!oldest || item[1].lastUsed < oldest[1].lastUsed) oldest = item;
          }
          if (!oldest || oldest[0] === spec?.key) break;
          gl.deleteTexture(oldest[1].texture);
          terrainTiles.delete(oldest[0]);
          terrainBytes -= Number(oldest[1].byteLength || 0);
        }
      return true;
    }

    function scheduleTerrainUpload() {
      if (!uploadScheduler || !terrainUploadQueue.length) return;
      const generation = projectGeneration, contextGeneration = contextRevision, uploadEpoch = epoch;
      const queue = terrainUploadQueue;
      const key = 'terrain:' + generation + ':' + contextGeneration + ':' + uploadEpoch;
      uploadKeys.add(key);
      void uploadScheduler.enqueueUpload({
        key, projectGeneration: generation, contextGeneration, priority: 40,
        dispose: () => { if (uploadEpoch === epoch) discardUploads(queue); },
        step: () => {
          if (uploadEpoch !== epoch || disposed) throw Object.assign(new Error('Stale terrain upload'), { name: 'AbortError' });
          const next = terrainUploadQueue.shift();
          const bytes = next ? next.bitmap.width * next.bitmap.height * 4 : 0;
          const uploaded = next && uploadTerrainTile(next);
          if (uploaded && next?.spec && terrainRetentionKeys.has(next.spec.key)) invalidate('terrain-tile-ready');
          pumpTerrainFetchQueue();
          return { bytes, done: !terrainUploadQueue.length };
        },
      }).catch(error => {
        if (error.name !== 'AbortError') {
          console.warn('Terrain upload failed', error);
          if (terrainManifest?.representation === 'dem-relief-v1' && !unusableReported) {
            unusableReported = true;
            onUnusable?.(String(error?.message || error));
          }
        }
      }).finally(() => uploadKeys.delete(key));
    }

    function terrainGridMesh(spec, frameContext = activeFrameContext) {
      const spanLon = Math.abs(spec.bounds[2] - spec.bounds[0]);
      const spanLat = Math.abs(spec.bounds[1] - spec.bounds[3]);
      // Equirectangular terrain is affine inside a tile, so four vertices are
      // exact. On the globe, tessellate only enough to keep spherical chord
      // error below roughly one physical pixel. The old fixed 0.499-degree
      // grid generated hundreds of thousands of triangles for a single
      // overview tile and saturated mobile GPUs without improving the raster.
      const globe = Number(frameContext?.mode) === 0;
      const scale = Math.max(1, Number(frameContext?.scale) || 1);
      const angularStep = globe
        ? Math.max(0.75, Math.min(8, Math.sqrt(4 / scale) * 180 / PI))
        : 360;
      const stepsX = Math.max(1, Math.ceil(spanLon / angularStep));
      const stepsY = Math.max(1, Math.ceil(spanLat / angularStep));
      const key = `${globe ? 'globe' : 'flat'}:${stepsX}x${stepsY}`;
      if (terrainGridMeshes.has(key)) return terrainGridMeshes.get(key);
      const vertices = new Float32Array((stepsX + 1) * (stepsY + 1) * 2);
      let vertexOffset = 0;
      for (let y = 0; y <= stepsY; y += 1) {
        for (let x = 0; x <= stepsX; x += 1) {
          vertices[vertexOffset++] = x / stepsX;
          vertices[vertexOffset++] = y / stepsY;
        }
      }
      const indices = new Uint32Array(stepsX * stepsY * 6);
      let indexOffset = 0;
      for (let y = 0; y < stepsY; y += 1) {
        for (let x = 0; x < stepsX; x += 1) {
          const a = y * (stepsX + 1) + x;
          const b = a + 1;
          const c = a + stepsX + 1;
          const d = c + 1;
          indices[indexOffset++] = a; indices[indexOffset++] = c; indices[indexOffset++] = b;
          indices[indexOffset++] = b; indices[indexOffset++] = c; indices[indexOffset++] = d;
        }
      }
      const vertexBuffer = gl.createBuffer();
      gl.bindBuffer(gl.ARRAY_BUFFER, vertexBuffer);
      gl.bufferData(gl.ARRAY_BUFFER, vertices, gl.STATIC_DRAW);
      const indexBuffer = gl.createBuffer();
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, indexBuffer);
      gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, indices, gl.STATIC_DRAW);
      const meshEntry = { vertexBuffer, indexBuffer, indexCount: indices.length };
      terrainGridMeshes.set(key, meshEntry);
      return meshEntry;
    }

    function prepare() {
      if (!view.visible || !terrainManifest?.levels?.length || !gl || disposed) {
        terrainRetentionKeys.clear();
        pruneTerrainFetchQueue();
        return [];
      }
      if (terrainManifest.representation === 'dem-relief-v1' && view.physicalStyle === 'physical') requestTint();
      const frameContext = activeFrameContext;
      if (!frameContext) {
        terrainRetentionKeys.clear();
        pruneTerrainFetchQueue();
        return false;
      }
      if (terrainManifest.representation === 'dem-relief-v1') prepareTintFallback();
      const targetLevel = terrainLevelForView(frameContext);
      const targetSpecs = visibleTerrainTileSpecs(targetLevel, false, frameContext);
      // Keep one complete world base, independent of camera/country detail.
      // It is also the coverage reserve when rotating into an uncached region.
      const baseLevel = terrainManifest.levels[0];
      const baseSpecs = visibleTerrainTileSpecs(baseLevel, true, frameContext);
      const baseKeys = new Set(baseSpecs.map(spec => spec.key));
      const baseReady = baseSpecs.every(spec => terrainTiles.has(spec.key));
      terrainLastLevel = Number(targetLevel?.id ?? -1);
      terrainTargetTileCount = targetSpecs.length;
      terrainTargetTilesLoaded = targetSpecs.filter(spec => terrainTiles.has(spec.key)).length;
      terrainTargetTileKeys = new Set(targetSpecs.map(spec => spec.key));
      terrainRetentionKeys = new Set([...terrainTargetTileKeys, ...baseKeys]);
      // Only GPU-ready textures replace coverage. Keep cached fallback tiles
      // over the missing target regions, including when zooming out, and pin
      // exactly those draw resources against eviction during replacement uploads.
      const missingSpecs = targetSpecs.filter(spec => !terrainTiles.has(spec.key));
      const fallbackSpecs = missingSpecs.length
        ? [...(baseLevel.id !== targetLevel.id && baseReady ? baseSpecs : []),
          ...terrainManifest.levels.filter(level => level.id !== targetLevel.id && level.id !== baseLevel.id)
          .flatMap(level => visibleTerrainTileSpecs(level, false, frameContext))
          .filter(spec => terrainTiles.has(spec.key) && missingSpecs.some(target =>
            spec.bounds[0] < target.bounds[2] && spec.bounds[2] > target.bounds[0]
            && spec.bounds[3] < target.bounds[1] && spec.bounds[1] > target.bounds[3]))]
        : [];
      for (const spec of fallbackSpecs) terrainRetentionKeys.add(spec.key);
      if (targetLevel) for (const spec of terrainNeighbourSpecs(targetLevel, targetSpecs)) terrainRetentionKeys.add(spec.key);
      pruneTerrainFetchQueue();
      const projection = frameContext.viewState?.projection || view.projection;
      const rotation = frameContext.viewState?.rotation || view.rotation;
      const center = projection === 'flat' ? frameContext.viewState?.projectionCenter || view.flatCenter
        : [-Number(rotation?.[0] || 0), -Number(rotation?.[1] || 0)];
      for (const spec of baseSpecs) requestTerrainTile(spec, 50_000, false);
      for (const spec of targetSpecs) {
        const [west, north, east, south] = spec.bounds;
        const tileCenter = [(west + east) / 2, (north + south) / 2];
        const distance = projection === 'flat'
          ? Math.hypot((((tileCenter[0] - center[0]) + 540) % 360) - 180, tileCenter[1] - center[1]) * PI / 180
          : geoDistance(center, tileCenter);
        requestTerrainTile(spec, 30_000 - distance * 1_000, false);
      }
      if (targetLevel) for (const spec of terrainNeighbourSpecs(targetLevel, targetSpecs)) requestTerrainTile(spec, 1_000, false);
      const visibleTilesWaiting = terrainFetchQueue.some(entry => terrainTargetTileKeys.has(entry.spec.key));
      for (const [key, request] of terrainTileRequests) {
        if (!terrainRetentionKeys.has(key)
            || (visibleTilesWaiting && !terrainTargetTileKeys.has(key) && !baseKeys.has(key))) request.controller.abort();
      }
      // Fill the current-view batch before starting fetches so array traversal
      // cannot consume all slots with far-away tiles ahead of the center.
      pumpTerrainFetchQueue();
      terrainRenderedLevel = -1;
      const prepared = [];
      // Publish only after the world reserve is GPU-ready: even complete
      // current detail cannot cover a subsequent turn into an uncached region.
      const drawSpecs = baseReady ? [...fallbackSpecs, ...targetSpecs] : [];
      for (const spec of drawSpecs) {
        const tile = terrainTiles.get(spec.key);
        if (!tile) continue;
        tile.lastUsed = performance.now();
        prepared.push({ spec, texture: tile.texture, grid: terrainGridMesh(spec, frameContext), gutter: Number(terrainManifest.gutter || 0) });
        terrainRenderedLevel = Number(spec.level);
      }
      const exhausted = spec => Number(terrainTileFailures.get(spec.key)?.attempts || 0) >= 4;
      if (!prepared.length && targetSpecs.length && !unusableReported
          && baseSpecs.some(exhausted)) {
        unusableReported = true;
        const failedBase = baseSpecs.find(exhausted);
        onUnusable?.(`지형 바탕 타일을 불러오지 못했습니다: ${failedBase.key}: ${terrainTileFailures.get(failedBase.key).error.message}`);
      }
      return prepared;
    }

    function prepareTintFallback() {
      if (tint || tintFallback) return;
      const texture = gl.createTexture();
      if (!texture) throw new Error('Terrain tint texture allocation failed');
      try {
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        // Neutral flat-surface shade until the color image is GPU-ready.
        // Both styles keep a valid tint sampler and share the same height tiles.
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([212, 212, 212, 255]));
      } catch (error) { gl.deleteTexture(texture); throw error; }
      tintFallback = { texture, byteLength: 4 };
    }

    function requestTint() {
      if (!tintUrl || tint || tintPending) return;
      tintPending = true;
      const requestEpoch = epoch;
      const controller = new AbortController();
      controllers.add(controller);
      void (async () => {
        const response = await fetch(tintUrl(), { signal: controller.signal });
        if (!response.ok) throw new Error(`지형 색채 HTTP ${response.status}`);
        const bitmap = await createImageBitmap(await response.blob(), { premultiplyAlpha: 'none', colorSpaceConversion: 'none' });
        if (requestEpoch !== epoch || disposed) { bitmap.close?.(); return; }
        if (bitmap.width !== terrainManifest.tint.width || bitmap.height !== terrainManifest.tint.height) {
          bitmap.close?.(); throw new Error('지형 tint 크기가 manifest와 다릅니다.');
        }
        pendingDecodedBytes += bitmap.width * bitmap.height * 4;
        terrainUploadQueue.push({ tint: true, bitmap });
        scheduleTerrainUpload();
      })().catch(error => {
        if (requestEpoch === epoch && !controller.signal.aborted) {
          tintPending = false;
          console.warn('DEM terrain tint unavailable', error);
          if (!unusableReported) { unusableReported = true; onUnusable?.(String(error?.message || error)); }
        }
      }).finally(() => controllers.delete(controller));
    }


  function discardUploads(queue = terrainUploadQueue) {
    for (const item of queue.splice(0)) {
      if (item.spec) terrainTileQueuedKeys.delete(item.spec.key);
      pendingDecodedBytes = Math.max(0, pendingDecodedBytes - item.bitmap.width * item.bitmap.height * 4);
      item.bitmap.close?.();
    }
  }
  function reset() {
    for (const key of uploadKeys) uploadScheduler?.cancelKey?.(key);
    uploadKeys.clear();
    epoch++;
    for (const controller of controllers) controller.abort();
    controllers.clear();
    for (const timer of retryTimers) clearTimeout(timer);
    retryTimers.clear();
    discardUploads();
    terrainFetchQueue.length = 0; terrainFetchQueuedEntries.clear();
    terrainTileQueuedKeys.clear(); terrainTileRequests.clear(); terrainTileFailures.clear();
    terrainActiveFetches = 0;
    tintPending = false; pendingDecodedBytes = 0; unusableReported = false;
    if (gl && !gl.isContextLost?.()) {
      for (const tile of terrainTiles.values()) gl.deleteTexture(tile.texture);
      if (tint) gl.deleteTexture(tint.texture);
      if (tintFallback) gl.deleteTexture(tintFallback.texture);
      for (const grid of terrainGridMeshes.values()) { gl.deleteBuffer(grid.vertexBuffer); gl.deleteBuffer(grid.indexBuffer); }
    }
    tint = null; tintFallback = null; terrainTiles.clear(); terrainGridMeshes.clear();
    terrainLastLevel = -1; terrainRenderedLevel = -1;
    terrainTargetTileCount = 0; terrainTargetTilesLoaded = 0;
    terrainTargetTileKeys.clear(); terrainRetentionKeys.clear();
  }
  const settled = () => [...terrainTargetTileKeys].every(key => terrainTiles.has(key) || Number(terrainTileFailures.get(key)?.attempts || 0) >= 4);
  return Object.freeze({
    setContext(next) {
      if (disposed) return;
      if (gl !== next.gl || projectGeneration !== next.projectGeneration || contextRevision !== next.contextGeneration) reset();
      gl = next.gl; ready = next.ready; uploadScheduler = next.scheduler;
      projectGeneration = next.projectGeneration; contextRevision = next.contextGeneration;
    },
    setManifest(manifest) { if (terrainManifest !== manifest) reset(); terrainManifest = manifest; },
    prepare(frame, nextView) { activeFrameContext = frame; view = nextView; cacheBudgetBytes = nextView.cacheBudgetBytes; return prepare() || []; },
    request: requestTerrainTile,
    tintTexture: () => tint?.texture || tintFallback?.texture || null,
    scheduleUpload: scheduleTerrainUpload,
    settled, reset,
    stats: () => ({ terrainLevel: terrainLastLevel, terrainRenderedLevel, terrainTargetTileCount, terrainTargetTilesLoaded,
      terrainTargetTilesSettled: settled(), terrainTilesLoaded: terrainTiles.size,
      terrainCacheBytes: [...terrainTiles.values()].reduce((sum, tile) => sum + tile.byteLength, tint?.byteLength || tintFallback?.byteLength || 0),
      terrainPendingDecodedBytes: pendingDecodedBytes, terrainTintReady: !!tint,
      terrainTilesLoading: terrainTileRequests.size + terrainFetchQueue.length, terrainFetchConcurrency: terrainFetchConcurrency(),
      terrainUploadCount, terrainFailureCount: terrainTileFailures.size }),
    dispose() { if (disposed) return; reset(); disposed = true; gl = null; uploadScheduler = null; },
  });
}
