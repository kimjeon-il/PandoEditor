import assert from 'node:assert/strict';
import test from 'node:test';
import {createTerritorialLibraryService} from '../../assets/js/modules/territorial-library-service.js';
const geometry={type:'Polygon',coordinates:[[[0,0],[0,1],[1,1],[0,0]]]};
const entity=(entityId,parentEntityId='',validTo=null)=>({schemaVersion:2,entityId,lineageId:'sample',entityKind:'general',names:{ko:entityId},alternateNames:[],parentEntityId,lifetime:{validFrom:null,validTo},geometryVersions:[{versionId:`${entityId}:v1`,validFrom:null,validTo,geometry,certainty:'high',datePrecision:'current',sourceId:'fixture'}],metadata:{},sourceInfo:{},instantiation:{mode:'independent',countryUpdates:{}}});
function fixture(){
 const entities=[entity('state:parent'),entity('state:child','state:parent'),entity('state:expired','state:parent','1991-12-25')];
 let loads=0; const cache=new Map();
 const loader={loadIndex:async()=>({schemaVersion:2,lineages:[{lineageId:'sample',names:{ko:'계보'},entityRefs:entities.map(e=>e.entityId),relations:[]}],entities:entities.map(e=>({...e,geometryVersions:e.geometryVersions.map(({geometry,...v})=>v)})),snapshots:[]}),loadEntity:async id=>{loads++;const e=entities.find(e=>e.entityId===id);cache.set(id,e);return e;},peek:id=>cache.get(id)||null};
 return {service:createTerritorialLibraryService({loader,today:()=> '2026-10-06'}),loads:()=>loads};
}
test('catalog loading shares requests and searching loads no geometry',async()=>{
 const f=fixture(); const [a,b]=await Promise.all([f.service.load(),f.service.load()]);assert.equal(a,b);
 assert.deepEqual(f.service.search({query:'parent',referenceDate:'2026'}).flatMap(g=>g.entities.map(e=>e.entityId)),['state:parent']);assert.equal(f.loads(),0);
 assert.equal(f.service.search({query:'계보',referenceDate:'2026'})[0].entities.length,2);
 assert.equal(f.service.search({referenceDate:'1990'})[0].entities.length,3);
 assert.throws(()=>f.service.search({referenceDate:'0000'}));
});
test('descriptors lazily load requested alive descendants without runtime current/historical synthesis',async()=>{
 const f=fixture();await f.service.load();const items=await f.service.instantiateDescriptors(['state:parent'],'1991','all');
 assert.deepEqual(items.map(i=>i.entityId),['state:parent','state:child']);assert.equal(items[1].parentEntityId,'state:parent');assert.deepEqual(items[1].geometry,geometry);assert.equal(f.loads(),2);
});
test('failed catalog loading can retry',async()=>{
 let n=0;const service=createTerritorialLibraryService({loader:{loadIndex:async()=>{if(!n++)throw new Error('offline');return {schemaVersion:2,lineages:[],entities:[],snapshots:[]};},loadEntity:async()=>null,peek:()=>null}});
 await assert.rejects(service.load(),/offline/);await service.load();assert.equal(n,2);
});

test('missing reference date and geometry gaps never instantiate a nearest or overridden version',async()=>{
 const f=fixture();await f.service.load();
 await assert.rejects(f.service.instantiateDescriptors(['state:parent'],''),/date|required/i);
 await assert.rejects(f.service.instantiateDescriptors(['state:expired'],'2026'),/경계/);
 const result=await f.service.instantiateDescriptors(['state:expired'],'1990');
 assert.equal(result[0].validFrom,null);assert.equal(result[0].validTo,null);
 assert.deepEqual(result[0].metadata.sourceLifetime,{validFrom:null,validTo:'1991-12-25'});
 assert.equal(result[0].metadata.sourceReferenceDate,'1990');
});
