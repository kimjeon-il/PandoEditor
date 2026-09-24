#!/usr/bin/env node
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { dirname, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'../tests/fixtures/web-hydro');
const manifest=JSON.parse(await readFile(resolve(root,'v0.13.1/manifest.json'),'utf8'));
const expected=JSON.parse(await readFile(resolve(root,'expected.json'),'utf8')).viewport.tiles;
const probe=process.argv[2];
assert.ok(probe,'native viewport probe path required');
const { createHydroTileWindow, hydroTileSpecsForWindow }=await import(pathToFileURL(
  resolve(root,'source/hydro-tile-window.js')));
function native({threshold,width,height,scale,flatCenter}) {
  const args=[threshold,width,height,scale,...flatCenter,...manifest.stages.flatMap(
    stage=>[stage.id,stage.minZoom,stage.columns,stage.rows])].map(String);
  return JSON.parse(execFileSync(probe,args,{encoding:'utf8'}));
}
const base={threshold:7.5,width:800,height:500,scale:1500,flatCenter:[20,1]};
assert.deepEqual(native(base),expected);
for(const view of [
  ...[5.99,6,6.7,7,7.5].map(threshold=>({...base,threshold})),
  {...base,flatCenter:[179.5,80]},
  {...base,flatCenter:[-179.5,-80],width:400,height:900,scale:450},
]) {
  const web=hydroTileSpecsForWindow(createHydroTileWindow({manifest,projection:'flat',...view}));
  assert.deepEqual(native(view),web,JSON.stringify(view));
}
const threshold=zoom=>2.4+Math.log2(Math.max(1,zoom))*2.05;
for(const minZoom of [6,6.7,7,7.5]) {
  const zoom=2**((minZoom-2.4)/2.05);
  assert.ok(Math.abs(threshold(zoom)-minZoom)<1e-10);
  const actual=Number(execFileSync(probe,['--threshold',String(zoom)],{encoding:'utf8'}));
  assert.ok(Math.abs(actual-minZoom)<1e-10);
}
console.log('web hydro viewport parity passed');
