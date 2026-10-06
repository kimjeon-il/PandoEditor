// Actual Chromium is restricted to authorized exact-commit CI. Importing this
// module or exercising its transfer/validation contracts does not launch it.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';
import {gzipSync} from 'node:zlib';
import {createPendingSuite,verifyPendingSuite,verifyPendingReport,createPendingCaptureBinding,runtimePin,pendingObservationLimits} from './suite.mjs';
import {writeBoundedJsonArtifact} from '../m97/river/browser/report-transfer.mjs';

export function verifyPendingCI(env,head){
 assert.equal(env.GITHUB_ACTIONS,'true','Actual pending-input browser capture requires authorized exact-commit CI');
 assert.match(env.GITHUB_SHA||'',/^[a-f0-9]{40}$/,'Exact CI commit required');
 assert.match(env.GITHUB_RUN_ID||'',/^\d+$/,'Exact GitHub run identity required');
 assert.equal(head,env.GITHUB_SHA,'CI checkout must match exact GITHUB_SHA');
}
export function pendingContextOptions(profile){
 assert.ok(profile&&['mouse','touch'].includes(profile.pointerType),'Explicit mouse or touch profile required');
 assert.ok(Number.isSafeInteger(profile.width)&&profile.width>0&&Number.isSafeInteger(profile.height)&&profile.height>0);
 return {viewport:{width:profile.width,height:profile.height},deviceScaleFactor:1,hasTouch:profile.pointerType==='touch',isMobile:profile.pointerType==='touch'};
}
export async function runPendingBrowserCase({suite,caseId,baseUrl}){
 const require=(value,message)=>{if(!value)throw Error(message);};
 require(typeof process==='undefined'&&typeof document==='object'&&typeof Worker==='function'&&typeof navigator==='object'&&/Chrome\//.test(navigator.userAgent),'Pending-input observation requires an actual Chromium page');
 const digest=async text=>[...new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(text)))].map(byte=>byte.toString(16).padStart(2,'0')).join('');
 const definition=suite.corpus.cases.find(row=>row.id===caseId);require(definition,'Unknown pending-input case');
 const context={case:caseId,viewport:{width:innerWidth,height:innerHeight},userAgent:navigator.userAgent,devicePixelRatio,
  hasTouch:navigator.maxTouchPoints>0,node:typeof process!=='undefined',document:typeof document==='object',worker:typeof Worker==='function'};
 require(context.viewport.width===definition.profile.width&&context.viewport.height===definition.profile.height,'Actual viewport must match declared case');
 require(context.hasTouch===(definition.profile.pointerType==='touch')&&context.devicePixelRatio===1,'Actual pointer capability and device scale must match declared case');
 const sourceHashes={};
 for(const [name,source] of Object.entries(suite.sources)){
  const response=await fetch(new URL(name,baseUrl));require(response.ok,'Missing pinned production source '+name);
  sourceHashes[name]=await digest(await response.text());require(sourceHashes[name]===await digest(source),'Pinned production source mismatch '+name);
 }
 for(const row of [...suite.manifest.sources,suite.addendum.supplementary])require(sourceHashes[row.path]===row.sha256,'Immutable production manifest mismatch '+row.path);
 for(const [name,runtime] of [['lifecycle',suite.runtime],['pending',suite.pendingRuntime]])require(await digest(runtime.source)===runtime.sha256,'Verified runtime hash required '+name);
 for(const vendor of ['polygon-clipping.min.js','d3.min.js'])await new Promise((resolve,reject)=>{
  const script=document.createElement('script');script.src=new URL('assets/js/vendor/'+vendor,baseUrl);script.onload=resolve;script.onerror=()=>reject(Error('Pinned vendor failed '+vendor));document.head.append(script);
 });
 await import(new URL('assets/js/modules/polygon-geometry.js',baseUrl));
 const modules=await Promise.all([...suite.manifest.entrypoints,'tool-controller'].map(name=>import(new URL('assets/js/modules/'+name+'.js',baseUrl))));
 const api={...Object.assign({},...modules),...globalThis.PandoLabPolygonGeometry,d3:globalThis.d3,clipper:globalThis.polygonClipping};
 for(const name of ['createMapEditWorkerClient','createEditingDomain','toolDraftDefinition','createObjectPicking'])require(typeof api[name]==='function','Actual production entrypoint required '+name);
 const runtime=(0,eval)(suite.runtime.source),run=(0,eval)('('+suite.pendingRuntime.source+')');
 require(typeof runtime.createRuntime==='function'&&typeof runtime.selectionRuntime?.createSelectionRuntime==='function','Actual lifecycle and selection runtime required');
 const loaded={api,runtime,selectionRuntime:runtime.selectionRuntime,sourceTexts:suite.sources,assemblySource:suite.sources['assets/js/modules/app-domain-assembly.js'],
  createWorker:()=>new Worker(new URL('assets/js/workers/map-edit-worker.js',baseUrl))};
 return {schema:'pando-m977-actual-chromium-pending-input-row',version:1,state:'complete',identity:suite.identity,sourceHashes,context,
  observation:await run(loaded,structuredClone(definition),structuredClone(suite.corpus))};
}
export function createPendingPage(suite,caseId){
 verifyPendingSuite(suite);assert.ok(suite.corpus.cases.some(row=>row.id===caseId),'Unknown pending-input case');
 const encoded=gzipSync(JSON.stringify(suite),{level:9,mtime:0}).toString('base64');
 return `<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>M9.7.7 public pending-input observation</title><pre id="status">Starting</pre><script type="module">
try{const bytes=Uint8Array.from(atob('${encoded}'),c=>c.charCodeAt(0));const suite=JSON.parse(await new Response(new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'))).text());
const run=${runPendingBrowserCase.toString()};globalThis.__m977PendingRow=await run({suite,caseId:${JSON.stringify(caseId)},baseUrl:new URL('/production/',location.href)});globalThis.__m977PendingSummary={state:'complete',case:globalThis.__m977PendingRow.observation.case};}
catch(error){globalThis.__m977PendingSummary={state:'failed',error:String(error),stack:error.stack};}
document.querySelector('#status').textContent=JSON.stringify(globalThis.__m977PendingSummary,null,2);
</script>`;
}
export async function exportPendingRow(page,destination,options={}){
 try{
  const metadata=await page.evaluate(async()=>{
   const row=globalThis.__m977PendingRow;if(row?.state!=='complete')throw Error('Missing complete actual pending-input observation');
   const text=globalThis.__m977PendingExport=JSON.stringify(row),bytes=new TextEncoder().encode(text);
   return {characters:text.length,bytes:bytes.length,sha256:[...new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))].map(byte=>byte.toString(16).padStart(2,'0')).join('')};
  });
  return await writeBoundedJsonArtifact(metadata,request=>page.evaluate(({offset,maximum})=>{
   const text=globalThis.__m977PendingExport;if(typeof text!=='string')throw Error('Missing frozen pending-input row');
   let next=Math.min(offset+maximum,text.length);if(next<text.length&&text.charCodeAt(next-1)>=0xd800&&text.charCodeAt(next-1)<=0xdbff)next--;
   return {offset,text:text.slice(offset,next),next};
  },request),destination,options);
 }finally{await page.evaluate(()=>{delete globalThis.__m977PendingExport;}).catch(()=>{});}
}
async function main(){
 // Refuse before output creation, Playwright import, or browser launch.
 assert.equal(process.env.GITHUB_ACTIONS,'true','Actual pending-input browser capture requires authorized exact-commit CI');
 const root=fileURLToPath(new URL('../../',import.meta.url)),head=execFileSync('git',['-C',root,'rev-parse','HEAD'],{encoding:'utf8'}).trim();
 verifyPendingCI(process.env,head);const output=process.argv[2];assert.ok(output,'Output directory required');
 const suite=await createPendingSuite({commit:head,runId:process.env.GITHUB_RUN_ID}),suiteText=JSON.stringify(suite);
 const require=createRequire(new URL('../m97/river/browser/package.json',import.meta.url)),{chromium}=require('@playwright/test'),playwright=require('@playwright/test/package.json').version;
 const browsers=JSON.parse(fs.readFileSync(path.join(path.dirname(require.resolve('playwright-core/package.json')),'browsers.json'))),pin=browsers.browsers.find(row=>row.name==='chromium');
 assert.equal(playwright,runtimePin.playwright);assert.equal(pin.revision,runtimePin.revision);assert.equal(pin.browserVersion,runtimePin.chromium);
 assert.equal(fs.existsSync(output),false,'Capture output must be a fresh directory');fs.mkdirSync(output,{recursive:true});
 fs.writeFileSync(path.join(output,'suite.json.gz'),gzipSync(suiteText,{level:9,mtime:0}));
 const pages=new Map(suite.corpus.cases.map(definition=>['/case/'+definition.id,createPendingPage(suite,definition.id)]));
 const server=http.createServer((req,res)=>{
  if(pages.has(req.url)){res.setHeader('Content-Type','text/html; charset=utf-8');res.end(pages.get(req.url));return;}
  const key=req.url?.startsWith('/production/')?req.url.slice('/production/'.length):null;
  if(!key||!Object.hasOwn(suite.sources,key)){res.writeHead(404);res.end();return;}
  res.setHeader('Content-Type','text/javascript; charset=utf-8');res.end(suite.sources[key]);
 });
 await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});let browser;
 try{
  browser=await chromium.launch({headless:true});assert.equal(browser.version(),runtimePin.chromium);
  const cdp=await browser.newBrowserCDPSession(),version=await cdp.send('Browser.getVersion');
  assert.equal(version.jsVersion,runtimePin.v8);assert.ok(['Chrome/','HeadlessChrome/'].some(prefix=>version.product===prefix+runtimePin.chromium));
  fs.writeFileSync(path.join(output,'cdp-runtime.json'),JSON.stringify(version,null,2));
  const rows=[],transfers=[];
  for(const definition of suite.corpus.cases){
   const context=await browser.newContext(pendingContextOptions(definition.profile));
   try{
    const page=await context.newPage();await page.goto('http://127.0.0.1:'+server.address().port+'/case/'+definition.id);
    await page.waitForFunction(()=>['complete','failed'].includes(globalThis.__m977PendingSummary?.state),{},{timeout:180000});
    const summary=await page.evaluate(()=>globalThis.__m977PendingSummary);assert.equal(summary.state,'complete',JSON.stringify(summary));
    const destination=path.join(output,definition.id+'.json');transfers.push({case:definition.id,...await exportPendingRow(page,destination)});
    const row=JSON.parse(fs.readFileSync(destination,'utf8'));assert.equal(row.schema,'pando-m977-actual-chromium-pending-input-row');assert.equal(row.version,1);assert.equal(row.state,'complete');
    assert.deepEqual(row.identity,suite.identity);assert.equal(row.observation.case,definition.id);rows.push(row);
   }finally{await context.close();}
  }
  for(const row of rows)assert.deepEqual(row.sourceHashes,rows[0].sourceHashes,'Every context verified the same source closure');
  const report=verifyPendingReport(suite,{schema:'pando-m977-actual-chromium-pending-input',version:1,state:'complete',identity:suite.identity,sourceHashes:rows[0].sourceHashes,
   runtime:{collector:'actual-chromium',playwright,browserVersion:browser.version(),chromiumRevision:pin.revision,cdp:version,ci:{commit:head,runId:process.env.GITHUB_RUN_ID}},
   contexts:rows.map(row=>row.context),cases:rows.map(row=>row.observation),observationLimits:pendingObservationLimits});
  const reportText=JSON.stringify(report),binding=createPendingCaptureBinding(suiteText,reportText);
  fs.writeFileSync(path.join(output,'browser-report.json'),reportText);fs.writeFileSync(path.join(output,'browser-report.json.gz'),gzipSync(reportText,{level:9,mtime:0}));
  fs.writeFileSync(path.join(output,'report-transfers.json'),JSON.stringify(transfers,null,2));
  fs.writeFileSync(path.join(output,'capture-verification.json'),JSON.stringify(binding,null,2));
  console.log(JSON.stringify({state:'complete',cases:binding.cases,stages:binding.stages,rawParity:false}));
 }catch(error){fs.writeFileSync(path.join(output,'failure.txt'),String(error)+'\n'+error.stack);throw error;}
 finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))main().catch(error=>{console.error(error);process.exitCode=1;});
