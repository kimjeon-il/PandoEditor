// Authorized exact-commit CI only. No local retry, custom browser/math flags,
// alternate executable/channel, external hosting, or Node-generated goldens.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import {createRequire} from 'node:module';
import {createPage} from './page.mjs';
import {verifySuite,runtimePin} from '../suite.mjs';
import {compareBrowserNative} from '../compare-browser-native.mjs';
assert.equal(process.env.GITHUB_ACTIONS,'true','This browser checkpoint runs only in the authorized remote CI job');
const require=createRequire(import.meta.url),{chromium}=require('@playwright/test');
const input=process.argv[2],output=process.argv[3];assert.ok(input&&output,'Native artifact directory and output required');fs.mkdirSync(output,{recursive:true});
const payload=JSON.parse(fs.readFileSync(path.join(input,'payload.json'))),native=JSON.parse(fs.readFileSync(path.join(input,'native-report.json')));verifySuite(payload);
assert.equal(payload.commit,process.env.GITHUB_SHA);assert.equal(payload.runId,process.env.GITHUB_RUN_ID);assert.deepEqual(native.identity,payload.identity);
const packageVersion=require('@playwright/test/package.json').version;assert.equal(packageVersion,runtimePin.playwright);
const browsers=JSON.parse(fs.readFileSync(path.join(path.dirname(require.resolve('playwright-core/package.json')),'browsers.json'))),chromiumPin=browsers.browsers.find(b=>b.name==='chromium');assert.equal(chromiumPin.revision,runtimePin.revision);assert.equal(chromiumPin.browserVersion,runtimePin.chromium);
const html=createPage(payload);fs.writeFileSync(path.join(output,'oracle.html'),html);
const server=http.createServer((request,response)=>{if(request.url!=='/'){response.writeHead(404);response.end();return;}response.setHeader('Content-Type','text/html; charset=utf-8');response.end(html);});
await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});let browser;
try {
  browser=await chromium.launch({headless:true});const page=await browser.newPage();const cdp=await page.context().newCDPSession(page);const version=await cdp.send('Browser.getVersion');
  fs.writeFileSync(path.join(output,'cdp-runtime.json'),JSON.stringify(version,null,2));
  assert.equal(browser.version(),runtimePin.chromium);assert.equal(version.jsVersion,runtimePin.v8);
  await page.goto('http://127.0.0.1:'+server.address().port+'/');await page.waitForFunction(()=>globalThis.__riverSummary,{},{timeout:180000});
  const summary=await page.evaluate(()=>globalThis.__riverSummary);fs.writeFileSync(path.join(output,'browser-summary.json'),JSON.stringify(summary,null,2));assert.equal(summary.state,'complete',JSON.stringify(summary));
  const report=await page.evaluate(()=>globalThis.__riverReport);report.runtime={...report.runtime,playwright:packageVersion,browserVersion:browser.version(),chromiumRevision:chromiumPin.revision,cdp:version};
  fs.writeFileSync(path.join(output,'browser-report.json'),JSON.stringify(report));
  const comparison=compareBrowserNative(payload,native,report,{commit:process.env.GITHUB_SHA,runId:process.env.GITHUB_RUN_ID});fs.writeFileSync(path.join(output,'direct-comparison.json'),JSON.stringify(comparison,null,2));
  console.log(JSON.stringify({state:comparison.state,exactRaw:comparison.exactRaw,exactPresentation:comparison.exactPresentation,exactWorkspace:comparison.exactWorkspace,annex:comparison.annex},null,2));
}catch(error){fs.writeFileSync(path.join(output,'failure.txt'),String(error)+'\n'+error.stack);throw error;}
finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));}
