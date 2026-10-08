import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,mkdirSync,writeFileSync,readFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {gzipSync} from 'node:zlib';
import {compare,gateReport,sha256} from './compare-territorial-library.mjs';

const source = (id,start=null) => ({
  schemaVersion:2,
  entityId:id,entityKind:'general',names:{ko:id},alternateNames:[],
  lifetime:{validFrom:start,validTo:null},parentEntityId:'',
  geometryVersions:[{versionId:id+':current',validFrom:null,validTo:null,
    datePrecision:'current',certainty:'high',sourceId:'fixture',
    geometry:{type:'Polygon',coordinates:[[[0,0],[1,0],[1,1],[0,0]]]}}],
  instantiation:{mode:'independent',countryUpdates:{}},
  metadata:{sourceFeatureId:id},sourceInfo:{title:'Fixture',license:'Public domain',sourceId:'fixture'},
  lineageId:id.slice(6)
});
const ids=['state:AAA','state:BBB'];
const initial=()=>ids.map(id=>source(id));
const make=(root,entities,{level=9,format=2}={})=>{
  mkdirSync(root,{recursive:true});
  const rows=entities.map(e=>{
    const raw=Buffer.from(JSON.stringify(e,null,format)+'\n');
    const file=e.entityId.replace(':','-')+'.json.gz';
    const stored=gzipSync(raw,{level,mtime:0});
    writeFileSync(join(root,file),stored);
    return {...e,geometryVersions:e.geometryVersions.map(({geometry,...info})=>info),
      file,validFrom:e.lifetime.validFrom,validTo:e.lifetime.validTo,
      compressedBytes:stored.length,decodedBytes:raw.length,sha256:sha256(stored),
      bbox:[0,0,1,1],geometryVersionCount:e.geometryVersions.length};
  });
  const index={schemaVersion:2,lineages:entities.map(e=>({schemaVersion:1,
    lineageId:e.lineageId,names:{ko:e.entityId},relations:[],entityRefs:[e.entityId]})),
    entities:rows,snapshots:[{schemaVersion:1,snapshotId:'test',referenceDate:'2026',
      entityRefs:entities.map(e=>e.entityId)}]};
  writeFileSync(join(root,'index.json'),JSON.stringify(index,null,2)+'\n');
};
function pair(fn){
  const root=mkdtempSync(join(tmpdir(),'pando-territorial-crosscheck-'));
  const web=join(root,'web'),app=join(root,'app');
  try{return fn(web,app);}finally{rmSync(root,{recursive:true,force:true});}
}
const check=(w,a,mode)=>({result:compare(w,a),errors:gateReport(compare(w,a),mode)});

test('pinned historical catalog: all stored bytes and index exactly match',()=>pair((web,app)=>{
  make(web,initial());make(app,initial());
  const {result:r,errors}=check(web,app,'exact');
  assert.deepEqual(errors,[]);
  assert.equal(r.indexBytesEqual,true);
  assert.equal(r.counts['byte-identical'],2);
}));

test('only lifetime start dates changed: source preserved; exact gate blocks',()=>pair((web,app)=>{
  const newer=initial();newer[0].lifetime.validFrom='1975-11-11';newer[1].lifetime.validFrom='1912-11-28';
  make(web,newer);make(app,initial());
  const r=compare(web,app);
  assert.equal(r.counts['lifetime-start-only'],2);
  assert.equal(r.indexUnexpectedChanges.length,0);
  assert.deepEqual(gateReport(r,'lifetime-only'),[]);
  assert.notDeepEqual(gateReport(r,'exact'),[]);
}));

test('same uncompressed JSON, different gzip settings is classified, not conflated with source drift',()=>pair((web,app)=>{
  const entities=initial();
  entities[0].metadata.notes='a long validation payload, '.repeat(150);
  make(web,entities,{level:1});make(app,entities,{level:9});
  const r=compare(web,app);
  assert.equal(r.counts['gzip-only'],2);
  assert.deepEqual(gateReport(r,'lifetime-only'),[]);
}));

test('changed geometry fails lifetime-only gate even when index is consistent',()=>pair((web,app)=>{
  const altered=initial();
  altered[0].geometryVersions[0].geometry.coordinates[0][1][0]=3;
  make(web,altered);make(app,initial());
  const r=compare(web,app);
  assert.equal(r.counts['geometry-or-version-drift'],1);
  assert.notDeepEqual(gateReport(r,'lifetime-only'),[]);
}));

test('unexpected non-geometric metadata alteration is reported',()=>pair((web,app)=>{
  const altered=initial();altered[0].names.ko='Changed label';
  make(web,altered);make(app,initial());
  const r=compare(web,app);
  assert.equal(r.counts['other-semantic-drift'],1);
  assert.deepEqual(r.indexUnexpectedChanges,['state:AAA']);
  assert.notDeepEqual(gateReport(r,'lifetime-only'),[]);
}));

test('tampered compressed bytes fail rather than being trusted from index',()=>pair((web,app)=>{
  make(web,initial());make(app,initial());
  const path=join(app,'state-AAA.json.gz');
  const bytes=readFileSync(path);bytes[5]^=1;writeFileSync(path,bytes);
  assert.throws(()=>compare(web,app),/SHA-256 mismatch/);
}));

test('stale decoded length in index fails validation',()=>pair((web,app)=>{
  make(web,initial());make(app,initial());
  const path=join(app,'index.json');
  const index=JSON.parse(readFileSync(path,'utf8'));
  index.entities[0].decodedBytes+=1;
  writeFileSync(path,JSON.stringify(index,null,2)+'\n');
  assert.throws(()=>compare(web,app),/Decoded length mismatch/);
}));

test('mismatched lineage or snapshot blocks both modes',()=>pair((web,app)=>{
  make(web,initial());make(app,initial());
  const path=join(web,'index.json');
  const index=JSON.parse(readFileSync(path,'utf8'));
  index.lineages[0].names.ko='Updated lineage';
  writeFileSync(path,JSON.stringify(index,null,2)+'\n');
  const r=compare(web,app);
  assert.equal(r.lineagesEqual,false);
  assert.notDeepEqual(gateReport(r,'exact'),[]);
  assert.notDeepEqual(gateReport(r,'lifetime-only'),[]);
}));
