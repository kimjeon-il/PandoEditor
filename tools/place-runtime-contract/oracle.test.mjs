import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import test from 'node:test';
import { createPlaceWorkerStore } from '../../tests/fixtures/web-place-runtime-source/assets/js/modules/place-worker-store.js';
import { placeView } from '../../tests/fixtures/web-place-runtime-source/tests/helpers/place-view.mjs';
const root=new URL('../../tests/fixtures/web-place-runtime-source/',import.meta.url);
const sha=b=>createHash('sha256').update(b).digest('hex');
function store(name,cacheBytes) {
 const folder=new URL(`synthetic/${name}/`,root),manifest=JSON.parse(readFileSync(new URL('manifest.json',folder)));
 return createPlaceWorkerStore({manifest,cacheBytes,fetchBytes:async spec=>new Uint8Array(readFileSync(new URL(spec.url,folder)))});
}
const view=()=>placeView({width:400,height:300,scale:100,threshold:3});
test('pinned source and synthetic artifact hashes match every raw byte',()=>{
 const provenance=JSON.parse(readFileSync(new URL('source-manifest.json',root)));
 assert.equal(provenance.webCommit,'ebcfae4d27b29cbbea6416a7045a4806930204be');
 for(const spec of [...provenance.sources,...provenance.artifacts])assert.equal(sha(readFileSync(new URL(spec.path,root))),spec.sha256,spec.path);
 const manifest=JSON.parse(readFileSync(new URL('assets/data/places/manifest.json',root)));assert.equal(manifest.revision,'empty-v1');assert.deepEqual(manifest.tiles,{});
});
test('synthetic basic fixture has exactly the two on-screen records and reuses cache',async()=>{
 const s=store('basic'),r=await s.queryViewport(view());assert.deepEqual(r.records.map(r=>r.sourceId),['capital','city']);assert.equal(r.tileCount,1);
 const reads=s.stats().networkCount;await s.queryViewport(view());assert.equal(s.stats().networkCount,reads);
 const found=await s.search('  서울  ');assert.deepEqual(found.records.map(r=>r.sourceId),['capital','city']);
});
test('synthetic overscan priority cannot starve a visible place',async()=>{
 const r=await store('overscan').queryViewport(view());assert.deepEqual(r.records.map(r=>r.sourceId),['capital']);
});
test('synthetic dense fixture uses canonical 1500 cap and population order',async()=>{
 const s=store('dense'),r=await s.queryViewport(view());assert.equal(r.records.length,1500);assert.equal(r.records[0].sourceId,'02047');
 assert.ok(s.stats().cacheBytes<=24*1024*1024);assert.equal(r.tileCount,4);
});
test('synthetic cancellation cannot fill an empty cache',async()=>{
 const s=store('basic');await assert.rejects(s.queryViewport(view(),{throwIfCancelled(){throw Object.assign(new Error('cancelled'),{cancelled:true});}}));assert.equal(s.stats().cacheBytes,0);
});
