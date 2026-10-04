// Required native checks against captured REAL Chromium results. Node is a
// labelled different-engine diagnostic, never an authoritative output oracle.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {loadOracle,syntheticCases,realCases,stable,outputHash,assertBehavior,assertExact,canonical,sha256} from './oracle.mjs';
import {browserBaseline,assertCapturedBrowser} from './browser-baseline.mjs';
const args=process.argv.slice(2),option=name=>args[args.indexOf(name)+1];
const native=option('--native');assert.ok(args.includes('--native')&&native,'--native probe required');
const binarySha256=sha256(fs.readFileSync(native));
const oracle=await loadOracle(),baseline=browserBaseline(),directory=fs.mkdtempSync(path.join(os.tmpdir(),'m972-river-'));
const evidence=args.includes('--evidence')?option('--evidence'):process.env.PANDOEDITOR_RIVER_EVIDENCE;if(evidence)fs.mkdirSync(evidence,{recursive:true});
const summary={oracle:'Captured actual Chromium '+baseline.runtime.chromium,captureRun:baseline.capture.run,nodeDiagnostic:{node:process.version,v8:process.versions.v8,role:'Different-engine diagnostics; mismatches with Chromium are retained, not accepted as native/browser parity.'},suites:[]};
const hash=value=>sha256(JSON.stringify(canonical(value)));
function run(label,cases,provider=null,provenance=null){
  const input=path.join(directory,label+'.json');fs.writeFileSync(input,JSON.stringify(cases));
  const child=spawnSync(native,[input,...(provider?['--provider',provider]:[])],{encoding:'utf8',maxBuffer:60*1024*1024});
  if(child.status!==0)throw new Error(`Native ${label} failed (${child.status}): ${child.stderr}`);
  const actual=JSON.parse(child.stdout),node=cases.map(oracle.observe);assertBehavior(actual.observations,cases);assertBehavior(node,cases);
  const results=[];
  for(let i=0;i<cases.length;i++){
    const row=cases[i],a=actual.observations[i],diagnostic=node[i],name=row.name.startsWith('SRB-repeat-')?'SRB-raw':row.name;
    const pin=baseline.cases.find(c=>c.name===name);assert.ok(pin,'Missing actual browser case '+name);
    if(!provider)assertCapturedBrowser(row,a,name);
    else assert.equal(outputHash({...a,name}),pin.outputSha256,name+' exact captured-browser result through native provider');
    const shown=actual.presentations[i];assert.equal(shown.name,row.name);assert.ok(Array.isArray(shown.candidates));
    for(const cell of shown.candidates){const raw=a.result.candidates.find(c=>c.key===cell.key);assert.ok(raw);assertExact({...cell,geometry:raw.geometry},raw,'Presentation preserves raw identity/provenance');}
    if(provider){const source=actual.sources[i],features=row.request.riverFeatures.map(f=>({id:String(f.id),pandolabId:String(f.properties.pandolab_id),logicalFid:f.properties.__logicalFid,geometry:f.geometry}));
      assertExact(source.features,features,'native full logical source');assert.deepEqual(source.logicalIds,provenance.sources.find(s=>s.id===row.request.donors[0].countryId).logicalIds);assert.equal(source.version,provenance.version.slice(1));assert.equal(source.indexSha256,provenance.indexSha256);}
    results.push({name:row.name,exactCapturedBrowser:true,cells:a.result.candidates.length,rawSha256:outputHash(a),browserSha256:pin.outputSha256,nodeSha256:outputHash(diagnostic),nodeExact:outputHash(a)===outputHash(diagnostic)});
  }
  if(evidence){
    const bytes=JSON.stringify(actual);fs.writeFileSync(path.join(evidence,label+'-native.json'),bytes);fs.writeFileSync(path.join(evidence,label+'-node-diagnostic.json'),JSON.stringify({runtime:summary.nodeDiagnostic,observations:node}));fs.writeFileSync(path.join(evidence,label+'-native-stderr.txt'),child.stderr);
    fs.writeFileSync(path.join(evidence,label+'-receipt.json'),JSON.stringify({binarySha256,commit:process.env.GITHUB_SHA||null,runId:process.env.GITHUB_RUN_ID||'local-development',casesSha256:hash(cases),caseHashes:cases.map(row=>({name:row.name,sha256:hash(row)})),outputFileSha256:sha256(bytes)},null,2));
  }
  summary.suites.push({label,qt:actual.qtVersion,total:cases.length,exactCapturedBrowser:results.length,provenance,memorySamples:actual.memorySamples,observations:results});return actual.memorySamples;
}
try {
  if(args.includes('--synthetic'))run('synthetic',syntheticCases());
  if(args.includes('--full')){
    const current=process.env.PANDOEDITOR_HYDRO_FULL_MANIFEST;assert.ok(current,'Required PANDOEDITOR_HYDRO_FULL_MANIFEST missing; this gate never skips.');
    const raw=await realCases(path.resolve(path.dirname(current),'../v0.13.0/manifest.json'));run('original-v0.13.0',raw.cases,null,raw.provenance);
    const input=path.join(directory,'cancel.json');fs.writeFileSync(input,JSON.stringify([raw.cases[0]]));const cancel=spawnSync(native,[input,'--cancel-test'],{encoding:'utf8',maxBuffer:1024*1024});assert.equal(cancel.status,0,cancel.stderr);summary.cancellation=JSON.parse(cancel.stdout);
    const live=await realCases(current,{detail:true});run('native-provider-v0.13.1',live.cases,current,live.provenance);
    const repeated=Array.from({length:6},(_,i)=>({...structuredClone(live.cases[0]),name:'SRB-repeat-'+i})),memory=run('repeated-native-provider',repeated,current,live.provenance);assert.ok(memory.every(m=>m.providerCacheBytes===memory[0].providerCacheBytes));
    assertExact(raw.cases.map(c=>c.request.riverFeatures.map(f=>f.geometry)),live.cases.map(c=>c.request.riverFeatures.map(f=>f.geometry)),'Original/current decoded geometry unchanged');
  }
  if(evidence)fs.writeFileSync(path.join(evidence,(args.includes('--full')?'full':'synthetic')+'-browser-baseline-summary.json'),JSON.stringify(summary,null,2));console.log(JSON.stringify(summary,null,2));
}finally{fs.rmSync(directory,{recursive:true,force:true});}
