#!/usr/bin/env node
// Runs the pinned web worker's actual decoders against a deterministic local fixture.
import assert from 'node:assert/strict';
import { readFile, writeFile } from 'node:fs/promises';
import { resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import vm from 'node:vm';
import { gunzipSync } from 'node:zlib';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../tests/fixtures/web-hydro');
const source = resolve(root, 'source');
const manifest = JSON.parse(await readFile(resolve(root, 'v0.13.1/manifest.json'), 'utf8'));
const context = vm.createContext({
  TextDecoder, Uint8Array, DataView, ArrayBuffer, Map, Set, URL,
  setTimeout, clearTimeout, console, importScripts() {},
});
context.self = context;
context.fflate = { gunzipSync: bytes => new Uint8Array(gunzipSync(bytes)) };
vm.runInContext(await readFile(resolve(source, 'earcut.min.js'), 'utf8'), context);
vm.runInContext(await readFile(resolve(source, 'geographic-boundary-core.js'), 'utf8'), context);
vm.runInContext(
  (await readFile(resolve(source, 'hydro-tile-worker.js'), 'utf8')) +
    '\n;globalThis.__probe={readGlobalIndex,readFeatureMetadata,readPack,mergeLogicalFragments,tilePacks,logicalPacks,packSpecs,featureMetadata,setManifest(value){manifest=value}};',
  context, { filename: 'hydro-tile-worker.js' },
);
const api = context.__probe;
api.setManifest(manifest);
const asset = async spec => readFile(resolve(root, 'v0.13.1', spec.url));
const index = gunzipSync(await asset(manifest.index));
api.readGlobalIndex(new Uint8Array(index));
api.readFeatureMetadata(new Uint8Array(gunzipSync(await asset(manifest.metadata.core))));
const shard = await asset(manifest.shards[0]);
const packs = [...api.packSpecs.values()].sort((a, b) => a.id - b.id).map(spec => {
  const bytes = gunzipSync(shard.subarray(spec.offset, spec.offset + spec.length));
  return { id: spec.id, value: api.readPack(new Uint8Array(bytes), spec.id) };
});
const { createHydroTileWindow, hydroTileSpecsForWindow } =
  await import(pathToFileURL(resolve(source, 'hydro-tile-window.js')));
const tileWindow = createHydroTileWindow({
  manifest, projection: 'flat', threshold: 7.5,
  width: 800, height: 500, scale: 1500, flatCenter: [20, 1],
});
const feature = packs.flatMap(row => row.value.features)
  .filter(row => Number(row.properties.__logicalFid) === 5);
const merged = api.mergeLogicalFragments(feature);
const plain = value => JSON.parse(JSON.stringify(value));
const actual = plain({
  index: {
    tiles: [...api.tilePacks].sort(([a], [b]) => a.localeCompare(b)),
    logical: [...api.logicalPacks].sort(([a], [b]) => a - b),
    packs: [...api.packSpecs].sort(([a], [b]) => a - b),
  },
  metadata: [...api.featureMetadata].sort(([a], [b]) => a - b),
  packs: packs.map(({ id, value }) => ({
    id, descriptors: value.descriptors,
    geometry: value.features.map(row => row.geometry),
    mesh: Object.fromEntries(Object.entries(value.mesh)
      .map(([key, array]) => [key, Array.from(array)])),
  })),
  viewport: {
    signature: tileWindow.signature,
    tiles: hydroTileSpecsForWindow(tileWindow),
  },
  merged: { id: merged.id, geometry: merged.geometry,
            widths: merged.properties.stroke_widths,
            sourceId: merged.properties.source_id },
});
const golden = resolve(root, 'expected.json');
if (process.argv.includes('--write-golden')) {
  await writeFile(golden, JSON.stringify(actual, null, 2) + '\n');
} else {
  assert.deepEqual(actual, JSON.parse(await readFile(golden, 'utf8')));
}
if (!process.argv.includes('--fixture-only')) {
  const probe = process.argv.at(-1);
  assert.ok(probe && !probe.startsWith('--'), 'native hydro_probe path is required');
  // Native JSON comparison is enabled with the parser probe in Tasks 3–5.
  const { execFileSync } = await import('node:child_process');
  const packMode=process.argv.includes('--pack-only');
  const native = JSON.parse(execFileSync(probe, [
    ...(packMode?['--pack']:[]),resolve(root, 'v0.13.1/manifest.json')], { encoding: 'utf8' }));
  const packExpected=packs.map(({id,value})=>({id,features:value.features.map(feature=>({
    fid:Number(feature.properties.__fid),logicalFid:Number(feature.properties.__logicalFid),
    kind:feature.properties.category,flags:Number(feature.properties.__flags),
    geometry:feature.geometry,widths:feature.properties.stroke_widths,
  }))}));
  assert.deepEqual(native,process.argv.includes('--index-only')
    ? {index:actual.index,metadata:actual.metadata} : packMode?plain(packExpected):actual);
}
console.log('web hydro fixture oracle passed');
