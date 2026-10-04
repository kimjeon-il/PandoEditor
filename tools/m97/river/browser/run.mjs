// This runner is exercised in the branch's official Chromium CI gate only.
// No custom executable path, alternate channel, browser flags, or remote hosting.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import {createRequire} from 'node:module';
import {createPayload,createPage} from './page.mjs';
const require=createRequire(import.meta.url),{chromium}=require('@playwright/test');
const output=process.argv[2]||'evidence/river-chromium';fs.mkdirSync(output,{recursive:true});
const packageVersion=require('@playwright/test/package.json').version;assert.equal(packageVersion,'1.62.1');
const browsers=JSON.parse(fs.readFileSync(path.join(path.dirname(require.resolve('playwright-core/package.json')),'browsers.json')));
const chromiumPin=browsers.browsers.find(b=>b.name==='chromium');assert.equal(chromiumPin.revision,'1234');assert.equal(chromiumPin.browserVersion,'151.0.7922.34');
const manifest=process.env.PANDOEDITOR_HYDRO_FULL_MANIFEST;assert.ok(manifest,'Required full hydro source missing');
const payload=await createPayload(path.resolve(path.dirname(manifest),'../v0.13.0/manifest.json'));
fs.writeFileSync(path.join(output,'node-expected.json'),JSON.stringify({node:payload.node,expected:payload.expected,provenance:payload.provenance},null,2));
const pageHtml=createPage(payload);fs.writeFileSync(path.join(output,'oracle.html'),pageHtml);
const server=http.createServer((request,response)=>{if(request.url!=='/'){response.writeHead(404);response.end();return;}response.setHeader('Content-Type','text/html; charset=utf-8');response.end(pageHtml);});
await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});
let browser;
try{
  browser=await chromium.launch({headless:true});
  const page=await browser.newPage();await page.goto(`http://127.0.0.1:${server.address().port}/`);
  await page.waitForFunction(()=>globalThis.__riverSummary,{},{timeout:120000});
  const summary=await page.evaluate(()=>globalThis.__riverSummary);summary.playwright=packageVersion;summary.chromiumPin=chromiumPin;summary.browserVersion=browser.version();summary.commit=process.env.GITHUB_SHA||null;
  fs.writeFileSync(path.join(output,'browser-summary.json'),JSON.stringify(summary,null,2));
  const observations=await page.evaluate(()=>globalThis.__riverFullOutputs||[]);fs.writeFileSync(path.join(output,'browser-observations.json'),JSON.stringify(observations));
  assert.equal(summary.state,'complete');assert.equal(summary.total,24);assert.equal(summary.exactMatches,24);assert.equal(summary.synthetic,18);assert.equal(summary.actualSource,6);
  assert.equal(summary.browserVersion,chromiumPin.browserVersion);assert.ok(summary.userAgent.includes('Chrome/'));
  console.log(JSON.stringify(summary,null,2));
}catch(error){fs.writeFileSync(path.join(output,'failure.txt'),String(error)+'\n'+error.stack);throw error;}
finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));}
