import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import test from 'node:test';
import {createTerritorialEntityLoader} from '../../assets/js/modules/territorial-entity-loader.js';
import {territorialDataRoot} from '../../tools/territorial-entity-sources.mjs';
const root=path.join(territorialDataRoot,'generated/v2');
function fixture(corrupt=false,wrongMetadata=false){
 let index=fs.readFileSync(path.join(root,'index.json'));let calls=0;
 if(wrongMetadata){const raw=JSON.parse(index);raw.entities.find(e=>e.entityId==='state:KOR').lineageId='france';raw.lineages.find(l=>l.lineageId==='korea').entityRefs=raw.lineages.find(l=>l.lineageId==='korea').entityRefs.filter(id=>id!=='state:KOR');raw.lineages.find(l=>l.lineageId==='france').entityRefs.push('state:KOR');index=Buffer.from(JSON.stringify(raw));}
 const service=createTerritorialEntityLoader({indexUrl:'https://test/index.json',dataRevision:'test',indexSpec:{encoding:'identity',compressedBytes:index.length,decodedBytes:index.length,sha256:createHash('sha256').update(index).digest('hex')},cacheStorage:null,
 fetchFn:async url=>{calls++;const name=new URL(url).pathname.split('/').at(-1);return new Response(name==='index.json'?index:corrupt?new Uint8Array([1,2,3]):fs.readFileSync(path.join(root,name)));}});
 return {service,calls:()=>calls};
}
test('lazy loader fetches index once and one gzip once for concurrent requests and dated selection',async()=>{
 const f=fixture();const [a,b]=await Promise.all([f.service.loadEntity('state:KOR'),f.service.loadEntity('state:KOR')]);
 assert.equal(a,b);assert.equal(f.calls(),2);assert.equal(f.service.peek('state:KOR'),a);
 assert.equal((await f.service.loadGeometryVersion('state:KOR','2026-10-06')).versionId,'state:KOR:natural-earth-5.1.1');
 assert.equal(f.calls(),2);await assert.rejects(f.service.loadEntity('state:missing'),/Unknown/);
});

test('valid index integrity cannot publish a chunk with mismatching lineage metadata',async()=>{
 const f=fixture(false,true);await assert.rejects(f.service.loadEntity('state:KOR'),/metadata mismatch/);
 assert.equal(f.service.peek('state:KOR'),null);
});
test('stored hash mismatch never publishes an entity and failed requests can retry',async()=>{
 const f=fixture(true);await assert.rejects(f.service.loadEntity('state:KOR'),/크기|무결성/);
 assert.equal(f.service.peek('state:KOR'),null);const calls=f.calls();await assert.rejects(f.service.loadEntity('state:KOR'));assert.ok(f.calls()>calls);
});
