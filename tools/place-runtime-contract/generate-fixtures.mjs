// Synthetic mechanism fixtures encoded by the pinned Web codec, never a production dataset.
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { pathToFileURL, fileURLToPath } from 'node:url';
const commit='ebcfae4d27b29cbbea6416a7045a4806930204be';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'../..');
const repo=process.argv[2] || 'D:/dev/Pandoeditor(Web)';
const output=resolve(root,'tests/fixtures/web-place-runtime-source');
const sha=b=>createHash('sha256').update(b).digest('hex');
const paths=['assets/js/modules/place-contract.js','assets/js/modules/place-codec.js','assets/js/modules/place-worker-store.js','assets/js/modules/place-runtime.js','assets/js/modules/label-layout.js','assets/js/modules/hydro-tile-window.js','assets/data/places/manifest.json','tests/helpers/place-view.mjs','assets/js/vendor/d3.min.js'];
mkdirSync(output,{recursive:true}); writeFileSync(resolve(output,'package.json'),'{"type":"module"}\n');
const sources=[];
for(let index=0;index<paths.length;index++){
 const path=paths[index],bytes=execFileSync('git',['-C',repo,'show',`${commit}:${path}`]);const target=resolve(output,path);mkdirSync(dirname(target),{recursive:true});writeFileSync(target,bytes);sources.push({path,sha256:sha(bytes),bytes:bytes.length});
 if(/\.(?:m?js)$/.test(path)&&!path.includes('/vendor/'))for(const match of bytes.toString('utf8').matchAll(/(?:from\s*|import\s*)['"]([^'"]+)['"]/g)){
  if(!match[1].startsWith('.'))continue;const dependency=resolve(root,dirname(path),match[1]).substring(root.length+1).replaceAll('\\','/');if(!paths.includes(dependency))paths.push(dependency);
 }
}
const {encodePlaceTile}=await import(pathToFileURL(resolve(output,'assets/js/modules/place-codec.js')));
const basic=[{source:'synthetic',sourceId:'capital',name:'서울',kind:'capital',coordinates:[0,0],population:10,priority:90,minZoom:0},
 {source:'synthetic',sourceId:'city',name:'서울 도시',kind:'city',coordinates:[1,1],population:20,priority:70,minZoom:0},
 {source:'synthetic',sourceId:'hidden',name:'가려진 도시',kind:'city',coordinates:[179,0],population:999,priority:999,minZoom:0},
 {source:'synthetic',sourceId:'back',name:'뒤편',kind:'capital',coordinates:[180,0],priority:900,minZoom:0}];
const artifacts=[];
function fixture(name,groups,columns=groups.length){
 const folder=resolve(output,'synthetic',name);mkdirSync(folder,{recursive:true});
 const manifest={version:1,revision:`synthetic-${name}-v1`,stages:[{id:0,minZoom:0,columns,rows:1}],tiles:{},shards:{},search:{}};
 groups.forEach((records,i)=>{const b=Buffer.from(encodePlaceTile(records));const filename=`${i}.bin`;writeFileSync(resolve(folder,filename),b);const spec={shard:String(i),offset:0,length:b.length,sha256:sha(b)};manifest.tiles[`0/${i}-0`]=spec;manifest.shards[String(i)]={url:filename,bytes:b.length,sha256:sha(b)};artifacts.push({path:`synthetic/${name}/${filename}`,sha256:sha(b),bytes:b.length});});
 if(name==='basic')manifest.search['서울']=[{...manifest.tiles['0/0-0'],first:'서울',last:'서울 도시'}];
 const b=Buffer.from(JSON.stringify(manifest,null,2)+'\n');writeFileSync(resolve(folder,'manifest.json'),b);artifacts.push({path:`synthetic/${name}/manifest.json`,sha256:sha(b),bytes:b.length});
}
fixture('basic',[basic],1);
const rows=Array.from({length:2048},(_,i)=>({source:'synthetic',sourceId:String(i).padStart(5,'0'),name:`서울 ${String(i).padStart(5,'0')}`,kind:'capital',coordinates:[0,0],population:i,priority:90,minZoom:0}));
fixture('dense',Array.from({length:4},(_,i)=>rows.slice(i*512,(i+1)*512)),4);
fixture('overscan',Array.from({length:4},(_,i)=>i===3?[basic[0]]:rows.slice(i*512,(i+1)*512).map(r=>({...r,coordinates:[179,0],priority:999}))),4);
writeFileSync(resolve(output,'source-manifest.json'),JSON.stringify({version:1,webCommit:commit,scope:'synthetic mechanism fixtures; production manifest remains empty-v1; no 64-review runtime acceptance',sources,artifacts},null,2)+'\n');
const production=execFileSync('git',['-C',repo,'show',`${commit}:assets/data/places/manifest.json`]);mkdirSync(resolve(root,'assets/place'),{recursive:true});writeFileSync(resolve(root,'assets/place/manifest.json'),production);
console.log(JSON.stringify({webCommit:commit,sourceFiles:sources.length,syntheticArtifacts:artifacts.length,productionManifestSha256:sha(production)}));
