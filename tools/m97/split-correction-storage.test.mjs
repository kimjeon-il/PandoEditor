import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import {createHash} from 'node:crypto';import {gunzipSync} from 'node:zlib';import {observationParts} from './capture-approved-split-correction.mjs';
test('approved capture parts retain every raw observation value in exact order',()=>{
 const root=new URL('../../tests/fixtures/web-m973-split/corrections/dateline/',import.meta.url),manifest=JSON.parse(fs.readFileSync(new URL('observation-manifest.json',root))),cases=[];
 for(const row of manifest.parts){const bytes=fs.readFileSync(new URL(row.path,root));assert.equal(createHash('sha256').update(bytes).digest('hex'),row.sha256);const raw=gunzipSync(bytes);assert.equal(createHash('sha256').update(raw).digest('hex'),row.uncompressedSha256);const value=JSON.parse(raw);assert.equal(value.behavioralCommit,manifest.behavioralCommit);assert.deepEqual(value.cases.map(item=>item.case),row.caseIds);cases.push(...value.cases);}
 assert.deepEqual(cases.map(row=>row.case),manifest.caseIds);assert.equal(cases.length,37);
 const parts=observationParts({schema:'storage-test',cases},180000),restored=parts.flatMap(row=>JSON.parse(gunzipSync(row.bytes)).cases);assert.deepEqual(restored,cases);assert.ok(parts.every(row=>row.bytes.length<=180000));
});
