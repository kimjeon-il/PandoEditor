import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {loadOracle} from './calculations.mjs';
const fixture=new URL('../../tests/fixtures/web-m97/',import.meta.url);
const corpus=JSON.parse(readFileSync(new URL('calculation-corpus.json',fixture)));
const expected=JSON.parse(readFileSync(new URL('calculation-expected.json',fixture)));
const oracle=await loadOracle();
assert.equal(expected.sourceCommit,oracle.manifest.behavioralCommit);
assert.equal(new Set(corpus.cases.map(x=>x.id)).size,corpus.cases.length);
assert.deepEqual(expected.outcomes.map(x=>x.id),corpus.cases.map(x=>x.id));
for(const row of corpus.cases)test(`production web corpus: ${row.id}`,()=>{
 const before=structuredClone(row);
 assert.deepEqual({id:row.id,...oracle.calculate(row)},expected.outcomes.find(x=>x.id===row.id));
 assert.deepEqual(row,before,'fixture input remains immutable');
});
