import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {readPinnedSources,entrypoints,sha256} from './sources.mjs';
import {selectionEntrypoints} from '../m97/web-selection.mjs';

const sourceModule=await import('./source-history-sources.mjs').catch(error=>{if(error.code!=='ERR_MODULE_NOT_FOUND')throw error;return {};});
const expectedEntrypoints=[...new Set([...entrypoints,...selectionEntrypoints,'app-spatial-index','app-object-picking','cut-preparation-cache','editing-render-packet'])];
const additionalPaths=['assets/js/modules/app-object-picking.js','assets/js/modules/app-spatial-index.js'];
const gitBlob=source=>{const bytes=Buffer.from(source);return createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`),bytes])).digest('hex');};
const preserved=['sources.mjs','../../tests/fixtures/web-m974/source-manifest.json','../../tests/fixtures/web-m974/production-sources.json.gz'];
const protectedHashes=()=>Object.fromEntries(preserved.map(name=>[name,sha256(fs.readFileSync(new URL(name,import.meta.url)))]));

test('source-history augments the immutable production closure with the two actual application modules',()=>{
 assert.equal(typeof sourceModule.readSourceHistorySources,'function','Source-history source bundle reader must exist');
 const before=protectedHashes(),base=readPinnedSources(),bundle=sourceModule.readSourceHistorySources();
 assert.equal(sourceModule.verifySourceHistorySources(bundle),bundle);
 assert.equal(bundle.manifest.schema,'pando-m974-source-history-source-pin');assert.equal(bundle.manifest.version,1);
 assert.equal(bundle.manifest.behavioralCommit,'ad78780f79f4f38fcd7c2a3c2fb1fe0ba8e37c32');
 assert.deepEqual(bundle.manifest.entrypoints,expectedEntrypoints);
 assert.deepEqual(sourceModule.sourceHistoryEntrypoints,expectedEntrypoints);
 assert.equal(bundle.manifest.sources.length,base.manifest.sources.length+2);
 assert.deepEqual(Object.keys(bundle.sources).filter(name=>!Object.hasOwn(base.sources,name)).sort(),additionalPaths);
 for(const [name,source]of Object.entries(base.sources))assert.equal(bundle.sources[name],source,'Old source unchanged '+name);
 for(const row of bundle.manifest.sources){assert.equal(gitBlob(bundle.sources[row.path]),row.blob);assert.equal(sha256(bundle.sources[row.path]),row.sha256);}
 assert.deepEqual(protectedHashes(),before);
});

test('source-history bundle rejects tampered manifests, either source layer, missing closure and unexpected imports',()=>{
 assert.equal(typeof sourceModule.readSourceHistorySources,'function','Source-history source bundle reader must exist');
 const bundle=sourceModule.readSourceHistorySources();
 for(const [label,mutate]of [
  ['additional source',b=>{b.sources[additionalPaths[0]]+='\n';}],
  ['original source',b=>{b.sources['assets/js/modules/map-edit-worker-client.js']+='\n';}],
  ['missing source',b=>{delete b.sources[additionalPaths[1]];}],
  ['unlisted source',b=>{b.sources['assets/js/modules/unlisted.js']='export const x=1;';}],
  ['wrong Git blob',b=>{b.manifest.sources.at(-1).blob='0'.repeat(40);}],
  ['wrong commit',b=>{b.manifest.behavioralCommit='0'.repeat(40);}],
  ['omitted import',b=>{b.manifest.entrypoints.pop();}],
  ['duplicate import',b=>{b.manifest.entrypoints.push('app-spatial-index');}],
 ]){const invalid=structuredClone(bundle);mutate(invalid);assert.throws(()=>sourceModule.verifySourceHistorySources(invalid),undefined,label);}
});

test('Node source-history loader supplies actual application modules, lifecycle helper and Worker host',async()=>{
 assert.equal(typeof sourceModule.loadSourceHistoryNodeSources,'function','Actual Node source-history loader must exist');
 const loaded=await sourceModule.loadSourceHistoryNodeSources();
 try{
  for(const name of ['createSpatialIndex','createObjectPicking','createTerritorySelectionWorkflow','createMapEditWorkerClient','rememberCutPreparation','freezeEditingGeometry'])assert.equal(typeof loaded.api[name],'function',name);
  assert.equal(typeof loaded.selectionRuntime.createSelectionRuntime,'function');
  assert.equal(loaded.assemblySource,loaded.sourceTexts['assets/js/modules/app-domain-assembly.js']);
  assert.equal(typeof loaded.runtime.createRuntime,'function');assert.equal(typeof loaded.api.d3.geo.mercator,'function');
  assert.equal(typeof loaded.api.clipper.union,'function');
  assert.deepEqual(loaded.sourceTexts,sourceModule.readSourceHistorySources().sources);
  const worker=loaded.createWorker();
  try{
   const result=await new Promise((resolve,reject)=>{
    const timer=setTimeout(()=>reject(Error('Actual source-history Worker did not respond')),10000);
    worker.onmessage=event=>{clearTimeout(timer);resolve(event.data);};worker.onerror=error=>{clearTimeout(timer);reject(error);};
    worker.postMessage({type:'rebase',dataRevision:1,geometryRevision:1,targetRevision:0,boundaryIds:[],features:[],editSources:{patches:[],removedKeys:[],sourceRevision:1}});
   });
   assert.deepEqual(result,{type:'ready',dataRevision:1,geometryRevision:1,targetRevision:0});
  }finally{await worker.terminate();}
 }finally{loaded.cleanup();}
});

const options={commit:'a'.repeat(40),runId:'unit-source-history-only'};
const optionalModule=async name=>import(name).catch(error=>{if(error.code!=='ERR_MODULE_NOT_FOUND')throw error;return {};});

test('source-history browser page embeds portable runtime and refuses local capture before writing output',async()=>{
 const browser=await optionalModule('./source-history-browser-runner.mjs');
 assert.equal(typeof browser.createSourceHistoryPage,'function','Dedicated source-history browser page must exist');
 const suiteModule=await import('./source-history-suite.mjs'),suite=await suiteModule.createSourceHistorySuite(options);
 const html=browser.createSourceHistoryPage(suite);
 assert.ok(html.includes('DecompressionStream'));assert.ok(html.includes('__m974SourceHistoryReport'));assert.ok(html.includes('__m974SourceHistorySummary'));assert.ok(!html.includes('node:'));
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'m974-source-history-refusal-'));
 try{
  const destination=path.join(dir,'must-not-create'),env={...process.env};delete env.GITHUB_ACTIONS;
  const result=spawnSync(process.execPath,[fileURLToPath(new URL('./source-history-browser-runner.mjs',import.meta.url)),destination],{env,encoding:'utf8'});
  assert.notEqual(result.status,0);assert.match(result.stderr,/authorized exact-commit CI/);assert.equal(fs.existsSync(destination),false);
 }finally{fs.rmSync(dir,{recursive:true,force:true});}
});

test('source-history capture preflight rejects non-CI callers without authorizing a browser',async()=>{
 const browser=await optionalModule('./source-history-browser-runner.mjs');
 assert.equal(typeof browser.verifySourceHistoryCI,'function','Exact-commit source-history CI guard must exist');
 for(const env of [{},{GITHUB_ACTIONS:'false'},{GITHUB_ACTIONS:'yes',GITHUB_SHA:options.commit,GITHUB_RUN_ID:'123'}])assert.throws(()=>browser.verifySourceHistoryCI(env,options.commit),/authorized exact-commit CI/);
});

test('source-history bounded export preserves Unicode and rejects corrupt or incomplete reports',async()=>{
 const browser=await optionalModule('./source-history-browser-runner.mjs');
 assert.equal(typeof browser.exportSourceHistoryReport,'function','Bounded source-history export must exist');
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'m974-source-history-transfer-'));
 try{
  // Transfer-only unit scaffold, never represented as an actual Chromium observation.
  const report={state:'complete',runtime:{},unicode:'🧭'.repeat(200000)},page={evaluate:async(fn,arg)=>fn(arg)};
  globalThis.__m974SourceHistoryReport=structuredClone(report);
  const destination=path.join(dir,'report.json'),receipt=await browser.exportSourceHistoryReport(page,destination,{testOnly:true});
  assert.ok(receipt.chunks>1);assert.equal(receipt.sha256,sha256(fs.readFileSync(destination)));
  assert.deepEqual(JSON.parse(fs.readFileSync(destination)),{...report,runtime:{testOnly:true}});assert.equal(globalThis.__m974SourceHistoryExport,undefined);
  const badPage={evaluate:async(fn,arg)=>{const result=await fn(arg);if(arg&&Object.hasOwn(arg,'offset'))result.text='x'+result.text.slice(1);return result;}};
  globalThis.__m974SourceHistoryReport=structuredClone(report);
  await assert.rejects(()=>browser.exportSourceHistoryReport(badPage,path.join(dir,'bad.json'),{}),/Complete report digest/);
  assert.equal(fs.existsSync(path.join(dir,'bad.json')),false);assert.equal(globalThis.__m974SourceHistoryExport,undefined);
  globalThis.__m974SourceHistoryReport={state:'pending'};
  await assert.rejects(()=>browser.exportSourceHistoryReport(page,path.join(dir,'pending.json'),{}),/Missing complete actual source-history observation/);
  assert.equal(fs.existsSync(path.join(dir,'pending.json')),false);
 }finally{delete globalThis.__m974SourceHistoryReport;delete globalThis.__m974SourceHistoryExport;fs.rmSync(dir,{recursive:true,force:true});}
});

test('Node source-history discovery remains distinguishable from authoritative Chromium capture',async()=>{
 const node=await optionalModule('./source-history-node-runner.mjs');
 assert.equal(typeof node.runSourceHistoryDiscovery,'function','Separate actual-source Node discovery runner must exist');
 const suiteModule=await import('./source-history-suite.mjs'),suite=await suiteModule.createSourceHistorySuite(options),report=await node.runSourceHistoryDiscovery(options);
 assert.equal(report.schema,'pando-m974-node-source-history-discovery');assert.equal(report.version,1);assert.equal(report.state,'complete');
 assert.equal(report.authoritativeBrowserObservation,false);assert.equal(report.runtime.node,process.version);assert.equal(report.runtime.v8,process.versions.v8);
 assert.deepEqual(report.sourceHashes,Object.fromEntries(suite.manifest.sources.map(row=>[row.path,row.sha256])));
 assert.deepEqual(report.identity,suite.identity);assert.deepEqual(report.cases.map(row=>row.case),suite.cases.map(row=>row.id));
 assert.throws(()=>suiteModule.verifySourceHistoryReport(suite,report));
});

test('browser source-history preflight rejects downloaded source, manifest and runtime corruption before execution',async()=>{
 const browser=await optionalModule('./source-history-browser-runner.mjs');
 assert.equal(typeof browser.runSourceHistoryBrowser,'function','Portable verified browser runner must exist');
 const source='export const untouched=true;',runtime='(()=>({}))()',base={sources:{'assets/js/modules/unit-only.js':source},manifest:{sources:[{path:'assets/js/modules/unit-only.js',sha256:sha256(source)}]},runtime:{source:runtime,sha256:sha256(runtime)}};
 const originalFetch=globalThis.fetch;
 try{
  globalThis.fetch=async()=>new Response(null,{status:404});
  await assert.rejects(()=>browser.runSourceHistoryBrowser({suite:base,baseUrl:new URL('https://invalid.example/')}),/Missing pinned production source/);
  globalThis.fetch=async()=>new Response(source+' ');
  await assert.rejects(()=>browser.runSourceHistoryBrowser({suite:base,baseUrl:new URL('https://invalid.example/')}),/Pinned production source mismatch/);
  globalThis.fetch=async()=>new Response(source);
  const wrongManifest=structuredClone(base);wrongManifest.manifest.sources[0].sha256='0'.repeat(64);
  await assert.rejects(()=>browser.runSourceHistoryBrowser({suite:wrongManifest,baseUrl:new URL('https://invalid.example/')}),/Production manifest mismatch/);
  const wrongRuntime=structuredClone(base);wrongRuntime.runtime.source+=' ';
  await assert.rejects(()=>browser.runSourceHistoryBrowser({suite:wrongRuntime,baseUrl:new URL('https://invalid.example/')}),/Source-history runtime hash mismatch/);
 }finally{globalThis.fetch=originalFetch;}
});
