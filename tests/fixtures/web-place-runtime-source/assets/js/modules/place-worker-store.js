import { createHydroTileWindow, hydroTileSpecsForWindow } from './hydro-tile-window.js';
import { decodePlaceTile } from './place-codec.js';
import { comparePlaces, normalizePlaceQuery, PLACE_LIMITS } from './place-contract.js';
import { createFrameProjectors } from './map-visual-frame.js';
import { placeLabelDimensions, automaticLabelSettings } from './label-layout.js';
const noCancellation = Object.freeze({ throwIfCancelled() {} });
const SHA256_PATTERN = /^[a-f0-9]{64}$/u;

async function sha256Hex(bytes) {
  const hash = await crypto.subtle.digest('SHA-256', bytes);
  return Array.from(new Uint8Array(hash), value => value.toString(16).padStart(2, '0')).join('');
}

// Cull within the conservative tile window before spending the candidate budget.
function viewportPredicate(view) {
  const frame=view.projectionFrame;
  if (!frame || !frame.cssViewport?.every(Number.isFinite) || !(frame.cssScale > 0) || !frame.cssTranslate?.every(Number.isFinite)) throw new TypeError('Place viewport requires canonical projection frame');
  const {projectVisibleCoordinate}=createFrameProjectors(frame);
  return record => {
    const point=projectVisibleCoordinate(record.coordinates); if (!point) return false;
    const {width,height}=placeLabelDimensions(record.name), safe=frame.safeInset;
    return point[0]-width/2 >= safe.left && point[0]+width/2 <= frame.cssViewport[0]-safe.right
      && point[1]-height/2 >= safe.top && point[1]+height/2 <= frame.cssViewport[1]-safe.bottom;
  };
}

export async function readPlaceResponse(response, limit) {
  if (Number(response.headers.get('Content-Length')) > limit) throw new RangeError('Place response byte budget exceeded');
  if (!response.body?.getReader) { const bytes = new Uint8Array(await response.arrayBuffer()); if (bytes.length > limit) throw new RangeError('Place response byte budget exceeded'); return bytes; }
  const reader = response.body.getReader(), chunks = []; let length = 0;
  try {
    for (;;) { const { value, done } = await reader.read(); if (done) break; length += value.length; if (length > limit) throw new RangeError('Place response byte budget exceeded'); chunks.push(value); }
  } catch (error) { await reader.cancel().catch(() => {}); throw error; }
  finally { reader.releaseLock(); }
  const result = new Uint8Array(length); let offset = 0;
  for (const chunk of chunks) { result.set(chunk, offset); offset += chunk.length; }
  return result;
}
export function validatePlaceManifest(raw) {
  if (raw?.version !== 1 || !Array.isArray(raw.stages) || raw.stages.length > 8 || !raw.tiles || !raw.shards || !raw.search) throw new TypeError('Invalid place manifest');
  const stageIds = new Set();
  for (const stage of raw.stages) {
    if (!Number.isInteger(stage.id) || stageIds.has(stage.id) || !Number.isFinite(stage.minZoom) || stage.minZoom < 0 || !Number.isInteger(stage.columns) || stage.columns < 1 || stage.columns > 512 || !Number.isInteger(stage.rows) || stage.rows < 1 || stage.rows > 256) throw new TypeError('Invalid place stage');
    stageIds.add(stage.id);
  }
  for (const spec of Object.values(raw.shards)) {
    if (!spec.url || !Number.isInteger(spec.bytes) || spec.bytes < 1 || spec.bytes > PLACE_LIMITS.shardBytes) throw new RangeError('Invalid place shard byte budget');
  }
  const checkTile = row => {
    const shard = raw.shards[row?.shard];
    if (!shard || !Number.isInteger(row.offset) || row.offset < 0 || !Number.isInteger(row.length) || row.length < 32 || row.offset + row.length > shard.bytes) throw new RangeError('Invalid place tile range');
    if (!SHA256_PATTERN.test(String(row.sha256 || ''))) throw new TypeError('Invalid place tile hash');
  };
  for (const [key,row] of Object.entries(raw.tiles)) {
    const match = /^(\d+)\/(\d+)-(\d+)$/u.exec(key), stage = match && raw.stages.find(s => s.id === Number(match[1]));
    if (!stage || Number(match[2]) >= stage.columns || Number(match[3]) >= stage.rows) throw new TypeError('Invalid place tile key');
    checkTile(row);
  }
  for (const pages of Object.values(raw.search)) {
    if (!Array.isArray(pages)) throw new TypeError('Invalid place search pages');
    for (const row of pages) { checkTile(row); if (typeof row.first !== 'string' || typeof row.last !== 'string' || row.first > row.last) throw new TypeError('Invalid place search range'); }
  }
  return raw;
}

/** Worker-only owner of loaded tile data. No project objects or DOM here. */
export function createPlaceWorkerStore({ manifest: raw, baseUrl = 'http://localhost/', fetchBytes = null, cacheBytes = PLACE_LIMITS.cacheBytes } = {}) {
  const manifest = validatePlaceManifest(raw), cache = new Map(), inflight = new Map();
  const budget = Math.max(0, Math.min(PLACE_LIMITS.cacheBytes, Number(cacheBytes) || 0));
  let used = 0; const metrics = { queryCount: 0, networkCount: 0, evictions: 0, lastTileCount: 0, lastCandidateCount: 0, peakWorkingRecords: 0 };
  function get(key) { const entry=cache.get(key); if (!entry) return null; cache.delete(key); cache.set(key,entry); return entry.value; }
  function put(key,value,bytes) {
    if (bytes > budget) return value;
    const existing=cache.get(key); if (existing) { used-=existing.bytes; cache.delete(key); }
    while (used + bytes > budget && cache.size) { const oldest=cache.keys().next().value; used-=cache.get(oldest).bytes; cache.delete(oldest); metrics.evictions++; }
    cache.set(key,{value,bytes}); used+=bytes; return value;
  }
  async function loadTile(row, context) {
    context.throwIfCancelled();
    const key=`${row.shard}:${row.offset}:${row.length}`, hit=get(`tile:${key}`); if (hit) return hit;
    if (inflight.has(key)) {
      const pending=inflight.get(key);
      if (pending.context === context) return pending.task;
      try { await pending.task; } catch (error) { if (!error.cancelled && error.name !== 'AbortError') throw error; }
      context.throwIfCancelled();
      return loadTile(row,context);
    }
    const task=(async () => {
      const spec=manifest.shards[row.shard]; let full=get(`shard:${row.shard}`), bytes;
      if (!full) {
        metrics.networkCount++;
        if (fetchBytes) { full=await fetchBytes(spec); if (full.byteLength !== spec.bytes || full.byteLength > PLACE_LIMITS.shardBytes) throw new RangeError('Invalid place shard length/budget'); }
        else {
          const url=new URL(spec.url,baseUrl); const response=await fetch(url, { headers: { Range: `bytes=${row.offset}-${row.offset+row.length-1}` }, signal: context.signal });
          if (!response.ok) throw new Error(`Place HTTP ${response.status}: ${row.shard}`);
          const payload=await readPlaceResponse(response, spec.bytes);
          if (response.status === 206) {
            if (response.headers.get('Content-Range') !== `bytes ${row.offset}-${row.offset+row.length-1}/${spec.bytes}` || payload.length !== row.length) throw new RangeError('Invalid place range response');
            bytes=payload;
          } else if (response.status === 200 && payload.length === spec.bytes) full=payload;
          else throw new RangeError('Invalid place shard response');
        }
        if (full) {
          if (spec.sha256 && await sha256Hex(full) !== spec.sha256) throw new Error('Invalid place shard hash');
          context.throwIfCancelled(); put(`shard:${row.shard}`,full,full.byteLength);
        }
      }
      context.throwIfCancelled(); bytes ||= full.subarray(row.offset,row.offset+row.length);
      if (await sha256Hex(bytes) !== row.sha256) throw new Error('Invalid place tile hash');
      context.throwIfCancelled();
      const records=decodePlaceTile(bytes);
      const decodedBytes=records.reduce((total,record) => total+512+Object.values(record).reduce((sum,value)=>sum+(typeof value === 'string' ? value.length*2 : 0),0),0);
      context.throwIfCancelled(); return put(`tile:${key}`,records,decodedBytes);
    })();
    inflight.set(key,{task,context});
    try { return await task; } finally { inflight.delete(key); }
  }
  async function loadRows(rows, context, accepts, limit) {
    let records=[]; let truncated=false;
    for (let i=0; i<rows.length; i+=4) {
      context.throwIfCancelled();
      const batch=await Promise.all(rows.slice(i,i+4).map(row => loadTile(row,context)));
      metrics.peakWorkingRecords=Math.max(metrics.peakWorkingRecords,records.length+batch.reduce((sum,tile)=>sum+tile.length,0));
      const unique=new Map(records.map(record=>[record.id,record]));
      for (const tile of batch) for (const record of tile) if (accepts(record)) unique.set(record.id,record);
      truncated ||= unique.size > limit;
      records=[...unique.values()].sort(comparePlaces).slice(0,limit);
    }
    return {records,truncated};
  }
  async function queryViewport(view, context=noCancellation) {
    metrics.queryCount++; context.throwIfCancelled();
    const stages=manifest.stages.filter(stage => stage.minZoom <= Number(view.threshold || 0)).sort((a,b) => b.minZoom-a.minZoom);
    let tileWindow=null, specs=[];
    for (const stage of stages) { tileWindow=createHydroTileWindow({ ...view, manifest: { stages: [stage] } }); specs=hydroTileSpecsForWindow(tileWindow); if (specs.length <= PLACE_LIMITS.queryTiles) break; }
    if (specs.length > PLACE_LIMITS.queryTiles) throw new RangeError('Place viewport tile budget exceeded');
    const rows=specs.map(spec => manifest.tiles[`${spec.stage}/${spec.x}-${spec.y}`]).filter(Boolean);
    const inViewport=viewportPredicate(view);
    const {records}=await loadRows(rows,context,record => Math.max(record.minZoom,automaticLabelSettings(record.kind).minZoom) <= Number(view.threshold || 0) && inViewport(record),PLACE_LIMITS.candidates);
    metrics.lastTileCount=rows.length; metrics.lastCandidateCount=records.length;
    return { signature: `${manifest.revision}:${tileWindow?.signature || 'empty'}`, records, tileCount: rows.length, cacheBytes: used, peakWorkingRecords:metrics.peakWorkingRecords };
  }
  async function search(value, context=noCancellation) {
    const query=normalizePlaceQuery(value); if ([...query].length < 2) return { records: [], truncated: false };
    const prefix=[...query].slice(0,2).join(''), pages=(manifest.search[prefix] || []).filter(row => row.last >= query && row.first <= `${query}\uffff`);
    const selected=pages.slice(0,PLACE_LIMITS.queryTiles);
    const {records,truncated}=await loadRows(selected,context,record=>normalizePlaceQuery(record.name).startsWith(query),PLACE_LIMITS.searchResults);
    return { records, truncated: pages.length > selected.length || truncated };
  }
  return Object.freeze({ queryViewport, search, stats: () => ({ ...metrics, cacheBytes: used, cacheBudget: budget, cachedTiles: [...cache.keys()].filter(key => key.startsWith('tile:')).length, inflight: inflight.size }) });
}
