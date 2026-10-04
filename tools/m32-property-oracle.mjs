// Executes pinned web modules and compares them with the compiled C++/QML probe.
import fs from 'node:fs';
import crypto from 'node:crypto';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import * as math from '../tests/fixtures/web-properties/source/custom-color-control.js';
import {normalizeTemporalInterval} from '../tests/fixtures/timeline-temporal-reference.mjs';
import {createObjectCommands} from '../tests/fixtures/web-properties/source/app-object-commands.js';
import {createObjectMetadata} from '../tests/fixtures/web-properties/source/app-object-metadata.js';
import {createHistoryService} from '../tests/fixtures/web-properties/source/history-service.js';
import {createProjectCommandPipeline} from '../tests/fixtures/web-properties/source/project-command-pipeline.js';
import {createTerritorialApplicationService} from '../tests/fixtures/web-properties/source/territorial-service.js';
import * as territory from '../tests/fixtures/web-properties/source/territorial-units.js';
import {createTerritorialScopeResolver,validateSubunitParentChanges} from '../tests/fixtures/web-properties/source/territorial-scope.js';
import {countryDisplayName} from '../tests/fixtures/web-properties/source/country-display.js';
import {pruneCountryOverrides} from '../tests/fixtures/web-properties/source/country-feature.js';
import * as colors from '../tests/fixtures/web-properties/source/color-adapter.js';
const root=new URL('../',import.meta.url), source=new URL('tests/fixtures/web-properties/source/',root);
const manifest=JSON.parse(fs.readFileSync(new URL('tests/fixtures/web-properties/manifest.json',root)));
for(const [name,sha] of Object.entries(manifest.blobs)){
 const bytes=fs.readFileSync(new URL(name,source));assert.equal(crypto.createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`),bytes])).digest('hex'),sha,`pinned source modified: ${name}`);
}
const temporalBytes=fs.readFileSync(new URL('tests/fixtures/timeline-temporal-reference.mjs',root));
assert.equal(crypto.createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${temporalBytes.length}\0`),temporalBytes])).digest('hex'),'34ef7a8bc1cba4cf9c550dc93be8882f5c9b6aa7','T0 month contract source modified');
function originalScenario(ops){
 const geometry={type:'Polygon',coordinates:[[[0,0],[8,0],[8,8],[0,8],[0,0]]]};
 // General A and General child S share the same metadata contract. The pinned
 // metadata handlers still provide the independent reference for that contract;
 // R is a standalone Regional. No source blobs or math/date cases are changed.
 const state={countriesData:{features:['A','S'].map(id=>({type:'Feature',id,geometry,properties:{name:id==='A'?'Alpha':'S'}}))},countryOverrides:{},territorialUnits:territory.normalizeTerritorialUnits([{type:'Feature',id:'R',geometry,properties:{schemaVersion:2,unitType:'region',name:'R',notes:'',parentId:'',sovereignId:'',coverageMode:'explicit',style:{},locked:false}}],{countryExists:id=>id==='A'||id==='S'}),stateRevision:0};
 let refs=[],error=false;
 const countryById=id=>state.countriesData.features.find(x=>x.id===id);
 const unitById=id=>state.territorialUnits.find(x=>x.id===id);
 const snapshot=()=>structuredClone({countryOverrides:state.countryOverrides,territorialUnits:state.territorialUnits});
 const restore=s=>{state.countryOverrides=pruneCountryOverrides(structuredClone(s.countryOverrides));state.territorialUnits=structuredClone(s.territorialUnits);++state.stateRevision};
 const store={history:[],future:[],historyMeta:[],futureMeta:[]};
 const history=createHistoryService({store,maxEntries:1000,snapshot,restore,normalizeMetadata:x=>x});
 const projectDomain={recordHistory:history.record,queueAutosave(){},queuePresentationAutosave(){}};
 const repository=territory.createTerritorialRepository({getCountries:()=>state.countriesData,getUnits:()=>state.territorialUnits,getCountryOverride:id=>state.countryOverrides[id]||{}});
 const scope=createTerritorialScopeResolver({read:()=>({revision:state.stateRevision,units:state.territorialUnits}),countryById,countryColor:f=>colors.readDomainColor('country',{feature:f,override:state.countryOverrides[f.id]},{fallback:'#cccccc'}).value,clipper:()=>null});
 const colorPorts={...colors,normalizeEditorColor:colors.normalizeColorValue,defaultCountryColor:()=> '#cccccc',DEFAULT_GENERIC_FEATURE_COLOR:'#8c68d8'};
 const locked=id=>id!=='R'?state.countryOverrides[id]?.locked===true:unitById(id)?.properties.locked===true;
 const setLocked=(id,value)=>{const o={...(state.countryOverrides[id]||{})};if(value)o.locked=true;else delete o.locked;if(Object.keys(o).length)state.countryOverrides[id]=o;else delete state.countryOverrides[id]};
 const pipeline=createProjectCommandPipeline({captureSnapshot:snapshot,restoreSnapshot:restore,recordHistory:(m,s)=>history.commitSnapshot(s,m),discardHistory:history.discardLast,advanceRevision:()=>++state.stateRevision});
 const service=createTerritorialApplicationService({repository,commandPipeline:pipeline,countryCommands:{isLocked:locked,setLocked,hasField:(id,f)=>Object.hasOwn(state.countryOverrides[id]||{},f),setField:(id,f,v)=>{state.countryOverrides[id]||={};if(f==='color')colors.writeDomainColor('country',{override:state.countryOverrides[id]},v);else state.countryOverrides[id][f]=v;}},unitCommands:{setField:(id,f,v)=>{const u=unitById(id);if(f==='color')colors.writeDomainColor('territorial',{feature:u},v,{clear:!v});else u.properties[f]=v},replaceAll:units=>{state.territorialUnits=units}}});
 const commands=createObjectCommands();
 const ports={projectState:{state},territorialModel:territory,selectionServices:{normalizeObjectRef:v=>v},objectPresentation:{territorialUnitById:unitById,territorialUnitName:f=>f.properties.name},countries:{countryFeatureById:countryById},colorModel:colorPorts,
   geometryOperations:{boundaryEditSelectionAnalysis:()=>({valid:false})}, // UI availability only; no geometry operation in these metadata scenarios.
   domains:{projectDomain,selectionDomain:{snapshot:()=>({selection:{items:refs}}),primary:()=>refs.at(-1)},selectionUiController:{presentPrimary(){}},layerTreeController:{syncLocks(){}},renderingDomain:{invalidateCountryPatch(){},invalidateBaseScene(){},invalidateSelection(){}}},
   layerPresentation:{isLayerItemVisible:()=>true},layers:{markLayerTreeDirty(){}},rendering:{gpuMapRenderer:{invalidateCountryPalette(){}}},platform:{$:()=>null,deepClone:structuredClone},domainControllers:{},presentation:{countryName:f=>countryDisplayName(f,state.countryOverrides[f?.id])},objectModelA:{},territorialServicesB:{},distributionPresentation:{}};
 commands.connect(ports);
 const metadata=createObjectMetadata();metadata.connect({...ports,objectModelB:{territorialApplicationService:service,setTerritorialStyleColor:(f,c)=>colors.writeDomainColor('territorial',{feature:f},c,{clear:!c})},
  propertyEditingA:{applyCountrySelectionIntent(){},applyTerritorialUnitSelectionIntent(){}},feedback:{setActionStatus:(_m,tone)=>{if(tone==='error')error=true}},readiness:{compactNotificationMessage:v=>v},territorialServicesB:{validateSubunitParentChanges},applicationServicesA:territory});
 const result=[];
 for(const op of ops){error=false;let ok=true;refs=(op.ids||[]).map(id=>({domain:'territorial',type:id!=='R'?'country':'region',id,key:`territorial:${id}`}));state.selected=refs[0];
  if(op.op==='undo')ok=history.undo();else if(op.op==='redo')ok=history.redo();
  else if(op.op==='lock'){
   if(refs.length>1){const all=refs.every(r=>locked(r.id));if(all!==op.value||refs.some(r=>locked(r.id)!==op.value))commands.batchToggleLocked();}
   else service.setLocked(refs[0].type,refs[0].id,op.value);
  }else if(op.op==='field'){
   if(locked(refs[0].id)){ok=false;}
   else if((op.field==='validFrom'||op.field==='validTo')&&op.value.trim())ok=false;
   else if(refs[0].id!=='R')metadata.commitCountryEdit(op.field,op.field==='name'?op.value.trim():op.value);
   else metadata.commitTerritorialUnitMeta(op.field,op.field==='notes'?op.value:op.value.trim());
  }else if(refs.length>1)commands.batchSetColor(op.value);
  else if(locked(refs[0].id))ok=false;
  else if(op.value===null){
   if(refs[0].id!=='R'){
    const id=refs[0].id;if(state.countryOverrides[id]?.color){history.record();delete state.countryOverrides[id].color;if(!Object.keys(state.countryOverrides[id]).length)delete state.countryOverrides[id];}
   }else metadata.commitTerritorialUnitMeta('color','');
  }else if(refs[0].id!=='R')metadata.commitCountryEdit('color',op.value);
  else metadata.commitTerritorialUnitMeta('color',op.value);
  const rows=['A','S','R'].map(id=>{
   const isGeneral=id!=='R',u=isGeneral?countryById(id):unitById(id),p=u.properties,o=isGeneral?(state.countryOverrides[id]||{}):p;
   const parentColor=id==='S'?colors.readDomainColor('country',{feature:countryById('A'),override:state.countryOverrides.A||{}},{fallback:'#cccccc'}).value:'';
   const c=colors.readDomainColor(isGeneral?'country':'territorial',isGeneral?{feature:u,override:o}:{feature:u},{fallback:id==='S'?parentColor:isGeneral?'#cccccc':'#8c68d8',inherited:''});
   return {id,name:isGeneral?countryDisplayName(u,o):p.name||'이름 없는 권역',rawName:o.name||(id==='S'?p.name:''),notes:o.notes||'',hasName:isGeneral?Object.hasOwn(o,'name')||id==='S':true,color:c.value,explicit:!c.isDefault,locked:locked(id),from:p.validFrom||null,to:p.validTo||null};
  });result.push({units:rows,undo:history.canUndo(),redo:history.canRedo(),ok:ok&&!error});
 }return result;
}
const cases=[],expected=[];let rng=123456;const random=()=>{rng=(1664525*rng+1013904223)>>>0;return rng/2**32};
function add(test,value){cases.push(test);expected.push(value)}
for(const value of ['abc','#AbC','AABBCC','#badbad','#abcd','bad-input','',' #123456 ','＃abcdef'])add({kind:'math',fn:'parseColorHex',args:[value]},math.parseColorHex(value));
for(let i=0;i<400;++i){const rgb=[0,0,0].map(()=>Math.floor(random()*256)),h=360*random();
 for(const [fn,args] of [['rgbToHex',[rgb]],['rgbToHsv',[rgb,h]],['rgbToHsl',[rgb]],['hslToRgb',[[Math.floor(h),Math.floor(random()*101),Math.floor(random()*101)]]],['hsvToRgb',[[h,random(),random()]]]])add({kind:'math',fn,args},math[fn](...args));
}
for(const value of ['',' x ','\n\tHello\nworld \r','\ufeffx\u3000','\u0085x\u200b','  한글  ',...Array.from({length:20},(_,i)=>String.fromCharCode(0x2000+i)+'x'+String.fromCharCode(0x2000+i))])add({kind:'trim',value},value.trim());
const dates=['','0000','0001','-0001','1900','1900-02-29','2000-02-29','2000-02-30','2026-09-19','+010000','10000','-999999-12-31','+0000','-0004-02-29','2026-09',' 2000 ','2000-13-01'];
for(const from of dates)for(const to of dates){try{const v=normalizeTemporalInterval(from,to);add({kind:'temporal',from,to},{ok:true,from:v.validFrom,to:v.validTo})}catch{add({kind:'temporal',from,to},{ok:false})}}
const scenarios=[
 [{op:'field',ids:['A'],field:'name',value:''},{op:'field',ids:['A'],field:'name',value:''},{op:'undo'},{op:'undo'},{op:'redo'},{op:'redo'}],
 [{op:'color',ids:['A'],value:'#336699'},{op:'color',ids:['S'],value:'#336699'},{op:'color',ids:['A'],value:'#ff0000'},{op:'color',ids:['S'],value:null},{op:'undo'},{op:'redo'}],
 [{op:'lock',ids:['S'],value:true},{op:'color',ids:['A','S'],value:'#112233'},{op:'color',ids:['A','S'],value:'#112233'},{op:'undo'},{op:'redo'},{op:'lock',ids:['A','S'],value:true},{op:'lock',ids:['A','S'],value:false}],
 [{op:'field',ids:['A'],field:'name',value:'  New name  '},{op:'field',ids:['A'],field:'name',value:''},{op:'undo'},{op:'redo'}],
 [{op:'field',ids:['S'],field:'notes',value:' \ufeffline 1\nline2\u3000'},{op:'field',ids:['R'],field:'validFrom',value:'-0001'},{op:'field',ids:['R'],field:'validTo',value:'0000'},{op:'undo'},{op:'redo'}],
 [{op:'field',ids:['A'],field:'notes',value:'  raw\nnotes  '},{op:'field',ids:['A'],field:'name',value:'Next'},{op:'undo'},{op:'redo'}]
];
for(const ops of scenarios)add({kind:'scenario',ops},originalScenario(ops));
const child=spawnSync(process.argv[2],[],{input:JSON.stringify(cases),encoding:'utf8',maxBuffer:32*1024*1024});if(child.status!==0)throw new Error(child.stderr||`probe exit ${child.status}`);const actual=JSON.parse(child.stdout);
if(process.argv[3])fs.writeFileSync(process.argv[3],JSON.stringify({cases,expected,actual}));
assert.equal(actual.length,cases.length);const failures=[];
for(let i=0;i<cases.length;++i){try{assert.deepEqual(actual[i],expected[i]);}catch(e){failures.push(i);console.error('MISMATCH',i,JSON.stringify(cases[i]),e.message.slice(0,450));}}
assert.equal(failures.length,0,`Source/native differences: ${failures.join(',')}`);
console.log(`Pinned source ${manifest.commit}: ${Object.keys(manifest.blobs).length} blob hashes verified; ${cases.length} comparisons passed (unchanged math/temporal, General metadata/batch/history, static date guard).`);
