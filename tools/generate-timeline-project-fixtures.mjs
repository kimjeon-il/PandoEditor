import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL,fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
const webRoot=process.argv[2];if(!webRoot)throw Error('usage: node --experimental-vm-modules tools/generate-timeline-project-fixtures.mjs <web-checkout>');
const root=fileURLToPath(new URL('../',import.meta.url));
const load=relative=>import(pathToFileURL(path.join(webRoot,relative)));
const {createProjectSerializer}=await load('assets/js/modules/project-serializer.js');
const {projectForStorage}=await load('tests/helpers/timeline-project.mjs');
const {timelineStorageCases}=await load('tests/fixtures/timeline-storage-cases.mjs');
const {productionGeoPackage}=await load('tests/helpers/production-geopackage.mjs');
const base=createProjectSerializer({appVersion:'0.34.0',baseDataset:'base',distributionModes:['territorial','geometry'],
 terrainDataset:'terrain',hydroDataset:'hydro',readSnapshot:()=>projectForStorage(),now:()=>new Date('2026-10-04T00:00:00Z')}).buildProject();
const cases=timelineStorageCases().map(row=>{
 const snapshot=projectForStorage(row),project={...structuredClone(base),...snapshot.projectFields,territorialEntities:snapshot.territorialEntities};
 if(row.name==='unknown snapshot schema')project.schemaVersion=row.input.schemaVersion;
 if(row.name==='unknown snapshot field')project.cursor=row.input.cursor;
 if(row.name==='missing geometries collection')delete project.geometries;
 return {name:row.name,expected:row.expected,project};
});
fs.writeFileSync(path.join(root,'tests/fixtures/timeline-project-v9.json'),JSON.stringify(cases,null,2)+'\n');
const destination=path.join(root,'tests/fixtures/timeline-exchange');fs.mkdirSync(destination,{recursive:true});
for(const kind of ['static','complex'])for(const extension of ['json','gpkg','expected.json'])
 fs.copyFileSync(path.join(webRoot,'tests/fixtures/timeline-exchange',kind+'.'+extension),path.join(destination,kind+'.'+extension));
const content=JSON.parse(fs.readFileSync(path.join(destination,'static.json'),'utf8'));
const source={schemaVersion:1,kind:'user',dataset:'exchange',version:'9',sourceId:'original',sourceFormat:'geojson',sourceType:'Point',importedAt:'2026-10-04',details:{provider:'fixture'}};
const uuid=i=>'22000000-0000-4000-8000-'+String(i).padStart(12,'0');
content.sourceInfo={provider:'web-worker',schemaVersion:1};
content.territorialEntities[0].properties.metadata.flagDataUrl='data:image/svg+xml;base64,'+Buffer.from('<svg xmlns="http://www.w3.org/2000/svg" width="2" height="2"><rect width="2" height="2" fill="red"/></svg>').toString('base64');
content.labels=[{id:uuid(1),name:'서울',kind:'city',notes:'original',coordinates:[127,37],territorialUnitId:'A',source}];
content.genericFeatures=[{type:'Feature',id:uuid(2),geometry:{type:'Point',coordinates:[128,38]},properties:{schemaVersion:2,name:'원본 Point',notes:'source metadata',color:'#123456',locked:false,source}}];
content.distributionLayers=[{id:uuid(3),schemaVersion:3,name:'Population',unit:'people',valueScale:{mode:'manual',min:-100,max:3000},color:'#123456',locked:false,parentId:'',groups:[],validFrom:null,validTo:null,metadata:{source:'original'}}];
content.distributionEntries=[{id:uuid(4),schemaVersion:3,layerId:uuid(3),mode:'territorial',territorialUnitId:'A',geometry:null,value:1000,certainty:'high',validFrom:null,validTo:null,metadata:{source:'original'}}];
content.geometries.push({id:'web-label:'+uuid(1),version:1,geojson:{type:'Point',coordinates:[127,37]}},{id:'web-genericFeatures:'+uuid(2),version:1,geojson:content.genericFeatures[0].geometry});
fs.writeFileSync(path.join(destination,'content.json'),JSON.stringify(content,null,2)+'\n');
const file=await productionGeoPackage('write',new ArrayBuffer(0),content);
fs.writeFileSync(path.join(destination,'content.gpkg'),new Uint8Array(file.buffer));
const sha=spawnSync('git',['rev-parse','HEAD'],{cwd:webRoot,encoding:'utf8'}).stdout.trim();
fs.writeFileSync(path.join(destination,'provenance.json'),JSON.stringify({webCommit:sha,projectSchemaVersion:9,timelineSchemaVersion:1,
 generator:'tools/generate-timeline-project-fixtures.mjs',fixtureCases:cases.length},null,2)+'\n');
console.log(cases.length+' production-header/owner fixtures and three actual web Worker GeoPackages generated from '+sha);
