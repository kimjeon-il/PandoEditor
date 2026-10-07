import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {createTerritorialEntityLoader} from './source/assets/js/modules/territorial-entity-loader.js';
import {createTerritorialLibraryService} from './source/assets/js/modules/territorial-library-service.js';
const root=new URL('../../../assets/territorial-library-v2/',import.meta.url);
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
async function fixture() {
  const index=await readFile(new URL('index.json',root));let reads=0;
  const loader=createTerritorialEntityLoader({indexUrl:'https://fixture/index.json',dataRevision:'data-e984fd32b177a9ffdaeb9d8e20ca06d6',
    indexSpec:{encoding:'identity',compressedBytes:555560,decodedBytes:555560,sha256:'63c072095fd6c95034365f6d7f9d4e4ff99f71684890bffc8e678fe741b4334c'},cacheStorage:null,
    fetchFn:async url=>{reads++;const file=new URL(url).pathname.split('/').at(-1);return new Response(file==='index.json'?index:await readFile(new URL(file,root)));}});
  const service=createTerritorialLibraryService({loader,today:()=> '2026-10-06'});await service.load();
  return {service,loader,reads:()=>reads};
}
test('all copied Git bytes retain exact ebc source provenance',async()=>{
  const manifestBytes=await readFile(new URL('source-manifest.json',import.meta.url));
  assert.equal(sha(manifestBytes),'1f429460614e1e392e0a3c1c880decd6c45e6c5b9a3a540cea5dd2828bedb0fe');
  const manifest=JSON.parse(manifestBytes);
  assert.equal(manifest.webCommit,'ebcfae4d27b29cbbea6416a7045a4806930204be');assert.equal(manifest.files.length,299);
  for(const entry of manifest.files){const bytes=await readFile(new URL('../../../'+entry.destination,import.meta.url));
    assert.equal(bytes.length,entry.bytes);assert.equal(sha(bytes),entry.sha256);
    assert.equal(createHash('sha1').update(Buffer.from(`blob ${bytes.length}\0`)).update(bytes).digest('hex'),entry.gitBlob);}
});
test('fixed source verifies all284 gzip chunks through the original loader',async()=>{
  const f=await fixture();assert.equal(f.service.list().length,284);assert.equal(f.service.snapshots().length,2);assert.equal(f.reads(),1);
  for(const entry of f.service.list()){const entity=await f.loader.loadEntity(entry.entityId);assert.equal(entity.entityId,entry.entityId);}
  assert.equal(f.reads(),285);
});
test('fixed source confirms literal date search gaps and independent source descriptors',async()=>{
  const f=await fixture(),ids=(q,date)=>f.service.search({query:q,referenceDate:date}).flatMap(g=>g.entities.map(e=>e.entityId));
  assert.deepEqual(ids('kOrEa','2026-10-06'),['state:KOR','state:PRK']);
  assert.deepEqual(ids('Germany','1900-01-01'),['state:DEU','state:east-prussia','state:west-prussia']);
  const gap=f.service.search({query:'독일 민주공화국',referenceDate:'1989-04-24'});assert.equal(gap[0].entities[0].selectedVersionId,null);
  assert.equal(f.reads(),1,'search has no geometry loads');
  await assert.rejects(f.service.instantiateDescriptors(['state:yugoslavia'],'1943'),/경계/);
  const descriptors=await f.service.instantiateDescriptors(['state:ABW'],'2026-10-06');
  assert.equal(descriptors[0].parentEntityId,'state:NLD');assert.equal(descriptors[0].validFrom,null);assert.equal(descriptors[0].name,'아루바');
  assert.equal(f.service.entityRefsWithChildren(['state:soviet-union'],'all','1991-06-01').length,16);
  assert.deepEqual(f.service.entityRefsWithChildren(['state:soviet-union'],'all','1991'),['state:soviet-union']);
});
