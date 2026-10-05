import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {createSuite,verifySuite,canonicalHash,runtimePin} from './suite.mjs';
import {loadOracle,outputHash,sha256,assertBehavior} from './oracle.mjs';
const args=process.argv.slice(2),option=k=>args[args.indexOf(k)+1];
assert.ok(args.includes('--native')&&args.includes('--output'));
const binary=path.resolve(option('--native')),directory=path.resolve(option('--output'));
const binarySha256=sha256(fs.readFileSync(binary));
const commit=process.env.GITHUB_SHA||spawnSync('git',['rev-parse','HEAD'],{encoding:'utf8'}).stdout.trim(),runId=process.env.GITHUB_RUN_ID||'local-development';
const manifest=process.env.PANDOEDITOR_HYDRO_FULL_MANIFEST,countries=process.env.PANDOEDITOR_RIVER_COUNTRIES;
assert.ok(manifest&&countries,'Required full hydro and original full-country files');
const payload=await createSuite(path.resolve(path.dirname(manifest),'../v0.13.0/manifest.json'),countries,{commit,runId,controllerManifestPath:manifest});verifySuite(payload);
fs.mkdirSync(directory,{recursive:true});fs.writeFileSync(path.join(directory,'payload.json'),JSON.stringify(payload));
const cached=new Map();let qt=null;
if(args.includes('--reuse'))for(const label of ['synthetic','original-v0.13.0']) {
  const base=path.resolve(option('--reuse')),bytes=fs.readFileSync(path.join(base,label+'-native.json')),receipt=JSON.parse(fs.readFileSync(path.join(base,label+'-receipt.json'))),data=JSON.parse(bytes);
  assert.equal(receipt.binarySha256,binarySha256,'Cached observations must use this exact compiled executable');assert.equal(receipt.commit,commit,'Cached native commit');assert.equal(receipt.runId,runId,'Cached native run');assert.equal(sha256(bytes),receipt.outputFileSha256);assert.equal(data.qtVersion,runtimePin.qt);qt=data.qtVersion;
  assert.equal(data.observations.length,receipt.caseHashes.length);assert.equal(data.presentations.length,data.observations.length);assert.equal(data.traces.length,data.observations.length);
  for(let i=0;i<data.observations.length;i++){
    const name=data.observations[i].name,row=payload.cases.find(r=>r.name===name);assert.ok(row&&!cached.has(name));assert.equal(receipt.caseHashes[i].name,name);assert.equal(receipt.caseHashes[i].sha256,canonicalHash(row));
    cached.set(name,{raw:data.observations[i],presentation:data.presentations[i],trace:data.traces[i]});
  }
}
const pending=payload.cases.filter(c=>!cached.has(c.name));
if(pending.length){const input=path.join(directory,'native-pending-inputs.json');fs.writeFileSync(input,JSON.stringify(pending));const child=spawnSync(binary,[input],{encoding:'utf8',maxBuffer:100*1024*1024});fs.writeFileSync(path.join(directory,'native-stderr.txt'),child.stderr||'');assert.equal(child.status,0,child.stderr);const data=JSON.parse(child.stdout);qt=data.qtVersion;assert.equal(data.observations.length,pending.length);assert.equal(data.presentations.length,pending.length);assert.equal(data.traces.length,pending.length);for(let i=0;i<pending.length;i++){assert.equal(data.observations[i].name,pending[i].name);cached.set(pending[i].name,{raw:data.observations[i],presentation:data.presentations[i],trace:data.traces[i]});}}
assert.equal(qt,runtimePin.qt);
const report={schema:'river-native-observations-v2',identity:payload.identity,runtime:{qt,platform:process.platform,arch:process.arch,binarySha256},observations:payload.cases.map(c=>cached.get(c.name).raw),presentations:payload.cases.map(c=>cached.get(c.name).presentation),traces:payload.cases.map(c=>cached.get(c.name).trace)};
assertBehavior(report.observations,payload.cases);assert.equal(report.observations.length,42);fs.writeFileSync(path.join(directory,'native-report.json'),JSON.stringify(report));
const oracle=await loadOracle(),node=payload.cases.map(oracle.observe);assertBehavior(node,payload.cases);
fs.writeFileSync(path.join(directory,'node-diagnostic.json'),JSON.stringify({role:'Different-engine diagnostic, not browser expected values.',runtime:{node:process.version,v8:process.versions.v8},identity:payload.identity,observations:node,comparison:node.map((r,i)=>({name:r.name,nodeSha256:outputHash(r),nativeSha256:outputHash(report.observations[i]),equal:outputHash(r)===outputHash(report.observations[i])}))}));
console.log(JSON.stringify({nativeCases:42,reused:42-pending.length,calculated:pending.length,identity:payload.identity}));
