import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
import {gzipSync,gunzipSync} from 'node:zlib';
import {execFileSync} from 'node:child_process';
import {tmpdir} from 'node:os';
import {Worker} from 'node:worker_threads';
import {runInThisContext} from 'node:vm';
import {createRuntime,observe,referenceEffects} from '../m97/web-lifecycle.mjs';
export const fixtureRoot=fileURLToPath(new URL('../../tests/fixtures/web-m974/',import.meta.url));
export const baseBehavioralCommit='53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47';
export const behavioralCommit='ad78780f79f4f38fcd7c2a3c2fb1fe0ba8e37c32';
export const sourceChain=[baseBehavioralCommit,'12cd8c8ec47c83cfb8c650e8f44c81cdfac10043','6c3f930b8573fa09991885b661879ea36725472e','07d3e2053c71573e11c5cf89151f5f6686038511',behavioralCommit];
export const sha256=value=>createHash('sha256').update(value).digest('hex');
const gitBlob=bytes=>createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`),bytes])).digest('hex');
const manifestPin='8e4401889f2de3c9707798e0663c90a0d349b32fe06f1898660fbf3100b6d6b4';
export const entrypoints=[
 'territorial-units','territorial-entity-store','territorial-entity-repository','territorial-service','project-command-pipeline','project-state','history-service','app-project-snapshots','distribution-model','app-hydro-settings','app-layer-list','generic-feature-service','layer-presentation','project-invariants','geometry-preview','app-geometry-preview','app-territorial-drafts','app-object-presentation','map-object-categories','app-object-metadata','builtin-subunits','map-edit-worker-client','geometry-metrics',
 'app-pointer-targets','app-cut-geometry','geometry-snap','geometry-segment-index','app-country-modes','app-country-validation','territorial-interaction-policy','editing-domain','selection-domain','geometry-versions','app-object-commands','selection-ui-controller','object-selection-controller','boundary-topology'
];
export function relativeDependencies(name,source){
 const rows=[...source.matchAll(/(?:from\s*|import\s*\(?\s*)['"](\.[^'"]+\.js)['"]/g)].map(row=>path.posix.normalize(path.posix.join(path.posix.dirname(name),row[1])));
 if(name==='assets/js/workers/map-edit-worker.js') {
  for(const row of source.matchAll(/['"]([^'"]+\.js)['"]/g)) rows.push(row[1].startsWith('../')?path.posix.normalize(path.posix.join(path.posix.dirname(name),row[1])):'assets/js/modules/'+row[1]);
 }
 return [...new Set(rows)];
}
export function verifyPinnedSources(bundle){
 assert.equal(sha256(JSON.stringify(bundle.manifest)),manifestPin,'immutable production source manifest pin');
 const {manifest,sources}=bundle;
 assert.equal(manifest.schema,'pando-m974-production-source-pin');
 assert.equal(manifest.behavioralCommit,behavioralCommit);
 assert.deepEqual(manifest.sourceChain.map(row=>row.behavioralCommit),sourceChain,'approved source chain');
 assert.deepEqual(Object.keys(sources).sort(),manifest.sources.map(row=>row.path).sort(),'exact executable source closure');
 for(const row of manifest.sources){
  assert.match(row.path,/^assets\/js\/(modules|workers|vendor)\/[a-zA-Z0-9_.-]+\.js$/);
  assert.equal(typeof sources[row.path],'string','missing source '+row.path);
  const bytes=Buffer.from(sources[row.path]);
  assert.equal(bytes.length,row.bytes,'source bytes '+row.path);assert.equal(sha256(bytes),row.sha256,'source hash '+row.path);assert.equal(gitBlob(bytes),row.blob,'source Git blob '+row.path);
  for(const dependency of relativeDependencies(row.path,sources[row.path]))assert.ok(Object.hasOwn(sources,dependency),'missing dependency closure '+dependency);
 }
 assert.deepEqual(manifest.entrypoints,entrypoints);
 return bundle;
}
export function readPinnedSources(){
 const manifest=JSON.parse(fs.readFileSync(path.join(fixtureRoot,'source-manifest.json'),'utf8'));
 const bytes=gunzipSync(fs.readFileSync(path.join(fixtureRoot,'production-sources.json.gz')));
 assert.equal(sha256(bytes),manifest.sourceArchive.decompressedSha256,'decompressed archive hash');
 assert.equal(bytes.length,manifest.sourceArchive.decompressedBytes);
 return verifyPinnedSources({manifest,sources:JSON.parse(bytes)});
}
export function generatePinnedSources(webRoot){
 const git=(...args)=>execFileSync('git',['-C',webRoot,...args],{maxBuffer:64*1024*1024});
 assert.equal(git('rev-parse','HEAD').toString().trim(),behavioralCommit,'web checkout must be approved exact commit');
 assert.equal(git('status','--porcelain').toString().trim(),'','web checkout must be clean');
 const original=JSON.parse(fs.readFileSync(new URL('../../tests/fixtures/web-m97/lifecycle-manifest.json',import.meta.url))),
  pending=[...original.sources.map(row=>row.path),...entrypoints.map(name=>'assets/js/modules/'+name+'.js'),'assets/js/modules/app-domain-assembly.js','assets/js/vendor/d3.min.js'],sources={};
 while(pending.length){const name=pending.shift();if(Object.hasOwn(sources,name))continue;const source=git('show',behavioralCommit+':'+name).toString();sources[name]=source;pending.push(...relativeDependencies(name,source));}
 const ordered=Object.fromEntries(Object.entries(sources).sort(([a],[b])=>a.localeCompare(b))),bytes=Buffer.from(JSON.stringify(ordered));
 const layers=sourceChain.map((commit,i)=>({behavioralCommit:commit,...(i?{baseBehavioralCommit:sourceChain[i-1]}:{}),tree:git('rev-parse',commit+'^{tree}').toString().trim()}));
 const manifest={schema:'pando-m974-production-source-pin',version:1,repository:'kimjeon-il/Pando',baseBehavioralCommit,behavioralCommit,sourceChain:layers,entrypoints,sourceArchive:{file:'production-sources.json.gz',decompressedSha256:sha256(bytes),decompressedBytes:bytes.length},sources:Object.entries(ordered).map(([name,source])=>({path:name,blob:gitBlob(Buffer.from(source)),baseBlob:git('rev-parse',baseBehavioralCommit+':'+name).toString().trim(),sha256:sha256(source),bytes:Buffer.byteLength(source)}))};
 fs.mkdirSync(fixtureRoot,{recursive:true});fs.writeFileSync(path.join(fixtureRoot,'production-sources.json.gz'),gzipSync(bytes,{level:9,mtime:0}));fs.writeFileSync(path.join(fixtureRoot,'source-manifest.json'),JSON.stringify(manifest,null,2)+'\n');
 return {manifestSha256:sha256(JSON.stringify(manifest)),files:manifest.sources.length,rawBytes:bytes.length};
}
export function lifecycleRuntimeSource(){
 const text=fs.readFileSync(new URL('../m97/web-lifecycle.mjs',import.meta.url),'utf8');
 const seed=text.match(/function seedFeatures\(api\) \{[\s\S]*?\n\}\n\nfunction createRuntime/);assert.ok(seed,'lifecycle seed boundary');
 return `(()=>{const clone=value=>structuredClone(value),noop=()=>{},square=(x0,y0,x1,y1)=>({type:'Polygon',coordinates:[[[x0,y0],[x0,y1],[x1,y1],[x1,y0],[x0,y0]]]});${seed[0].slice(0,-'\n\nfunction createRuntime'.length)};return {createRuntime:${createRuntime.toString()},observe:${observe.toString()},referenceEffects:${referenceEffects.toString()}};})()`;
}
export async function loadNodeSources(){
 const bundle=readPinnedSources(),root=fs.mkdtempSync(path.join(tmpdir(),'m974-web-'));
 for(const [name,source]of Object.entries(bundle.sources)){const destination=path.join(root,name);fs.mkdirSync(path.dirname(destination),{recursive:true});fs.writeFileSync(destination,source);}
 fs.writeFileSync(path.join(root,'package.json'),'{"type":"module"}');
 try{
  globalThis.window=globalThis;
  await import(pathToFileURL(path.join(root,'assets/js/vendor/polygon-clipping.min.js')));
  runInThisContext(bundle.sources['assets/js/vendor/d3.min.js'],{filename:'pinned-d3.min.js'});
  await import(pathToFileURL(path.join(root,'assets/js/modules/polygon-geometry.js')));
  const modules=await Promise.all(entrypoints.map(name=>import(pathToFileURL(path.join(root,'assets/js/modules/'+name+'.js')))));
  const api={...Object.assign({},...modules),...globalThis.PandoLabPolygonGeometry,d3:globalThis.d3,clipper:globalThis.polygonClipping};
  const createWorker=()=>{const worker=new Worker(new URL('./worker-host.mjs',import.meta.url),{workerData:{root},execArgv:[]}),adapter={onmessage:null,onerror:null,postMessage:message=>worker.postMessage(message),terminate:()=>worker.terminate()};worker.on('message',data=>adapter.onmessage?.({data}));worker.on('error',error=>adapter.onerror?.(error));return adapter;};
  return {api,createWorker,sourceTexts:bundle.sources,manifest:bundle.manifest,runtime:{createRuntime,observe,referenceEffects},cleanup:()=>fs.rmSync(root,{recursive:true,force:true})};
 }catch(error){fs.rmSync(root,{recursive:true,force:true});throw error;}
}
if(process.argv[2]==='--generate')console.log(JSON.stringify(generatePinnedSources(process.argv[3])));
