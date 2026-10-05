import test from 'node:test';
import assert from 'node:assert/strict';
const modulePromise=import('./contract.mjs').catch(()=>null);
test('whole-file comparison preserves ordered IDs, geometry precision, archive membership and unknown fields',async()=>{
 const c=await modulePromise;assert.ok(c,'M975 strict contract module must exist');
 const a={territorialEntities:[{id:'B'},{id:'A'}],geometries:[{id:'shape',version:1,geojson:{type:'Point',coordinates:[0.1,0]}}],labels:[],savedAt:'fixed',custom:1};
 assert.doesNotThrow(()=>c.assertExchangeEqual(a,structuredClone(a)));
 for(const mutate of [v=>v.territorialEntities.reverse(),v=>v.geometries[0].geojson.coordinates[0]+=1e-15,v=>v.geometries.push({...v.geometries[0],version:2}),v=>delete v.custom,v=>v.extra=true]){const b=structuredClone(a);mutate(b);assert.throws(()=>c.assertExchangeEqual(a,b));}
});
test('inventory rejects missing, duplicate, reordered and extra cases and stages',async()=>{
 const c=await modulePromise;assert.ok(c,'M975 strict contract module must exist');
 const expected=[{id:'a',stages:['before','confirm']}],rows=[{case:'a',stages:{before:{},confirm:{}}}];
 assert.doesNotThrow(()=>c.verifyInventory(expected,rows));
 for(const changed of [[],[...rows,...rows],[{case:'extra',stages:rows[0].stages}],[{case:'a',stages:{before:{}}}],[{case:'a',stages:{confirm:{},before:{}}}],[{case:'a',stages:{...rows[0].stages,extra:{}}}]])assert.throws(()=>c.verifyInventory(expected,changed));
});
test('raw checkpoint transfer verifies UTF-8 bytes and SHA, not parsed reconstruction',async()=>{
 const c=await modulePromise;assert.ok(c,'M975 strict contract module must exist');
 const raw='{ "name": "서울", "n": 1.2300e+02 }\n',row=c.rawRecord(raw);assert.equal(c.verifyRawRecord(row),raw);
 for(const changed of [{...row,raw:raw.trim()},{...row,bytes:row.bytes+1},{...row,sha256:'0'.repeat(64)}])assert.throws(()=>c.verifyRawRecord(changed));
});
test('actual-browser gate refuses non-CI and wrong HEAD before execution',async()=>{
 const c=await modulePromise;assert.ok(c,'M975 strict contract module must exist');
 assert.throws(()=>c.verifyCI({},'a'.repeat(40)));
 assert.throws(()=>c.verifyCI({GITHUB_ACTIONS:'true',GITHUB_SHA:'a'.repeat(40),GITHUB_RUN_ID:'123'},'b'.repeat(40)));
 assert.doesNotThrow(()=>c.verifyCI({GITHUB_ACTIONS:'true',GITHUB_SHA:'a'.repeat(40),GITHUB_RUN_ID:'123'},'a'.repeat(40)));
});
test('native return import permits only documentId recreation and preserves exact GeometryRefs',async()=>{
 const c=await modulePromise;assert.equal(typeof c.assertNativeExchangeEqual,'function','native full-document comparison required');const a={format:'pandoeditor-project',version:9,documentId:'source',content:{labels:[{id:'label',geometry:{id:'exact',version:2}}]}};const b=structuredClone(a);b.documentId='reimported';assert.doesNotThrow(()=>c.assertNativeExchangeEqual(a,b));b.content.labels[0].geometry.version=1;assert.throws(()=>c.assertNativeExchangeEqual(a,b));
});
