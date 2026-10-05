// Launch only in authorized exact-commit GitHub Actions. No local Chromium capture path.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';
import {gzipSync} from 'node:zlib';
import {createSourceHistorySuite,verifySourceHistorySuite,verifySourceHistoryReport,runtimePin,sourceHistoryObservationLimits} from './source-history-suite.mjs';
import {sha256} from './sources.mjs';
import {writeBoundedJsonArtifact} from '../m97/river/browser/report-transfer.mjs';

export async function runSourceHistoryBrowser({suite,baseUrl}){
 const digest=async text=>[...new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(text)))].map(byte=>byte.toString(16).padStart(2,'0')).join(''),
  require=(value,message)=>{if(!value)throw Error(message);},sourceHashes={};
 for(const [name,source]of Object.entries(suite.sources)){
  const response=await fetch(new URL(name,baseUrl));require(response.ok,'Missing pinned production source '+name);
  const text=await response.text();sourceHashes[name]=await digest(text);require(sourceHashes[name]===await digest(source),'Pinned production source mismatch '+name);
 }
 for(const row of suite.manifest.sources)require(sourceHashes[row.path]===row.sha256,'Production manifest mismatch '+row.path);
 require(await digest(suite.runtime.source)===suite.runtime.sha256,'Source-history runtime hash mismatch');
 for(const vendor of ['polygon-clipping.min.js','d3.min.js'])await new Promise((resolve,reject)=>{
  const script=document.createElement('script');script.src=new URL('assets/js/vendor/'+vendor,baseUrl);script.onload=resolve;script.onerror=()=>reject(Error('Pinned vendor failed '+vendor));document.head.append(script);
 });
 await import(new URL('assets/js/modules/polygon-geometry.js',baseUrl));
 const modules=await Promise.all(suite.manifest.entrypoints.map(name=>import(new URL('assets/js/modules/'+name+'.js',baseUrl)))),
  api={...Object.assign({},...modules),...globalThis.PandoLabPolygonGeometry,d3:globalThis.d3,clipper:globalThis.polygonClipping};
 require(typeof api.createMapEditWorkerClient==='function','Actual production map-edit client missing');
 const runtime=(0,eval)(suite.runtime.source);
 require(typeof runtime.createRuntime==='function','Actual application lifecycle runtime missing');
 require(typeof runtime.runSourceHistoryCase==='function','Source-history runtime entrypoint missing');
 require(typeof runtime.selectionRuntime?.createSelectionRuntime==='function','Actual selection runtime missing');
 const loaded={api,runtime,selectionRuntime:runtime.selectionRuntime,sourceTexts:suite.sources,assemblySource:suite.sources['assets/js/modules/app-domain-assembly.js'],
  createWorker:()=>new Worker(new URL('assets/js/workers/map-edit-worker.js',baseUrl))},cases=[];
 for(const definition of suite.cases)cases.push(await runtime.runSourceHistoryCase(loaded,structuredClone(definition)));
 return {schema:'pando-m974-actual-chromium-source-history',version:1,state:'complete',identity:suite.identity,sourceHashes,runtime:{userAgent:navigator.userAgent},cases,observationLimits:structuredClone(suite.identity.observationLimits)};
}
export function createSourceHistoryPage(suite){
 verifySourceHistorySuite(suite);const encoded=gzipSync(JSON.stringify(suite),{level:9,mtime:0}).toString('base64');
 return `<!doctype html><meta charset="utf-8"><title>M9.7.4 source-history v1 client/Worker diagnostic</title><pre id="status">Starting</pre><script type="module">
try{const bytes=Uint8Array.from(atob('${encoded}'),c=>c.charCodeAt(0));const suite=JSON.parse(await new Response(new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'))).text());
const run=${runSourceHistoryBrowser.toString()};globalThis.__m974SourceHistoryReport=await run({suite,baseUrl:new URL('/production/',location.href)});globalThis.__m974SourceHistorySummary={state:'complete',cases:globalThis.__m974SourceHistoryReport.cases.length};}
catch(error){globalThis.__m974SourceHistorySummary={state:'failed',error:String(error),stack:error.stack};}
document.querySelector('#status').textContent=JSON.stringify(globalThis.__m974SourceHistorySummary,null,2);
</script>`;
}
export async function exportSourceHistoryReport(page,destination,runtime){
 try{
  const metadata=await page.evaluate(async runtime=>{
   const report=globalThis.__m974SourceHistoryReport;if(report?.state!=='complete')throw Error('Missing complete actual source-history observation');
   report.runtime={...report.runtime,...runtime};const text=globalThis.__m974SourceHistoryExport=JSON.stringify(report),bytes=new TextEncoder().encode(text);
   return {characters:text.length,bytes:bytes.length,sha256:[...new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))].map(byte=>byte.toString(16).padStart(2,'0')).join('')};
  },runtime);
  return await writeBoundedJsonArtifact(metadata,request=>page.evaluate(({offset,maximum})=>{
   const text=globalThis.__m974SourceHistoryExport;if(typeof text!=='string')throw Error('Missing frozen source-history export');
   let next=Math.min(offset+maximum,text.length);if(next<text.length&&text.charCodeAt(next-1)>=0xd800&&text.charCodeAt(next-1)<=0xdbff)next--;
   return {offset,text:text.slice(offset,next),next};
  },request),destination);
 }finally{await page.evaluate(()=>{delete globalThis.__m974SourceHistoryExport;}).catch(()=>{});}
}
export function verifySourceHistoryCI(env,head){
 assert.equal(env.GITHUB_ACTIONS,'true','Actual source-history browser capture requires authorized exact-commit CI');
 assert.match(env.GITHUB_SHA||'',/^[a-f0-9]{40}$/,'Exact CI commit required');assert.match(env.GITHUB_RUN_ID||'',/^\d+$/,'Exact GitHub run identity required');
 assert.equal(head,env.GITHUB_SHA,'CI checkout must match exact GITHUB_SHA');
}
async function main(){
 // Refuse before touching output paths, importing Playwright, or launching a browser.
 assert.equal(process.env.GITHUB_ACTIONS,'true','Actual source-history browser capture requires authorized exact-commit CI');
 const root=fileURLToPath(new URL('../../',import.meta.url)),head=execFileSync('git',['-C',root,'rev-parse','HEAD'],{encoding:'utf8'}).trim();
 verifySourceHistoryCI(process.env,head);const output=process.argv[2];assert.ok(output,'Output directory required');
 const suite=await createSourceHistorySuite({commit:process.env.GITHUB_SHA,runId:process.env.GITHUB_RUN_ID}),html=createSourceHistoryPage(suite),suiteText=JSON.stringify(suite);
 fs.mkdirSync(output,{recursive:true});fs.writeFileSync(path.join(output,'suite.json.gz'),gzipSync(suiteText,{level:9,mtime:0}));
 fs.writeFileSync(path.join(output,'suite-pin.json'),JSON.stringify({identity:suite.identity,decompressedSha256:sha256(suiteText),decompressedBytes:Buffer.byteLength(suiteText)},null,2));
 const require=createRequire(new URL('../m97/river/browser/package.json',import.meta.url)),{chromium}=require('@playwright/test'),playwright=require('@playwright/test/package.json').version,
  browsers=JSON.parse(fs.readFileSync(path.join(path.dirname(require.resolve('playwright-core/package.json')),'browsers.json'))),pin=browsers.browsers.find(row=>row.name==='chromium');
 assert.equal(playwright,runtimePin.playwright);assert.equal(pin.revision,runtimePin.revision);assert.equal(pin.browserVersion,runtimePin.chromium);
 const server=http.createServer((req,res)=>{
  if(req.url==='/'){res.setHeader('Content-Type','text/html; charset=utf-8');res.end(html);return;}
  const key=req.url?.startsWith('/production/')?req.url.slice('/production/'.length):null;
  if(!key||!Object.hasOwn(suite.sources,key)){res.writeHead(404);res.end();return;}
  res.setHeader('Content-Type','text/javascript; charset=utf-8');res.end(suite.sources[key]);
 });
 await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});let browser;
 try{
  browser=await chromium.launch({headless:true});const page=await browser.newPage(),cdp=await page.context().newCDPSession(page),version=await cdp.send('Browser.getVersion');
  assert.equal(browser.version(),runtimePin.chromium);assert.equal(version.jsVersion,runtimePin.v8);fs.writeFileSync(path.join(output,'cdp-runtime.json'),JSON.stringify(version,null,2));
  await page.goto('http://127.0.0.1:'+server.address().port+'/');await page.waitForFunction(()=>['complete','failed'].includes(globalThis.__m974SourceHistorySummary?.state),{},{timeout:180000});
  const summary=await page.evaluate(()=>globalThis.__m974SourceHistorySummary);fs.writeFileSync(path.join(output,'browser-summary.json'),JSON.stringify(summary,null,2));assert.equal(summary.state,'complete',JSON.stringify(summary));
  const transfer=await exportSourceHistoryReport(page,path.join(output,'browser-report.json'),{playwright,browserVersion:browser.version(),chromiumRevision:pin.revision,cdp:version});
  fs.writeFileSync(path.join(output,'report-transfer.json'),JSON.stringify(transfer,null,2));
  const text=fs.readFileSync(path.join(output,'browser-report.json'),'utf8'),report=verifySourceHistoryReport(suite,JSON.parse(text));
  fs.writeFileSync(path.join(output,'browser-report.json.gz'),gzipSync(text,{level:9,mtime:0}));fs.writeFileSync(path.join(output,'source-history-observations.json'),JSON.stringify(report.cases));
  fs.writeFileSync(path.join(output,'capture-verification.json'),JSON.stringify({schema:'pando-m974-source-history-capture-verification',version:1,identity:suite.identity,passed:true,cases:report.cases.length,reportDecompressedSha256:sha256(text),rawParity:false,observationLimits:sourceHistoryObservationLimits},null,2));
  console.log(JSON.stringify(summary));
 }catch(error){fs.writeFileSync(path.join(output,'failure.txt'),String(error)+'\n'+error.stack);throw error;}
 finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))main().catch(error=>{console.error(error);process.exitCode=1;});
