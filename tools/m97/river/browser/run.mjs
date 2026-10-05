// Authorized exact-commit CI only. No local retry, custom browser/math flags,
// alternate executable/channel, external hosting, or Node-generated goldens.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import {getHeapStatistics} from 'node:v8';
import {exportBrowserReport} from './report-transfer.mjs';
import {createRequire} from 'node:module';
import {createPage} from './page.mjs';
import {verifySuite,runtimePin} from '../suite.mjs';
import {compareBrowserNative} from '../compare-browser-native.mjs';
assert.equal(process.env.GITHUB_ACTIONS,'true','This browser checkpoint runs only in the authorized remote CI job');
const require=createRequire(import.meta.url),{chromium}=require('@playwright/test');
const input=process.argv[2],output=process.argv[3];assert.ok(input&&output,'Native artifact directory and output required');fs.mkdirSync(output,{recursive:true});
const transferLog={schema:'river-browser-report-transfer-v1',state:'started',heapLimitBytes:getHeapStatistics().heap_size_limit,phases:[]};
const phase=(name,details={})=>{transferLog.phases.push({name,...details,memory:process.memoryUsage()});fs.writeFileSync(path.join(output,'report-transfer.json'),JSON.stringify(transferLog,null,2));};
let payload=JSON.parse(fs.readFileSync(path.join(input,'payload.json')));verifySuite(payload);phase('input-verified');
assert.equal(payload.commit,process.env.GITHUB_SHA);assert.equal(payload.runId,process.env.GITHUB_RUN_ID);
const packageVersion=require('@playwright/test/package.json').version;assert.equal(packageVersion,runtimePin.playwright);
const browsers=JSON.parse(fs.readFileSync(path.join(path.dirname(require.resolve('playwright-core/package.json')),'browsers.json'))),chromiumPin=browsers.browsers.find(b=>b.name==='chromium');assert.equal(chromiumPin.revision,runtimePin.revision);assert.equal(chromiumPin.browserVersion,runtimePin.chromium);
const htmlFile=path.join(output,'oracle.html');fs.writeFileSync(htmlFile,createPage(payload));payload=null;phase('page-persisted');
const server=http.createServer((request,response)=>{if(request.url!=='/'){response.writeHead(404);response.end();return;}response.setHeader('Content-Type','text/html; charset=utf-8');fs.createReadStream(htmlFile).pipe(response);});
await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});let browser;
try {
  browser=await chromium.launch({headless:true});const page=await browser.newPage();const cdp=await page.context().newCDPSession(page);const version=await cdp.send('Browser.getVersion');
  fs.writeFileSync(path.join(output,'cdp-runtime.json'),JSON.stringify(version,null,2));
  assert.equal(browser.version(),runtimePin.chromium);assert.equal(version.jsVersion,runtimePin.v8);
  await page.goto('http://127.0.0.1:'+server.address().port+'/');await page.waitForFunction(()=>globalThis.__riverSummary,{},{timeout:180000});
  const summary=await page.evaluate(()=>globalThis.__riverSummary);fs.writeFileSync(path.join(output,'browser-summary.json'),JSON.stringify(summary,null,2));assert.equal(summary.state,'complete',JSON.stringify(summary));
  const reportFile=path.join(output,'browser-report.json');
  transferLog.artifacts=await exportBrowserReport(page,{reportFile,diagnosticsFile:path.join(output,'browser-area-diagnostics.json'),runtime:{playwright:packageVersion,browserVersion:browser.version(),chromiumRevision:chromiumPin.revision,cdp:version},onPhase:phase});
  transferLog.state='persisted';phase('all-evidence-persisted');
  await browser.close();browser=null;phase('browser-closed');
  // Parse only after durable evidence exists; no nested Playwright report conversion.
  payload=JSON.parse(fs.readFileSync(path.join(input,'payload.json'),'utf8'));
  const native=JSON.parse(fs.readFileSync(path.join(input,'native-report.json'),'utf8'));assert.deepEqual(native.identity,payload.identity);
  const report=JSON.parse(fs.readFileSync(reportFile,'utf8'));phase('comparison-inputs-loaded');
  const comparison=compareBrowserNative(payload,native,report,{commit:process.env.GITHUB_SHA,runId:process.env.GITHUB_RUN_ID});fs.writeFileSync(path.join(output,'direct-comparison.json'),JSON.stringify(comparison,null,2));
  transferLog.state='compared';phase('comparison-complete');
  console.log(JSON.stringify({state:comparison.state,exactRaw:comparison.exactRaw,exactPresentation:comparison.exactPresentation,exactWorkspace:comparison.exactWorkspace,annex:comparison.annex,controllerAnnex:comparison.controllerAnnex,controllerLifecycle:comparison.controllerLifecycle},null,2));
}catch(error){transferLog.state='failed';const failure=String(error).slice(0,16384)+'\n'+String(error.stack||'').slice(0,32768);phase('failed',{error:String(error).slice(0,16384)});fs.writeFileSync(path.join(output,'failure.txt'),failure);console.error(failure);process.exitCode=1;}
finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));}
