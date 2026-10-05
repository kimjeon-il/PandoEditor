// Actual Chromium observations may be generated only by authorized exact-commit CI.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import {fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';
import {gzipSync} from 'node:zlib';
import {createCaptureSuite,verifyCaptureSuite,verifyBrowserReport,snapProbeRows,runtimePin} from './protocol.mjs';
import {sha256} from './sources.mjs';
import {writeBoundedJsonArtifact} from '../m97/river/browser/report-transfer.mjs';
export async function runWorkflowOracle({suite,baseUrl}){
 const digest=async text=>[...new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(text)))].map(byte=>byte.toString(16).padStart(2,'0')).join('');
 const require=(value,message)=>{if(!value)throw Error(message);},sourceHashes={};
 for(const [name,source]of Object.entries(suite.sources)){
  const response=await fetch(new URL(name,baseUrl));require(response.ok,'Missing production source '+name);const text=await response.text();sourceHashes[name]=await digest(text);require(sourceHashes[name]===await digest(source),'Production source hash mismatch '+name);
 }
 for(const row of suite.manifest.sources)require(sourceHashes[row.path]===row.sha256,'Production manifest mismatch '+row.path);
 for(const row of Object.values(suite.runtime))require(await digest(row.source)===row.sha256,'Harness source changed');
 for(const vendor of ['polygon-clipping.min.js','d3.min.js'])await new Promise((resolve,reject)=>{const script=document.createElement('script');script.src=new URL('assets/js/vendor/'+vendor,baseUrl);script.onload=resolve;script.onerror=()=>reject(Error('Vendor load failed '+vendor));document.head.append(script);});
 await import(new URL('assets/js/modules/polygon-geometry.js',baseUrl));
 const modules=await Promise.all(suite.manifest.entrypoints.map(name=>import(new URL('assets/js/modules/'+name+'.js',baseUrl))));
 const api={...Object.assign({},...modules),...globalThis.PandoLabPolygonGeometry,d3:globalThis.d3,clipper:globalThis.polygonClipping};
 require(typeof api.createPointerTargets==='function','Actual pointer entrypoint missing');require(typeof api.createTerritorialFeature==='function','Current territorial model missing');
 const loaded={api,sourceTexts:suite.sources,runtime:(0,eval)(suite.runtime.lifecycle.source),createWorker:()=>new Worker(new URL('assets/js/workers/map-edit-worker.js',baseUrl))};
 const {runMathDiagnostic}=(0,eval)(suite.runtime.math.source),math=runMathDiagnostic(api,suite.mathInputs);
 const {runSnapCase}=(0,eval)(suite.runtime.snap.source),{runBoundaryCase}=(0,eval)(suite.runtime.boundary.source),snap=[],boundary=[];
 for(const definition of suite.inputs.snap){snap.push(await runSnapCase(loaded,structuredClone(definition)));globalThis.__m974Summary={state:'running',kind:'snap',completed:snap.length,total:suite.inputs.snap.length};}
 for(const definition of suite.inputs.boundary){boundary.push(await runBoundaryCase(loaded,structuredClone(definition)));globalThis.__m974Summary={state:'running',kind:'boundary',completed:boundary.length,total:suite.inputs.boundary.length};}
 return {schema:'pando-m974-actual-chromium-workflows',version:1,state:'complete',identity:suite.identity,sourceHashes,runtime:{userAgent:navigator.userAgent},snap,boundary,math,observationLimits:{rawParity:false,scope:'Pinned production workflow observations with headless presentation ports. Native helper comparison and native controller comparison are separate required evidence.',boundaryPointerProjection:false,boundaryGpuRendering:false,boundaryDraftUndoRedo:false}};
}
export function createCapturePage(suite){
 verifyCaptureSuite(suite);const encoded=gzipSync(JSON.stringify(suite),{level:9,mtime:0}).toString('base64');
 return `<!doctype html><meta charset="utf-8"><title>M9.7.4 actual workflow capture</title><pre id="status">Starting</pre><script type="module">
try {const bytes=Uint8Array.from(atob('${encoded}'),c=>c.charCodeAt(0));const suite=JSON.parse(await new Response(new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'))).text());
const run=${runWorkflowOracle.toString()};globalThis.__m974Report=await run({suite,baseUrl:new URL('/production/',location.href)});globalThis.__m974Summary={state:'complete',snap:globalThis.__m974Report.snap.length,boundary:globalThis.__m974Report.boundary.length};}
catch(error){globalThis.__m974Summary={state:'failed',error:String(error),stack:error.stack};}
document.querySelector('#status').textContent=JSON.stringify(globalThis.__m974Summary,null,2);
</script>`;
}
export async function exportReport(page,destination,runtime){
 try{
  const metadata=await page.evaluate(async runtime=>{const report=globalThis.__m974Report;if(report?.state!=='complete')throw Error('Missing complete actual workflow observation');report.runtime={...report.runtime,...runtime};const text=globalThis.__m974Export=JSON.stringify(report),bytes=new TextEncoder().encode(text);return {characters:text.length,bytes:bytes.length,sha256:[...new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))].map(byte=>byte.toString(16).padStart(2,'0')).join('')};},runtime);
  return await writeBoundedJsonArtifact(metadata,request=>page.evaluate(({offset,maximum})=>{const text=globalThis.__m974Export;if(typeof text!=='string')throw Error('Missing frozen browser export');let next=Math.min(offset+maximum,text.length);if(next<text.length&&text.charCodeAt(next-1)>=0xd800&&text.charCodeAt(next-1)<=0xdbff)next--;return {offset,text:text.slice(offset,next),next};},request),destination);
 }finally{await page.evaluate(()=>{delete globalThis.__m974Export;}).catch(()=>{});}
}
async function main(){
 assert.equal(process.env.GITHUB_ACTIONS,'true','Actual browser capture runs only in authorized exact-commit CI');const output=process.argv[2];assert.ok(output,'Output directory required');fs.mkdirSync(output,{recursive:true});
 const suite=await createCaptureSuite({commit:process.env.GITHUB_SHA,runId:process.env.GITHUB_RUN_ID});verifyCaptureSuite(suite);
 const suiteText=JSON.stringify(suite);fs.writeFileSync(path.join(output,'suite.json.gz'),gzipSync(suiteText,{level:9,mtime:0}));fs.writeFileSync(path.join(output,'suite-pin.json'),JSON.stringify({decompressedSha256:sha256(suiteText),decompressedBytes:Buffer.byteLength(suiteText),identity:suite.identity},null,2));
 const html=createCapturePage(suite),require=createRequire(new URL('../m97/river/browser/package.json',import.meta.url)),{chromium}=require('@playwright/test'),playwright=require('@playwright/test/package.json').version;
 assert.equal(playwright,runtimePin.playwright);const browsers=JSON.parse(fs.readFileSync(path.join(path.dirname(require.resolve('playwright-core/package.json')),'browsers.json'))),pin=browsers.browsers.find(row=>row.name==='chromium');assert.equal(pin.revision,runtimePin.revision);assert.equal(pin.browserVersion,runtimePin.chromium);
 const server=http.createServer((req,res)=>{if(req.url==='/'){res.setHeader('Content-Type','text/html; charset=utf-8');res.end(html);return;}const key=req.url?.startsWith('/production/')?req.url.slice('/production/'.length):null;if(!key||!Object.hasOwn(suite.sources,key)){res.writeHead(404);res.end();return;}res.setHeader('Content-Type','text/javascript; charset=utf-8');res.end(suite.sources[key]);});
 await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});let browser;
 try{
  browser=await chromium.launch({headless:true});const page=await browser.newPage(),cdp=await page.context().newCDPSession(page),version=await cdp.send('Browser.getVersion');assert.equal(browser.version(),runtimePin.chromium);assert.equal(version.jsVersion,runtimePin.v8);fs.writeFileSync(path.join(output,'cdp-runtime.json'),JSON.stringify(version,null,2));
  await page.goto('http://127.0.0.1:'+server.address().port+'/');await page.waitForFunction(()=>['complete','failed'].includes(globalThis.__m974Summary?.state),{},{timeout:240000});const summary=await page.evaluate(()=>globalThis.__m974Summary);fs.writeFileSync(path.join(output,'browser-summary.json'),JSON.stringify(summary,null,2));assert.equal(summary.state,'complete',JSON.stringify(summary));
  const transfer=await exportReport(page,path.join(output,'browser-report.json'),{playwright,browserVersion:browser.version(),chromiumRevision:pin.revision,cdp:version});fs.writeFileSync(path.join(output,'report-transfer.json'),JSON.stringify(transfer,null,2));
  const text=fs.readFileSync(path.join(output,'browser-report.json'),'utf8'),report=verifyBrowserReport(suite,JSON.parse(text));fs.writeFileSync(path.join(output,'browser-report.json.gz'),gzipSync(text,{level:9,mtime:0}));
  fs.writeFileSync(path.join(output,'math-diagnostics.json'),JSON.stringify(report.math));
  const rows=snapProbeRows(report);fs.writeFileSync(path.join(output,'snap-probe-input.jsonl'),rows.map(row=>JSON.stringify(row)).join('\n')+'\n');fs.writeFileSync(path.join(output,'boundary-observations.json'),JSON.stringify(report.boundary));
  fs.writeFileSync(path.join(output,'capture-verification.json'),JSON.stringify({schema:'pando-m974-capture-verification',identity:suite.identity,passed:true,reportDecompressedSha256:sha256(text),snapCases:report.snap.length,boundaryCases:report.boundary.length,snapHelperRows:rows.length,rawParity:false,limits:report.observationLimits},null,2));console.log(JSON.stringify(summary));
 }catch(error){fs.writeFileSync(path.join(output,'failure.txt'),String(error)+'\n'+error.stack);throw error;}finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))main().catch(error=>{console.error(error);process.exitCode=1;});
