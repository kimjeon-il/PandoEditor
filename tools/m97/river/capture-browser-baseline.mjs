// Reproduce compact goldens only from the authenticated, real Chromium artifact.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {gunzipSync} from 'node:zlib';
import {canonical,sha256,outputHash} from './oracle.mjs';
const input=process.argv[2],output=process.argv[3];assert.ok(input&&output,'artifact directory and output file required');
const html=fs.readFileSync(path.join(input,'oracle.html'));
assert.equal(sha256(html),'96008a186f67ad49f1ebcc42ab69e84ff68d1bd43d06d3827d1e238d2fedc670');
const encoded=html.toString().match(/atob\('([^']+)'\)/)[1],compressed=Buffer.from(encoded,'base64'),decoded=gunzipSync(compressed);
assert.equal(sha256(compressed),'fcc8cbd8f88972f26303aa59022b43362a25967e4128621a32f1172621723467');
assert.equal(sha256(decoded),'7dc951b87b117704730575a62988610940215aff7bac39343e5efd567e8e1962');
for(const [name,hash] of Object.entries({'browser-summary.json':'ce46675d954a5b6c4ff61c0d818a704fb412ebae4f6ce3201bfe2b6454e9101b','browser-observations.json':'fd9ffb96c63ebefb710dd394aaaa912ca4da545d53b0ab4397235e301571ec4a','node-expected.json':'ed1d8cf3c32b925e3e821c202334b72402600249dcf1a6cc1f1251b9887756c3'}))assert.equal(sha256(fs.readFileSync(path.join(input,name))),hash,name);
const payload=JSON.parse(decoded),summary=JSON.parse(fs.readFileSync(path.join(input,'browser-summary.json'))),observations=JSON.parse(fs.readFileSync(path.join(input,'browser-observations.json')));
assert.equal(summary.total,24);assert.equal(observations.length,24);assert.equal(summary.browserVersion,'151.0.7922.34');assert.equal(summary.commit,'e2eed653099629bc30fa62b2441b4acb3fe7ba4f');
const cases=payload.cases.map((row,i)=>{assert.equal(observations[i].name,row.name);const hash=outputHash(observations[i]);assert.equal(hash,summary.observations[i].sha256);return {name:row.name,inputSha256:sha256(JSON.stringify(canonical(row))),outputSha256:hash,cells:observations[i].result.candidates.length,keys:observations[i].result.candidates.map(c=>c.key),nodeOutputSha256:payload.expected[row.name].sha256};});
const baseline={schema:'actual-chromium-river-baseline-v1',runtime:{playwright:'1.62.1',chromium:'151.0.7922.34',revision:'1234',v8:'15.1.206.8',v8Commit:'f479186c16abdb6fa05539fe957bb84deee830df',v8Evidence:'Version from pinned Chromium DEPS and official V8 version header; original capture did not record CDP jsVersion.'},capture:{run:'37231724016',commit:summary.commit,artifactZipSha256:'9b6444eb8e494bf66a913683ab71a1fd0546c6037c29ad43626d0f6b4087ed15',files:Object.fromEntries(['oracle.html','browser-summary.json','browser-observations.json','node-expected.json'].map(name=>[name,sha256(fs.readFileSync(path.join(input,name)))])),casesSha256:sha256(JSON.stringify(canonical(payload.cases))),sourceHashes:summary.sourceHashes,dataset:summary.provenance},nodeDiagnostic:{version:payload.node.version,v8:payload.node.v8,exactMatches:18,total:24,role:'Different-engine negative evidence; never a browser golden proxy.'},cases};
fs.writeFileSync(output,JSON.stringify(baseline,null,2)+'\n');console.log('Derived 24 exact browser baselines from verified capture.');
