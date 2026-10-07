import {buildGpuStrokeInstances} from '../tests/fixtures/web-p8-stroke-source/gpu-stroke-geometry.js';
import {readFileSync,writeFileSync,mkdirSync,existsSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
import path from 'node:path';

const [binary,out]=process.argv.slice(2);
assert.ok(binary&&out,'native probe and fresh evidence directory required');
assert.equal(existsSync(out),false);mkdirSync(out,{recursive:true});
const source=new URL('../tests/fixtures/web-p8-stroke-source/',import.meta.url);
const manifest=JSON.parse(readFileSync(new URL('source-manifest.json',source)));
assert.equal(manifest.webSHA,'ebcfae4d27b29cbbea6416a7045a4806930204be');
for(const file of manifest.files)assert.equal(createHash('sha256').update(readFileSync(new URL(file.file,source))).digest('hex'),file.sha256);
const cases=[
 {id:'empty',segments:[]},{id:'one',segments:[0,0,10,0]},
 {id:'open-right-angle',segments:[0,0,10,0,10,0,10,10]},
 {id:'closed',segments:[0,0,10,0,10,0,10,10,10,10,0,0]},
 {id:'disconnected',segments:[0,0,10,0,20,0,30,0]},
 {id:'invalid-and-degenerate',segments:[0,0,0,0,'NaN',0,1,1,0,0,1,1,1,1,2,1]},
 {id:'dateline-unwrapped',segments:[179,0,181,0,181,0,182,10]},
 {id:'chain-epsilon',segments:[0,0,1e-10,1,1e-10,1,0,0]},
];
for(let i=0;i<32;i++){
 const f=Math.fround,x=f(i*.37),y=f(i*.013);
 cases.push({id:`fraction-${i}`,segments:[x,y,f(x+.003),f(y+.01),f(x+.003),f(y+.01),f(x+.111),f(y+.5)]});
}
const input=path.resolve(out,'inputs.json');writeFileSync(input,JSON.stringify({cases},null,2));
const result=spawnSync(binary,[input],{encoding:'utf8',timeout:60000});
writeFileSync(path.join(out,'stdout.json'),result.stdout??'');writeFileSync(path.join(out,'stderr.txt'),result.stderr??'');
assert.ifError(result.error);assert.equal(result.status,0);
const native=JSON.parse(result.stdout);assert.equal(native.expected,cases.length);assert.equal(native.processed,cases.length);
const differences=[];
for(let i=0;i<cases.length;i++){
 const web=buildGpuStrokeInstances(cases[i].segments.map(v=>v==='NaN'?NaN:v)),app=native.cases[i];
 for(const field of ['instances','nodes','segmentCount','nodeCount','joinCount','capCount','closedChainCount','invalidSegmentCount']){
  const expected=field==='instances'||field==='nodes'?Array.from(web[field]):web[field];
  try{assert.deepEqual(app[field],expected);}catch{differences.push({case:cases[i].id,field,web:expected,app:app[field]});}
 }
}
const receipt={webSHA:manifest.webSHA,nativePID:result.pid,nativeExit:result.status,expected:cases.length,processed:native.processed,mismatch: differences.length,skip:0,binarySha256:createHash('sha256').update(readFileSync(binary)).digest('hex'),sourceManifestSha256:createHash('sha256').update(readFileSync(new URL('source-manifest.json',source))).digest('hex'),differences};
writeFileSync(path.join(out,'receipt.json'),JSON.stringify(receipt,null,2));console.log(JSON.stringify(receipt));if(differences.length)process.exitCode=1;
