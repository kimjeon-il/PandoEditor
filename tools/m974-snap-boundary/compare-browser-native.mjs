import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {gunzipSync} from 'node:zlib';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {sha256} from './sources.mjs';
import {verifyCaptureSuite,verifyBrowserReport,snapProbeRows,compareSnapProbe} from './protocol.mjs';
export function nativeProbeInput(row){const {candidates,result,indicator,...input}=row;void candidates;void result;void indicator;return input;}
export function compareCapture({browserDirectory,nativeExecutable,outputDirectory,commit}){
 assert.match(commit||'',/^[a-f0-9]{40}$/,'Exact native/application commit required');
 const pin=JSON.parse(fs.readFileSync(path.join(browserDirectory,'suite-pin.json'))),suiteBytes=gunzipSync(fs.readFileSync(path.join(browserDirectory,'suite.json.gz')));
 assert.equal(sha256(suiteBytes),pin.decompressedSha256,'captured suite decompressed SHA');assert.equal(suiteBytes.length,pin.decompressedBytes);
 const suite=verifyCaptureSuite(JSON.parse(suiteBytes));assert.equal(suite.commit,commit,'browser/native exact commit');assert.deepEqual(suite.identity,pin.identity);
 const reportText=fs.readFileSync(path.join(browserDirectory,'browser-report.json'),'utf8'),transfer=JSON.parse(fs.readFileSync(path.join(browserDirectory,'report-transfer.json')));assert.equal(sha256(reportText),transfer.sha256);assert.equal(Buffer.byteLength(reportText),transfer.bytes);
 const report=verifyBrowserReport(suite,JSON.parse(reportText)),expected=snapProbeRows(report),actual=[];fs.mkdirSync(outputDirectory,{recursive:true});
 for(const row of expected){const process=spawnSync(nativeExecutable,[],{input:JSON.stringify(nativeProbeInput(row)),encoding:'utf8',timeout:30000,maxBuffer:16*1024*1024});
  if(process.error||process.status!==0){actual.push({case:row.case,queryIndex:row.queryIndex,error:String(process.error||process.stderr||'Native process failed'),exitCode:process.status});continue;}
  try{actual.push(JSON.parse(process.stdout));}catch(error){actual.push({case:row.case,queryIndex:row.queryIndex,error:'Native output is not a single JSON observation: '+error.message});}
 }
 const comparison={...compareSnapProbe(expected,actual),identity:suite.identity,reportSha256:transfer.sha256,nativeExecutable:path.basename(nativeExecutable)};
 fs.writeFileSync(path.join(outputDirectory,'snap-native-observations.json'),JSON.stringify(actual));fs.writeFileSync(path.join(outputDirectory,'snap-helper-comparison.json'),JSON.stringify(comparison,null,2)+'\n');return comparison;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const [browserDirectory,nativeExecutable,outputDirectory]=process.argv.slice(2);try{const report=compareCapture({browserDirectory,nativeExecutable,outputDirectory,commit:process.env.GITHUB_SHA});console.log(JSON.stringify({passed:report.passed,expected:report.expected,actual:report.actual,failures:report.failures.slice(0,8)}));if(!report.passed)process.exitCode=1;}catch(error){console.error(error);process.exitCode=1;}
}
