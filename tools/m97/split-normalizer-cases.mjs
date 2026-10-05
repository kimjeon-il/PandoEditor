/** Node-only regression observations from the exact approved web functions.
 * These observations are not Chromium evidence or the differential gate. */
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import vm from 'node:vm';
import {root,asset,commit,verifyApproved,hash} from './split-normalizer-generate.mjs';
import {loadSplitCandidateModules} from './web-split-candidate.mjs';
const copy=value=>structuredClone(value);
const square=(x0,y0,x1,y1)=>({type:'Polygon',coordinates:[[[x0,y0],[x0,y1],[x1,y1],[x1,y0],[x0,y0]]]});
const source={type:'Polygon',coordinates:[[[179,-2],[179,2],[-179,2],[-179,-2],[179,-2]]]};
const parent={type:'Polygon',coordinates:[[[173,-3],[173,3],[-178,3],[-178,-3],[173,-3]]]};
const hole=[[179.5,0.5],[-179.5,0.5],[-179.5,1.5],[179.5,1.5],[179.5,0.5]];
export const view={kind:'flat',scale:500,translate:[512,384],rotate:[0,0,0],center:[180,0],size:{width:1024,height:768},coarsePointer:false,snapDistance:{mouse:10,touch:18}};
export function definitions(){
 return ['root','child'].flatMap(scope=>['plain','hole','multiple-crossings','island'].map(variant=>({
  id:`${scope}-dateline-${variant}`,scope,variant,
  source:variant==='hole'?{...copy(source),coordinates:[...copy(source.coordinates),copy(hole)]}
   :variant==='island'?{type:'MultiPolygon',coordinates:[copy(source.coordinates),square(174,0,175,1).coordinates]}:copy(source),
  ...(scope==='child'?{parent:copy(parent)}:{}),
  coords:variant==='multiple-crossings'?[[178,-1],[-178,-1],[-178,0],[178,0],[178,1],[-178,1]]:[[178,0],[-178,0]],
  view:copy(view),expectedCandidateCount:variant==='multiple-crossings'?4:2,
 })));
}
export async function observeCases(){
 const {source:helper,manifest}=verifyApproved();
 const loaded=await loadSplitCandidateModules({candidateRoot:resolve(root,'tests/fixtures/web-m973-split/corrections/dateline'),candidateChanges:manifest.changes});
 try{
  const {prepareCutInWorker}=await import(pathToFileURL(resolve(loaded.root,'assets/js/modules/cut-worker-preparation.js')));
  const context=vm.createContext({});
  for(const [path,pin]of [['assets/geometry/river/original/d3.min.js','4cdf92091ed0cfdd8b862af1c6d4744bd0458e746b92c1bbd5403a1143ecd538'],['assets/geometry/polygon-clipping-0.15.7.js','8c1ed56df8b1f97b047f82d91b910aacdaff67d8d9a55f2495eb26e8369186f7']]){
   const bytes=readFileSync(resolve(root,path));assert.equal(hash(bytes),pin);vm.runInContext(bytes.toString(),context,{filename:path});
  }
  vm.runInContext(helper.toString(),context,{filename:'approved/polygon-geometry.js'});
  const api=context.PandoLabPolygonGeometry,pc=context.polygonClipping;
  const portable=value=>JSON.parse(JSON.stringify(value));
  const polygons=value=>value.type==='Polygon'?[value.coordinates]:value.coordinates;
  const rows=[],cases=definitions();
  function add(id,operation,input,extra={}){
   const before=copy(input),value=operation==='wrap'?api.wrapPolygonGeometry(input,pc):api.normalizeClippedPolygonGeometry(input);
   assert.deepEqual(input,before,id+' mutates input');
   const expected=portable(value);rows.push({id,operation,input:copy(input),expected,...extra});return expected;
  }
  for(const definition of cases){
   const before=copy(definition),payload={source:copy(definition.source),sourceKey:definition.id,coords:copy(definition.coords),view:copy(definition.view),buildPreview:true};
   const cut=prepareCutInWorker(payload,api,context.d3,pc);
   assert.equal(cut.valid,true,definition.id);assert.equal(cut.split.candidates.length,definition.expectedCandidateCount,definition.id);
   const wrapped=add(definition.id+'-source','wrap',definition.source,{definition:definition.id});
   if(definition.parent)add(definition.id+'-parent','wrap',definition.parent,{definition:definition.id});
   for(const [index,candidate]of cut.split.candidates.entries()){
    const selected=add(definition.id+`-candidate-${index}`,'wrap',portable(candidate.geometry),{definition:definition.id});
    const raw={type:'MultiPolygon',coordinates:portable(pc.difference(polygons(wrapped),polygons(selected)))};
    add(definition.id+`-remainder-${index}`,'normalize-clipped',raw,{definition:definition.id});
   }
   assert.deepEqual(definition,before,definition.id+' definition mutation');
  }
  add('ordinary-identity','wrap',square(-10,-5,10,5));
  add('ordinary-multipolygon-identity','wrap',{type:'MultiPolygon',coordinates:[square(5,0,6,1).coordinates]});
  add('full-world-seam-identity','wrap',square(-180,-90,180,90));
  add('full-world-periodic-hole','wrap',{type:'Polygon',coordinates:[square(-180,-90,180,90).coordinates[0],[[179,-2],[-179,-2],[-179,2],[179,2],[179,-2]]]});
  add('crossing-with-untouched-tiny-island','wrap',{type:'MultiPolygon',coordinates:[source.coordinates,square(20,0,20+1e-9,1e-9).coordinates]});
  const long={type:'Polygon',coordinates:[[[170,0],[170,10],[-40,10],[110,10],[110,0],[-40,0],[170,0]]]};
  const first=add('long-edge-unwrapped-source','wrap',long);add('long-edge-idempotent','wrap',first);
  add('planar-wide-edge','normalize-clipped',square(-170,0,170,10));
  add('planar-wide-edge-with-hole','normalize-clipped',{type:'Polygon',coordinates:[square(-175,-10,175,10).coordinates[0],square(-165,-5,165,5).coordinates[0].slice().reverse()]});
  add('empty-wrapped','wrap',{type:'MultiPolygon',coordinates:[]});
  add('empty-clipped','normalize-clipped',{type:'MultiPolygon',coordinates:[]});
  return {schema:'pando-approved-split-normalizer-observations',version:1,behavioralCommit:commit,
   mode:'Node regression observations from full approved web sources; not browser or pixel parity evidence.',
   definitionSource:'tools/m97/split-normalizer-cases.mjs',
   upstreamCaseSource:'Pando 07d3e20 tests/unit/map-edit-preparation-worker.test.mjs and polygon-geometry.test.mjs',
   definitions:cases,cases:rows};
 }finally{loaded.cleanup();}
}
if(process.argv[1]&&import.meta.url===pathToFileURL(resolve(process.argv[1])).href){
 const bytes=Buffer.from(JSON.stringify(await observeCases(),null,2)+'\n'),file=resolve(asset,'normalizer-cases.json');
 if(process.argv.includes('--check'))assert.deepEqual(readFileSync(file),bytes,'Normalizer observation drift');else writeFileSync(file,bytes);
 console.log('Verified approved-source Node normalizer regression observations (not browser evidence).');
}
