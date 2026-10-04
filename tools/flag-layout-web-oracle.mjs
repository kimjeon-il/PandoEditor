import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {readFileSync} from 'node:fs';
import {layoutCountryFlags} from '../tests/fixtures/web-hydro/source/country-label-flags.js';

// Pinned upstream flag algorithm; current territorial module has the same rule.
const bytes = Buffer.from(readFileSync(new URL('../tests/fixtures/web-hydro/source/country-label-flags.js', import.meta.url), 'utf8').replace(/\r\n/g, '\n'));
assert.equal(createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`),bytes])).digest('hex'),
    'f95de74a7d51b80a83dd94d9cfb00f9a3bf7be86');
const cases=JSON.parse(execFileSync(process.argv[2],{encoding:'utf8'}));
for(const [index,{zoom,input,actual}] of cases.entries()) {
    const eligible=input.filter(item=>item.nameVisible||(item.source.available&&zoom>=1.8));
    const flags=layoutCountryFlags(eligible,{zoom,enabled:true,isVisible:()=>true,
        flagUrl:source=>source.available?`/${source.id}.svg`:null});
    const expected=eligible.map(item=>({id:item.source.id,name:item.nameVisible,flag:flags.has(item.source.id)}))
        .filter(item=>item.name||item.flag);
    assert.deepEqual(actual,expected,`flag layout case ${index}, zoom ${zoom}`);
}
console.log(`Native/web flag decoration: ${cases.length} ordered, cross-group and flag-only cases passed`);
