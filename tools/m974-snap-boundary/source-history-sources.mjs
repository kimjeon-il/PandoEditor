// Supplemental, read-only oracle source closure. Existing production pins remain immutable.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
import {gunzipSync} from 'node:zlib';
import {tmpdir} from 'node:os';
import {Worker} from 'node:worker_threads';
import {runInThisContext} from 'node:vm';
import {createRuntime,observe,referenceEffects} from '../m97/web-lifecycle.mjs';
import * as selectionRuntime from '../m97/web-selection.mjs';
import {selectionEntrypoints} from '../m97/web-selection.mjs';
import {fixtureRoot,behavioralCommit,entrypoints,readPinnedSources,relativeDependencies,sha256} from './sources.mjs';

export const sourceHistoryManifestPin='200cf832af6d359c3d97cf48e323f9802dcaeb8107dd2ca42d558c84ad40f18e';
export const sourceHistoryEntrypoints=Object.freeze([...new Set([...entrypoints,...selectionEntrypoints,'app-spatial-index','app-object-picking','cut-preparation-cache','editing-render-packet'])]);
const addedPaths=['assets/js/modules/app-object-picking.js','assets/js/modules/app-spatial-index.js'];
const repositoryRoot=fileURLToPath(new URL('../../',import.meta.url));
const gitBlob=bytes=>createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`),bytes])).digest('hex');

export function verifySourceHistorySources(bundle){
 const {manifest,sources}=bundle,base=readPinnedSources();
 assert.equal(sha256(JSON.stringify(manifest)),sourceHistoryManifestPin,'immutable source-history supplemental source manifest pin');
 assert.equal(manifest.schema,'pando-m974-source-history-source-pin');assert.equal(manifest.version,1);
 assert.equal(manifest.behavioralCommit,behavioralCommit,'approved exact web source commit');
 assert.equal(manifest.baseSourceManifestSha256,sha256(JSON.stringify(base.manifest)),'unchanged original source manifest');
 for(const [name,pin]of Object.entries(manifest.protectedFileSha256))assert.equal(sha256(fs.readFileSync(path.join(repositoryRoot,name))),pin,'unchanged original source file '+name);
 assert.deepEqual(manifest.entrypoints,sourceHistoryEntrypoints,'exact actual module import entrypoints');
 assert.deepEqual(Object.keys(sources).sort(),manifest.sources.map(row=>row.path).sort(),'exact supplemental executable source closure');
 assert.deepEqual(Object.keys(sources).filter(name=>!Object.hasOwn(base.sources,name)).sort(),addedPaths,'only approved two-module augmentation');
 for(const [name,source]of Object.entries(base.sources))assert.equal(sources[name],source,'unchanged original production source '+name);
 for(const row of manifest.sources){
  assert.match(row.path,/^assets\/js\/(modules|workers|vendor)\/[a-zA-Z0-9_.-]+\.js$/);
  assert.equal(typeof sources[row.path],'string','missing source '+row.path);
  const bytes=Buffer.from(sources[row.path]);
  assert.equal(bytes.length,row.bytes,'source bytes '+row.path);assert.equal(sha256(bytes),row.sha256,'source hash '+row.path);assert.equal(gitBlob(bytes),row.blob,'source Git blob '+row.path);
  for(const dependency of relativeDependencies(row.path,sources[row.path]))assert.ok(Object.hasOwn(sources,dependency),'missing dependency closure '+dependency);
 }
 for(const name of manifest.entrypoints)assert.ok(Object.hasOwn(sources,'assets/js/modules/'+name+'.js'),'missing actual imported entrypoint '+name);
 return bundle;
}

export function readSourceHistorySources(){
 const manifest=JSON.parse(fs.readFileSync(path.join(fixtureRoot,'source-history-source-manifest.json'),'utf8'));
 assert.equal(sha256(JSON.stringify(manifest)),sourceHistoryManifestPin,'immutable source-history supplemental source manifest pin');
 const archive=manifest.additionalSourceArchive,compressed=fs.readFileSync(path.join(fixtureRoot,archive.file));
 assert.equal(compressed.length,archive.bytes,'supplemental source archive bytes');assert.equal(sha256(compressed),archive.sha256,'supplemental source archive hash');
 const bytes=gunzipSync(compressed);assert.equal(bytes.length,archive.decompressedBytes,'supplemental decompressed source bytes');assert.equal(sha256(bytes),archive.decompressedSha256,'supplemental decompressed source hash');
 const addedSources=JSON.parse(bytes);assert.deepEqual(Object.keys(addedSources).sort(),addedPaths,'exact supplemental source archive');
 const sources=Object.fromEntries(Object.entries({...readPinnedSources().sources,...addedSources}).sort(([a],[b])=>a.localeCompare(b)));
 return verifySourceHistorySources({manifest,sources});
}

export async function loadSourceHistoryNodeSources(){
 const bundle=readSourceHistorySources(),root=fs.mkdtempSync(path.join(tmpdir(),'m974-source-history-web-'));
 try{
  for(const [name,source]of Object.entries(bundle.sources)){const destination=path.join(root,name);fs.mkdirSync(path.dirname(destination),{recursive:true});fs.writeFileSync(destination,source);}
  fs.writeFileSync(path.join(root,'package.json'),'{"type":"module"}');
  globalThis.window=globalThis;
  await import(pathToFileURL(path.join(root,'assets/js/vendor/polygon-clipping.min.js')));
  runInThisContext(bundle.sources['assets/js/vendor/d3.min.js'],{filename:'pinned-d3.min.js'});
  await import(pathToFileURL(path.join(root,'assets/js/modules/polygon-geometry.js')));
  const modules=await Promise.all(sourceHistoryEntrypoints.map(name=>import(pathToFileURL(path.join(root,'assets/js/modules/'+name+'.js')))));
  const api={...Object.assign({},...modules),...globalThis.PandoLabPolygonGeometry,d3:globalThis.d3,clipper:globalThis.polygonClipping};
  const createWorker=()=>{
   const worker=new Worker(new URL('./worker-host.mjs',import.meta.url),{workerData:{root},execArgv:[]}),adapter={onmessage:null,onerror:null,postMessage:message=>worker.postMessage(message),terminate:()=>worker.terminate()};
   worker.on('message',data=>adapter.onmessage?.({data}));worker.on('error',error=>adapter.onerror?.(error));return adapter;
  };
  return {api,createWorker,selectionRuntime,assemblySource:bundle.sources['assets/js/modules/app-domain-assembly.js'],sourceTexts:bundle.sources,manifest:bundle.manifest,runtime:{createRuntime,observe,referenceEffects},cleanup:()=>fs.rmSync(root,{recursive:true,force:true})};
 }catch(error){fs.rmSync(root,{recursive:true,force:true});throw error;}
}
