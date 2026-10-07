// One-time explicit diagnostic input builder. Existing pinned fixtures and
// captured expectations are read only; no oracle or shared fixture is written.
import {readFileSync,writeFileSync,readdirSync} from 'node:fs';
import {resolve,relative,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {gunzipSync} from 'node:zlib';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
const tools=dirname(fileURLToPath(import.meta.url)),root=resolve(tools,'..');
const capturePath='tests/fixtures/web-m973-split/lifecycle-observations-v2.json.gz';
const capture=JSON.parse(gunzipSync(readFileSync(resolve(root,capturePath))));
const ids=['root-six-crossing','root-hole','root-multi','root-date-line'];
const splitInputs=ids.map(id=>{const row=capture.cases.find(x=>x.case===id);assert.ok(row?.input,id);return row.input;});
const pins=[];
const pin=path=>{const bytes=readFileSync(resolve(root,path));pins.push({path:relative(tools,resolve(root,path)).replaceAll('\\','/'),bytes:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')});};
pin(capturePath);pin('tests/fixtures/web-m973-split/observation-manifest.json');pin('tests/fixtures/web-m974/input-manifest.json');pin('tests/fixtures/web-m974/source-manifest.json');pin('assets/place/manifest.json');
const hydro='tests/fixtures/web-hydro';
const walk=dir=>{for(const entry of readdirSync(resolve(root,dir),{withFileTypes:true})){const path=dir+'/'+entry.name;if(entry.isDirectory())walk(path);else pin(path);}};
walk(hydro);
const manifest={schema:'pando-m98-editing-diagnostic-input-v1',fixedWebSha:'ebcfae4d27b29cbbea6416a7045a4806930204be',fixtureClass:'explicitly reduced/synthetic diagnostic; independent from full-data acceptance',splitInputSource:{path:relative(tools,resolve(root,capturePath)).replaceAll('\\','/'),inputField:'cases[].input',behavioralCommit:capture.behavioralCommit,caseIds:ids,scope:'Retained original input coordinates only. Original observations and expected values remain immutable; this probe does not assert their legacy dateline rejection as current behavior.'},productionPlaceManifest:'../assets/place/manifest.json',hydroManifest:'../tests/fixtures/web-hydro/v0.13.1/manifest.json',caseCount:14,snapSamplesPerCase:300,boundaryOwnerCounts:[2,4,8,16],pins,splitInputs};
writeFileSync(resolve(tools,'native-editing-performance-diagnostic.json'),JSON.stringify(manifest,null,2)+'\n');
console.log(JSON.stringify({created:'tools/native-editing-performance-diagnostic.json',caseCount:14,pins:pins.length,splitInputIds:ids,expectationsWritten:0}));
