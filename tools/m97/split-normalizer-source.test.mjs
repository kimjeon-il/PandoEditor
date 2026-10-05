import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,mkdtempSync,cpSync,appendFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {resolve} from 'node:path';
import {asset,generate,verifyApproved,sourceHash,manifestHash,commit,hash} from './split-normalizer-generate.mjs';
import {observeCases,definitions} from './split-normalizer-cases.mjs';
test('approved normalizer is complete, unchanged, licensed and reproducible',()=>{
 const {source,manifest}=verifyApproved();assert.equal(hash(source),sourceHash);assert.equal(manifest.behavioralCommit,commit);
 assert.equal(hash(readFileSync(resolve(asset,'approved-manifest.json'))),manifestHash);
 const first=generate();assert.deepEqual(first,generate());for(const [file,bytes]of first)assert.deepEqual(readFileSync(file),bytes,file);
 const provenance=JSON.parse(readFileSync(resolve(asset,'provenance.json')));assert.match(provenance.transformation,/None/);assert.match(provenance.license.polygonClipping,/MIT/);
});
test('source and approval manifest drift fail closed',()=>{
 for(const file of ['original/polygon-geometry.js','approved-manifest.json']){
  const folder=mkdtempSync(resolve(tmpdir(),'split-normalizer-'));
  try{cpSync(asset,folder,{recursive:true});appendFileSync(resolve(folder,file),'\n');assert.throws(()=>generate(folder),/SPLIT_NORMALIZER_(SOURCE|MANIFEST)_HASH_MISMATCH/);}
  finally{rmSync(folder,{recursive:true,force:true});}
 }
});
test('recorded Node cases match the exact approved production cut and normalization helpers',async()=>{
 const expected=JSON.parse(readFileSync(resolve(asset,'normalizer-cases.json'))),actual=await observeCases();
 assert.deepEqual(actual,expected);assert.equal(expected.definitions.length,8);assert.equal(expected.cases.length,63);
 assert.deepEqual(expected.definitions,definitions());assert.match(expected.mode,/not browser/);
 for(const scope of ['root','child'])for(const variant of ['plain','hole','multiple-crossings','island']){
  const id=`${scope}-dateline-${variant}`,definition=expected.definitions.find(row=>row.id===id);
  assert.ok(definition);assert.equal(expected.cases.filter(row=>row.definition===id&&row.id.includes('-candidate-')).length,definition.expectedCandidateCount);
 }
});
test('regression expectations independently retain world holes, tiny islands and planar edge subdivisions',()=>{
 const {cases}=JSON.parse(readFileSync(resolve(asset,'normalizer-cases.json')));const byId=id=>cases.find(row=>row.id===id);
 for(const id of ['ordinary-identity','ordinary-multipolygon-identity','full-world-seam-identity','long-edge-idempotent'])assert.deepEqual(byId(id).input,byId(id).expected,id);
 const tiny=byId('crossing-with-untouched-tiny-island');assert.deepEqual(tiny.expected.coordinates[2],tiny.input.coordinates[1]);
 const wide=byId('planar-wide-edge');assert.deepEqual(wide.expected.coordinates[0],[[-170,0],[-170,10],[0,10],[170,10],[170,0],[0,0],[-170,0]]);
 const world=byId('full-world-periodic-hole').expected;const serialized=JSON.stringify(world);
 assert.ok(serialized.includes('[179,-2]'));assert.ok(serialized.includes('[-179,-2]'));
 for(const id of ['planar-wide-edge','planar-wide-edge-with-hole','full-world-periodic-hole']){
  const value=byId(id).expected,polygons=value.type==='Polygon'?[value.coordinates]:value.coordinates;
  for(const polygon of polygons)for(const ring of polygon)for(let i=1;i<ring.length;++i)assert.ok(Math.abs(ring[i][0]-ring[i-1][0])<=180,id);
 }
});
