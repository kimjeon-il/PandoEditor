// Node is diagnostic only. Browser uses the identical approved 106-file closure.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import {gunzipSync} from 'node:zlib';
import {createHash} from 'node:crypto';
import {sha256} from './contract.mjs';
import {relativeDependencies} from '../m974-snap-boundary/sources.mjs';
import path from 'node:path';
import {tmpdir} from 'node:os';
import {pathToFileURL} from 'node:url';
import {Worker} from 'node:worker_threads';
import {runInThisContext} from 'node:vm';
import {readSourceHistorySources,sourceHistoryEntrypoints} from '../m974-snap-boundary/source-history-sources.mjs';
import {createRuntime,observe,referenceEffects} from '../m97/web-lifecycle.mjs';
import {createSelectionRuntime,selectionCutView,seedSelectionFeatures,square} from '../m97/web-selection.mjs';
export const exchangeEntrypoints=Object.freeze([...new Set([...sourceHistoryEntrypoints,'project-serializer','project-state','app-country-validation','app-color-picker','color-adapter'])]);
export {readSourceHistorySources};
export const supplementPin='0e7168846663110be98ae216baf7030d7d983756ad15113f4e54f269139dd104';
export function readExchangeSources(){
 const base=readSourceHistorySources(),root=new URL('../../tests/fixtures/web-m975-model-exchange/',import.meta.url),supplement=JSON.parse(fs.readFileSync(new URL('source-manifest.json',root)));
 assert.equal(sha256(JSON.stringify(supplement)),supplementPin,'immutable approved source completion');assert.equal(supplement.baseManifestSha256,sha256(JSON.stringify(base.manifest)));
 const compressed=fs.readFileSync(new URL(supplement.archive.file,root));assert.equal(compressed.length,supplement.archive.bytes);assert.equal(sha256(compressed),supplement.archive.sha256);const raw=gunzipSync(compressed);assert.equal(raw.length,supplement.archive.decompressedBytes);assert.equal(sha256(raw),supplement.archive.decompressedSha256);const files=JSON.parse(raw);
 assert.deepEqual(Object.keys(files),supplement.sources.map(r=>r.path));for(const row of supplement.sources){const bytes=Buffer.from(files[row.path]);assert.equal(bytes.length,row.bytes);assert.equal(sha256(bytes),row.sha256);assert.equal(createHash('sha1').update(Buffer.concat([Buffer.from('blob '+bytes.length+'\0'),bytes])).digest('hex'),row.blob);}
 const sources={...base.sources,...Object.fromEntries(supplement.executablePaths.map(p=>[p,files[p]]))};for(const p of supplement.executablePaths)for(const dependency of relativeDependencies(p,sources[p]))assert.ok(Object.hasOwn(sources,dependency),'source completion dependency '+dependency);
 return {manifest:base.manifest,supplement,sources,evidenceSources:Object.fromEntries(supplement.evidencePaths.map(p=>[p,files[p]]))};
}
export async function loadExchangeSources(){
 const bundle=readExchangeSources(),root=fs.mkdtempSync(path.join(tmpdir(),'m975-exchange-'));
 try{
  for(const [name,source]of Object.entries(bundle.sources)){const file=path.join(root,name);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,source);}
  fs.writeFileSync(path.join(root,'package.json'),'{"type":"module"}');globalThis.window=globalThis;
  await import(pathToFileURL(path.join(root,'assets/js/vendor/polygon-clipping.min.js')));runInThisContext(bundle.sources['assets/js/vendor/d3.min.js']);
  await import(pathToFileURL(path.join(root,'assets/js/modules/polygon-geometry.js')));
  const modules=await Promise.all(exchangeEntrypoints.map(name=>import(pathToFileURL(path.join(root,'assets/js/modules/'+name+'.js')))));
  const api={...Object.assign({},...modules),...globalThis.PandoLabPolygonGeometry,d3:globalThis.d3,clipper:globalThis.polygonClipping};
  const createWorker=()=>{const worker=new Worker(new URL('../m974-snap-boundary/worker-host.mjs',import.meta.url),{workerData:{root},execArgv:[]}),adapter={onmessage:null,onerror:null,postMessage:message=>worker.postMessage(message),terminate:()=>worker.terminate()};worker.on('message',data=>adapter.onmessage?.({data}));worker.on('error',error=>adapter.onerror?.(error));return adapter;};
  return {...bundle,sourceTexts:bundle.sources,api,createWorker,runtime:{createRuntime,observe,referenceEffects},selectionRuntime:{createSelectionRuntime,selectionCutView,seedSelectionFeatures,square},cleanup:()=>fs.rmSync(root,{recursive:true,force:true})};
 }catch(error){fs.rmSync(root,{recursive:true,force:true});throw error;}
}
