import assert from 'node:assert/strict';
import fs from 'node:fs';
import crypto from 'node:crypto';
import vm from 'node:vm';
import {spawnSync} from 'node:child_process';
import * as territorial from '../tests/fixtures/web-structure/source/territorial-units.js';

const sources=new URL('../tests/fixtures/web-m4/source/',import.meta.url);
const previous=new URL('../tests/fixtures/web-structure/source/',import.meta.url);
function pinned(base,name,hash) {
  const bytes=fs.readFileSync(new URL(name,base));
  assert.equal(crypto.createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`),bytes])).digest('hex'),hash,name);
  return bytes.toString();
}
const planSource=pinned(sources,'territorial-edit-plan.js','46d42d7147cf5bb22a147b6f0ac2e3614e2f0ffe');
const countrySource=pinned(sources,'country-geometry.js','e49360a9d3c7dd3be5596c74d7de0b88ef3f9bdb');
const workerSource=pinned(sources,'map-edit-worker.js','300012a00902ee35fa4ddd606296a40b1042c4f3');
const clipperSource=pinned(previous,'polygon-clipping.min.js','66a4e7bb45fb37353ba86e1114f7e9166ef1efad');
const conversionSource=pinned(previous,'app-territorial-conversion.js','23c45d8fa0c8ffca024639ceeae8b27511f48395');
const box=(x,y,n)=>({type:'MultiPolygon',coordinates:[[[[x,y],[x+n,y],[x+n,y+n],[x,y+n],[x,y]]]]});
const rectangle=(x,y,w,h)=>({type:'MultiPolygon',coordinates:[[[[x,y],[x+w,y],[x+w,y+h],[x,y+h],[x,y]]]]});
function fixture(operation) {
  const feature=(id,geometry,parentId='',sovereignId='')=>({type:'Feature',id,geometry,properties:{name:id,...(parentId?{schemaVersion:2,unitType:'subunit',parentId,sovereignId,coverageMode:'partition'}:{})}});
  return {case:operation,operation,targetId:operation==='convert'?'A':'S',countryId:'B',
    countries:[feature('A',box(0,0,10)),feature('B',box(20,0,10))],
    units:[feature('P',box(1,1,8),'A','A'),feature('S',box(2,2,2),'P','A'),feature('C',box(2,2,.25),'S','A')]};
}
const rows=['transfer','promote','convert'].map(fixture);
const hole=fixture('transfer');hole.case='hole';hole.units[1].geometry.coordinates[0].push(box(2.5,2.5,.5).coordinates[0][0]);rows.push(hole);
const multi=fixture('promote');multi.case='multipolygon';multi.units[1].geometry.coordinates.push(box(5,5,1).coordinates[0]);rows.push(multi);
const remove=fixture('transfer');remove.case='empty former parent';remove.units[0].geometry=structuredClone(remove.units[1].geometry);rows.push(remove);
const full=fixture('transfer');full.case='complete subtraction rejected';full.units[0].geometry=box(0,0,10);full.units[1].geometry=box(0,0,10);rows.push(full);
const touch=fixture('convert');touch.case='touching countries';touch.countries[1].geometry=box(10,0,10);rows.push(touch);
const merge=fixture('merge');merge.case='merge siblings';merge.parentId='P';merge.sourceIds=['X'];merge.units.push({type:'Feature',id:'X',geometry:box(4,2,2),properties:{name:'X',schemaVersion:2,unitType:'subunit',parentId:'P',sovereignId:'A',coverageMode:'partition'}});rows.push(merge);
const annex=fixture('annex');annex.case='annex drawn area';annex.parentId='P';annex.sourceId='X';annex.draft=rectangle(4,2,1,2);annex.units.push({type:'Feature',id:'X',geometry:box(4,2,2),properties:{name:'X',schemaVersion:2,unitType:'subunit',parentId:'P',sovereignId:'A',coverageMode:'partition'}});rows.push(annex);
const boundary=fixture('country-boundary');boundary.case='shared country boundary';boundary.targetId='A';boundary.countries[1].geometry=box(10,0,10);boundary.featurePatches=[structuredClone(boundary.countries[0]),structuredClone(boundary.countries[1])];boundary.featurePatches[0].geometry=rectangle(0,0,12,10);boundary.featurePatches[1].geometry=rectangle(12,0,8,10);rows.push(boundary);
const coast=fixture('coast');coast.case='country coast clip';coast.targetId='A';coast.draft=box(0,0,7);coast.coastBaseline=structuredClone(coast.countries[0].geometry);rows.push(coast);

function context() {
  const c=vm.createContext({structuredClone,URL,console,setTimeout,performance});c.self=c;c.location={href:'https://fixture.invalid/workers/map-edit-worker.js'};
  c.importScripts=()=>{};vm.runInContext(clipperSource,c);vm.runInContext(countrySource,c);vm.runInContext(planSource,c);vm.runInContext(workerSource,c);
  return c;
}
async function web(row) {
  const c=context(),data=structuredClone(row);const kernel=c.PandoLabTerritorialEdit.createKernel(c.polygonClipping);
  if(row.operation!=='convert') {
    const request={...data,newCountry:{...structuredClone(data.units.find(u=>u.id===row.targetId)),properties:{name:row.targetId}}};
    const patch=kernel.plan(request);const byId=new Map([...data.countries,...data.units].map(f=>[f.id,f]));
    for(const id of patch.removedIds)byId.delete(id);for(const f of patch.features)byId.set(f.id,f);
    return {features:[...byId.values()],countries:new Set(patch.countryIds),clipper:c.polygonClipping};
  }
  // Expose the private entry point only; its body and executeMerge are original.
  const source=conversionSource.replace('export function createTerritorialConversion','function createTerritorialConversion').replace('    connect,','    convertCountryToRegionType,\n    connect,');
  vm.runInContext(source,c);const module=vm.runInContext('createTerritorialConversion()',c);
  const state={territorialUnits:data.units,territorialRelations:[],countryOverrides:{},distributionEntries:[],genericFeatures:[]};
  const countryById=id=>data.countries.find(f=>f.id===id);
  module.connect({
    countries:{countryFeatureById:countryById},projectState:{state},
    objectOperationsB:{requireCountriesUnlocked:ids=>ids.every(id=>!countryById(id)?.properties.locked)},
    territorialModel:territorial,
    presentation:{countryName:f=>f.properties.name,territorialRepository:{get:id=>countryById(id)||state.territorialUnits.find(f=>f.id===id)}},
    objectMetadata:{territorialUnitInsideContainer:(a,b)=>kernel.contains(b.geometry,a.geometry)},
    platform:{deepClone:structuredClone},surfaces:{uid:()=> 'new-A'},territorialServicesA:{createTerritorialFeature:territorial.createTerritorialFeature},
    snapshots:{snapshotEditable:()=>structuredClone(data)},feedback:{setActionStatus(){},reportOperationError(e){throw e;}},
    geometryOperations:{async transactCountryEdit({payload,applyResult}){
      c.mergePayload=payload;c.working=new Map(data.countries.map(f=>[f.id,f]));
      const patch=vm.runInContext('executeMerge(mergePayload, working)',c);applyResult(patch);return {ok:true};
    }},
    cutOperations:{applyWorkerCountryPatches(patch){data.countries=data.countries.filter(f=>!patch.removedIds.includes(f.id));for(const f of patch.features){const i=data.countries.findIndex(x=>x.id===f.id);if(i>=0)data.countries[i]=f;else data.countries.push(f);}}},
    // Completeness synthesis is outside these fixtures; relation validation is
    // performed with the original kernel below, not replaced with expected data.
    landRelations:{reconcileTerritorialUnitCompleteness(){}},
    objectModelB:{validateTerritorialUnitRelations(units){kernel.validate(data.countries,units);return {ok:true};}},
    countryValidation:{refreshCountryCentroids(){}},layers:{markLayerTreeDirty(){}},
    propertyEditingA:{applyTerritorialUnitSelectionIntent(){}},domains:{},applicationConstantsB:{TERRITORIAL_TYPE_LABELS:{subunit:'subunit'}}
  });
  assert.equal(await module.convertCountryToRegionType('A','subunit','B','B'),true);
  return {features:[...data.countries,...state.territorialUnits],countries:new Set(data.countries.map(f=>f.id)),clipper:c.polygonClipping};
}
const probe=process.argv[2];assert.ok(probe,'usage: node tools/m4-geometry-oracle.mjs <m4_geometry_probe.exe>');
const child=spawnSync(probe,[],{input:JSON.stringify(rows),encoding:'utf8',maxBuffer:16*1024*1024});assert.equal(child.status,0,child.stderr);
const native=JSON.parse(child.stdout);
for(let i=0;i<rows.length;i++) {
  let expected,error;try{expected=await web(rows[i]);}catch(e){error=e;}
  assert.equal(native[i].ok,!error,`${rows[i].case}: ${native[i].error||error?.message}`);
  if(error)continue;
  const actual=new Map(native[i].features.map(f=>[f.id,f]));
  assert.deepEqual([...actual.keys()].sort(),expected.features.map(f=>f.id).sort(),rows[i].case);
  for(const f of expected.features) {
    const a=actual.get(f.id),isCountry=expected.countries.has(f.id);
    assert.equal(a.properties.unitType,isCountry?'country':'subunit',f.id);
    assert.equal(a.properties.parentId,isCountry?'':f.properties.parentId,f.id);
    assert.equal(a.properties.sovereignId,isCountry?'':f.properties.sovereignId,f.id);
    const multi=g=>g.type==='Polygon'?[g.coordinates]:g.coordinates;
    assert.equal(JSON.stringify(expected.clipper.xor(multi(a.geometry),multi(f.geometry))),'[]',`${rows[i].case}: geometry ${f.id}`);
  }
}
console.log(`M4 web 17c3dbe: ${rows.length} native transaction comparisons passed (transfer, conversion, merge, annex, shared boundary and coast).`);
