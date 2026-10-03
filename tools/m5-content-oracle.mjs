import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import assert from 'node:assert/strict';
import { distributionValueRange, distributionValueAlpha } from '../tests/fixtures/web-current/source/distribution-model.js';
const manifest=JSON.parse(readFileSync(new URL('../tests/fixtures/web-current/source-manifest.json',import.meta.url)));
for(const [file,sha] of Object.entries(manifest.files)) {
 const bytes=Buffer.from(readFileSync(new URL('../tests/fixtures/web-current/source/'+file,import.meta.url),'utf8').replace(/\r\n/g,'\n'));
 assert.equal(createHash('sha1').update('blob '+bytes.length+'\0').update(bytes).digest('hex'),sha,file);
}
const probe=process.argv[2];assert.ok(probe);
const cases=[
 {layers:[{id:'auto'},{id:'empty'},{id:'manual',valueScale:{mode:'manual',min:-10,max:10}}],entries:[{id:'negative',layerId:'auto',value:-100},{id:'large',layerId:'auto',value:1200},{id:'below',layerId:'manual',value:-100},{id:'middle',layerId:'manual',value:0},{id:'above',layerId:'manual',value:100}],opacity:.5},
 {layers:[{id:'constant'}],entries:[{id:'a',layerId:'constant',value:42},{id:'b',layerId:'constant',value:42}]},
 {layers:[{id:'manual',valueScale:{mode:'manual',min:100,max:200}}],entries:[{id:'zero',layerId:'manual',value:0},{id:'huge',layerId:'manual',value:1e12}],opacity:0},
];
for(const test of cases) {
 const ranges=Object.fromEntries(test.layers.map(layer=>[layer.id,distributionValueRange(layer,test.entries.filter(e=>e.layerId===layer.id))]));
 const alpha=Object.fromEntries(test.entries.map(e=>[e.id,distributionValueAlpha(e.value,ranges[e.layerId],test.opacity??1)]));
 const result=spawnSync(probe,[],{input:JSON.stringify(test),encoding:'utf8'});assert.equal(result.status,0,result.stderr);assert.deepEqual(JSON.parse(result.stdout),{ranges,alpha});
}
console.log('Distribution v3 parity passed against '+manifest.commit);
