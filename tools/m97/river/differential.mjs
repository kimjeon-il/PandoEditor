import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {loadOracle,syntheticCases,realCases,stable,outputHash,assertBehavior,assertExact} from './oracle.mjs';
const args=process.argv.slice(2),option=name=>args[args.indexOf(name)+1];
const native=option('--native');assert.ok(args.includes('--native')&&native,'--native probe required');
const oracle=await loadOracle(),directory=fs.mkdtempSync(path.join(os.tmpdir(),'m972-river-'));
const evidence=args.includes('--evidence')?option('--evidence'):null;if(evidence)fs.mkdirSync(evidence,{recursive:true});
const summary={node:process.version,v8:process.versions.v8,normalization:'Only diagnostics.computeMs excluded; exact complete values, including geometry and identities.',suites:[]};
function run(label,cases,provider=null,provenance=null){
  console.error('Comparing '+label+': '+cases.length+' full-output cases; Node RSS '+process.memoryUsage().rss);
  const input=path.join(directory,label+'.json');fs.writeFileSync(input,JSON.stringify(cases));
  const child=spawnSync(native,[input,...(provider?['--provider',provider]:[])],{encoding:'utf8',maxBuffer:40*1024*1024});
  if(child.status!==0)throw new Error(`Native ${label} failed (${child.status}): ${child.stderr}\n${child.stdout}`);
  const actual=JSON.parse(child.stdout),expected=cases.map(oracle.observe);
  if(evidence){fs.writeFileSync(path.join(evidence,label+'-native.json'),JSON.stringify(actual));fs.writeFileSync(path.join(evidence,label+'-node.json'),JSON.stringify({observations:expected}));fs.writeFileSync(path.join(evidence,label+'-native-stderr.txt'),child.stderr);}
  assertBehavior(actual.observations,cases);assertBehavior(expected,cases);
  const results=[];let normalizedGeometryChanged=0;
  for(let i=0;i<cases.length;i++){
    const a=actual.observations[i],e=expected[i];assertExact(stable(a),stable(e),label+'/'+cases[i].name+' complete output');
    const presentation=rows=>rows.map(c=>({...c,geometry:oracle.normalize(c.geometry)}));
    const ap=presentation(a.result.candidates),ep=presentation(e.result.candidates);assertExact(ap,ep,'presentation-only normalization');
    for(let j=0;j<ap.length;j++){assert.equal(ap[j].key,a.result.candidates[j].key);if(JSON.stringify(ap[j].geometry)!==JSON.stringify(a.result.candidates[j].geometry))normalizedGeometryChanged++;}
    if(provider){const source=actual.sources[i],features=cases[i].request.riverFeatures;
      const common=f=>({id:String(f.id),pandolabId:String(f.properties.pandolab_id),logicalFid:f.properties.__logicalFid,geometry:f.geometry});
      assertExact(source.features,features.map(common),'native common logical-source identity and complete geometry');
      assert.deepEqual(source.logicalIds,provenance.sources.find(s=>s.id===cases[i].request.donors[0].countryId).logicalIds);
      assert.equal(source.version,provenance.version.slice(1));assert.equal(source.indexSha256,provenance.indexSha256);
    }
    results.push({name:cases[i].name,exact:true,cells:a.result.candidates.length,sha256:outputHash(a),expectedSha256:outputHash(e)});
  }
  summary.suites.push({label,qt:actual.qtVersion,exactMatches:results.length,total:cases.length,normalizedGeometryChanged,provenance,memorySamples:actual.memorySamples,observations:results});
  return actual.memorySamples;
}
try {
  if(args.includes('--synthetic'))run('synthetic',syntheticCases());
  if(args.includes('--full')){
    const current=process.env.PANDOEDITOR_HYDRO_FULL_MANIFEST;assert.ok(current,'Required PANDOEDITOR_HYDRO_FULL_MANIFEST missing; this gate never skips.');
    const rawManifest=path.resolve(path.dirname(current),'../v0.13.0/manifest.json');
    console.error('Generating pinned v0.13.0 inputs');const raw=await realCases(rawManifest);run('original-v0.13.0',raw.cases,null,raw.provenance);
    const cancelInput=path.join(directory,'cancel.json');fs.writeFileSync(cancelInput,JSON.stringify([raw.cases[0]]));
    const cancel=spawnSync(native,[cancelInput,'--cancel-test'],{encoding:'utf8',maxBuffer:1024*1024});
    assert.equal(cancel.status,0,cancel.stderr);summary.cancellation=JSON.parse(cancel.stdout);
    console.error('Generating current v0.13.1 inputs');const live=await realCases(current,{detail:true});run('native-provider-v0.13.1',live.cases,current,live.provenance);
    const repeated=Array.from({length:6},(_,index)=>({...structuredClone(live.cases[0]),name:'SRB-repeat-'+index}));
    const repeatedMemory=run('repeated-native-provider',repeated,current,live.provenance);
    assert.ok(repeatedMemory.every(sample=>sample.providerCacheBytes===repeatedMemory[0].providerCacheBytes),'Repeated source requests must not grow the physical pack cache');
    // Current enriched metadata is separately pinned. The kernel-relevant geometry
    // and identities must remain the same as the default upstream corpus.
    assertExact(raw.cases.map(c=>stable(oracle.observe(c))),live.cases.map(c=>stable(oracle.observe(c))),"v0.13.0 vs current enriched metadata kernel outputs");
  }
  if(evidence)fs.writeFileSync(path.join(evidence,'native-summary.json'),JSON.stringify(summary,null,2)+'\n');
  console.log(JSON.stringify(summary,null,2));
}finally{fs.rmSync(directory,{recursive:true,force:true});}
