import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { spawnSync } from 'node:child_process';

const [probe, webRoot, evidenceDir] = process.argv.slice(2);
assert.ok(probe && webRoot && evidenceDir, 'usage: node tools/timeline-exchange-oracle.mjs <native-probe> <web-checkout> <evidence-dir>');
const { prepareProjectForStorage, prepareProjectForActivation } = await import(pathToFileURL(path.join(webRoot, 'assets/js/modules/project-state.js')));
const { createProjectSerializer, fingerprintProjectBaseline } = await import(pathToFileURL(path.join(webRoot, 'assets/js/modules/project-serializer.js')));
const { restoreTimelineStorage } = await import(pathToFileURL(path.join(webRoot, 'assets/js/modules/timeline-storage.js')));
const { staticTimelineViews } = await import(pathToFileURL(path.join(webRoot, 'assets/js/modules/timeline-static-view.js')));
fs.mkdirSync(evidenceDir, { recursive: true });
const json = name => JSON.parse(fs.readFileSync(path.join(webRoot, 'tests/fixtures/timeline-exchange', name+'.json'), 'utf8'));
const canonical = value => Array.isArray(value) ? value.map(canonical).sort((a,b) =>
  a && b && typeof a === 'object' && 'id' in a && 'id' in b ? a.id.localeCompare(b.id,'en') || (a.version||0)-(b.version||0) : 0)
  : value && typeof value === 'object' ? Object.fromEntries(Object.keys(value).sort().map(key=>[key,canonical(value[key])])) : value;
const source = {schemaVersion:1,kind:'user',dataset:'exchange',version:'9',sourceId:'original',sourceFormat:'geojson',sourceType:'Point',importedAt:'2026-10-04',details:{provider:'fixture'}};
const content = json('static');
const labelId='11000000-0000-4000-8000-000000000001', genericId='11000000-0000-4000-8000-000000000002';
content.labels=[{id:labelId,name:'서울',kind:'city',notes:'원본',coordinates:[127,37],territorialUnitId:'A',source}];
content.genericFeatures=[{type:'Feature',id:genericId,geometry:{type:'Point',coordinates:[128,38]},properties:{schemaVersion:2,name:'원본 Point',notes:'source metadata',color:'#123456',locked:false,source}}];
content.geometries.push({id:'web-label:'+labelId,version:1,geojson:{type:'Point',coordinates:[127,37]}},
  {id:'web-genericFeatures:'+genericId,version:1,geojson:content.genericFeatures[0].geometry});
content.labelSettings={['label:'+labelId]:{pinned:true,manualPosition:[10,20],priority:10,minZoom:0,maxZoom:null,collisionGroup:'map'}};
content.layerPresentation.overlayOrder=['genericFeatures','regions','subunits','distributions'];
let passed=0;
function native(input,name) {
  const inputFile=path.join(evidenceDir,name+'.source.json'), outputFile=path.join(evidenceDir,name+'.app-web.json');
  fs.writeFileSync(inputFile,typeof input==='string' ? input : JSON.stringify(input));
  const result=spawnSync(probe,[inputFile,outputFile],{encoding:'utf8'});
  return {...result,outputFile};
}
const workerContent=JSON.parse(fs.readFileSync(new URL('../tests/fixtures/timeline-exchange/content.json',import.meta.url),'utf8'));
const staticProject=json('static');
const storage=restoreTimelineStorage({schemaVersion:1,records:staticProject.timelineRecords,geometries:staticProject.geometries},staticProject.territorialEntities.map(f=>({id:f.id,entityKind:f.properties.entityKind})));
const baseline=staticTimelineViews(staticProject.territorialEntities,storage.records,storage.geometries);
const fingerprint=await fingerprintProjectBaseline(baseline);
const domainKeys=['timelineRecords','geometries','labels','hydroEdits','genericFeatures','distributionLayers','distributionEntries','sourceInfo','physicalSourceInfo','physicalSettings','labelSettings','distributionSettings','layerVisibility','itemVisibility','layerPresentation'];
const snapshot={territorialEntities:staticProject.territorialEntities,projectFields:Object.fromEntries(domainKeys.map(k=>[k,staticProject[k]])),fullAutosave:false,baseDatasetFingerprint:fingerprint,entityDelta:{changed:[],removedIds:[]}};
const serializer=createProjectSerializer({appVersion:staticProject.version,baseDataset:staticProject.baseDataset,distributionModes:['territorial','geometry'],terrainDataset:'terrain',hydroDataset:'hydro',readSnapshot:()=>snapshot,now:()=>new Date(staticProject.savedAt)});
const delta=serializer.buildAutosave();
const recoveredDelta=prepareProjectForStorage(delta,{baseEntities:staticProject.territorialEntities,baseDataset:delta.baseDataset,baseDatasetFingerprint:fingerprint});
snapshot.fullAutosave=true;const fullAutosave=serializer.buildAutosave();
for(const [name,input] of [['static',staticProject],['complex',json('complex')],['content',content],['worker-content',workerContent],['full-autosave',fullAutosave],['delta-recovered',recoveredDelta]]) {
  const before=prepareProjectForStorage(input), result=native(input,name);
  assert.equal(result.status,0,name+': '+result.stderr);
  const after=prepareProjectForStorage(JSON.parse(fs.readFileSync(result.outputFile,'utf8')));
  for(const key of ['territorialEntities','timelineRecords','geometries','labels','genericFeatures','distributionLayers','distributionEntries','hydroEdits','sourceInfo','physicalSourceInfo','physicalSettings','labelSettings','layerPresentation','layerVisibility','itemVisibility','distributionSettings']) {
    // Optional presentation defaults are normalized by each real reader.
    if(key==='layerPresentation') {
      for(const field of ['objectOrder','overlayOrder'])assert.deepEqual(after[key][field]||[],before[key][field]||[],name+': '+key+'.'+field);
      for(const field of ['styles','objectStyles'])assert.deepEqual(canonical(after[key][field]||{}),canonical(before[key][field]||{}),name+': '+key+'.'+field);
    }
    else if(key==='labels'||key==='genericFeatures') {
      const clean=rows=>rows.map(row=>{const out=structuredClone(row);if(out.source)for(const k of Object.keys(out.source))if(out.source[k]===null)out.source[k]='';return out;});
      assert.deepEqual(canonical(clean(after[key])),canonical(clean(before[key])),name+': '+key);
    } else assert.deepEqual(canonical(after[key]),canonical(before[key]),name+': '+key);
  }
  if(name==='complex')assert.throws(()=>prepareProjectForActivation(after));
  else prepareProjectForActivation(after);
  console.log('PASS real web -> app QFile/native v10 -> app encodeWeb -> real web: '+name);passed++;
}
const deltaRejected=native(delta,'delta-without-baseline');assert.equal(deltaRejected.status,1,'native import must reject a delta with no matching baseline');assert.match(deltaRejected.stderr,/BASE_DATA_REQUIRED/);passed++;
const changedBaseline=structuredClone(baseline);changedBaseline[0].properties.name+=' changed source';
const changedFingerprint=await fingerprintProjectBaseline(changedBaseline);
assert.throws(()=>prepareProjectForStorage(delta,{baseEntities:staticProject.territorialEntities,baseDataset:delta.baseDataset,baseDatasetFingerprint:changedFingerprint}));passed++;
for(const [name,mutate] of [
  ['invalid-flag',p=>p.territorialEntities[0].properties.metadata.flagDataUrl='data:image/png;base64,aGVsbG8='],
  ['non-uuid-content',p=>p.labels[0].id='not-a-project-uuid'],
]) {
  const input=structuredClone(content);mutate(input);const result=native(input,name);
  assert.notEqual(result.status,0,name+': native codec accepted unsupported content');passed++;
}
for(const token of ['9007199254740993','1e400','0.123456789012345678901'])for(const location of ['metadata','source']) {
  const input=structuredClone(content);
  const payload={nested:[{number:'__LOSSLESS_NUMBER__'}]};
  if(location==='metadata')input.territorialEntities[0].properties.metadata.precision=payload;
  else input.labels[0].source.details=payload;
  const bytes=JSON.stringify(input).replace('"__LOSSLESS_NUMBER__"',token);
  const result=native(bytes,'unrepresentable-'+location+'-'+passed);
  assert.equal(result.status,1,'unsupported numeric precision must reject before web publication');
  assert.match(result.stderr,/UNREPRESENTABLE_WEB_NUMBER/);
  assert.equal(fs.existsSync(result.outputFile),false,'failed export must not publish an output file');
  console.log('PASS actual native file boundary rejects numeric precision loss: '+location+' '+token);passed++;
}
console.log(passed+' exchange cases passed, 0 failed, 0 skipped');
