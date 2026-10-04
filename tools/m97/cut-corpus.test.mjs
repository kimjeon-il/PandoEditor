import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {runWorkerCalculation} from './web-lifecycle.mjs';
const cases=JSON.parse(readFileSync(new URL('../../tests/fixtures/web-m97/cut-corpus.json',import.meta.url)));
for(const row of cases)test(`production cut calculation: ${row.id}`,async()=>{
 const input=structuredClone(row.payload);
 const result=await runWorkerCalculation('territorial-cut',input);
 assert.deepEqual(result,row.result);
 assert.deepEqual(input,row.payload);
 assert.equal(Object.hasOwn(result,'stages'),false,'calculation results are not lifecycle observations');
});
