import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {root,fixture,riverRoot,verifySources,syntheticCases,realCases,canonical,sha256} from './oracle.mjs';
import {controllerAnnexRole,requiredControllerAnnexScenarios,assertControllerAnnexContract,requiredControllerLifecycleScenarios,assertControllerLifecycleContract,controllerSourceBundle} from './controller-suite.mjs';
import {browserBaseline} from './browser-baseline.mjs';
import {stressCases,instrumentRiverSource,collinearTargets} from './stress-cases.mjs';
export const canonicalHash=value=>sha256(JSON.stringify(canonical(value)));
export const runtimePin={playwright:'1.62.1',chromium:'151.0.7922.34',revision:'1234',v8:'15.1.206.8',v8Commit:'f479186c16abdb6fa05539fe957bb84deee830df',qt:'6.8.3'};
export const annexRole='Browser oracle coverage only; native sliver-aware preview parity is pending D3.';
export function requiredAnnexScenarios(){return [
      {name:'Serbia-to-Hungary-raw-fixture',base:'SRB-raw',representation:'raw-upstream-fixture',revisionMode:'explicit-1',donorId:'SRB',targetId:'HUN',samplePoints:[[19.6,45.7],[20.7,45.5],[19.8,45.0]]},
      {name:'Moldova-to-Romania-raw-fixture',base:'MDA-raw',representation:'raw-upstream-fixture',revisionMode:'explicit-1',donorId:'MDA',targetId:'ROU',samplePoints:[[28.5,47.1]],capturedBrowserKey:'MDA:river-cell:e3fb6c4e',historicalNodeKey:'MDA:river-cell:6c83e30a',captureRun:'37231724016',requireSharedTargetBoundary:true},
      {name:'Serbia-to-Hungary-live-presentation',base:'SRB-live',representation:'normalized-filtered-presentation',revisionMode:'live-coordinate-signature',donorId:'SRB',targetId:'HUN',samplePoints:[[19.6,45.7],[20.7,45.5],[19.8,45.0]]},
      {name:'Moldova-to-Romania-live-presentation',base:'MDA-live',representation:'normalized-filtered-presentation',revisionMode:'live-coordinate-signature',donorId:'MDA',targetId:'ROU',samplePoints:[[28.5,47.1]],captureRun:'37231724016',requireSharedTargetBoundary:true}
    ];}
export function assertAnnexContract(annex) {
  assert.equal(annex?.role,annexRole,'Required browser-only annex scope');
  assert.deepEqual(annex?.scenarios,requiredAnnexScenarios(),'Required four raw/live annex scenarios');
}
export function suiteIdentity(payload){return {schema:payload.schema,commit:payload.commit,runId:payload.runId,casesSha256:canonicalHash(payload.cases),orderedCaseIds:payload.cases.map(c=>c.name),sourceHashes:Object.fromEntries([...Object.entries(payload.modules).map(([name,m])=>[name,m.sha256]),...Object.entries(payload.controllerSources.modules).map(([name,m])=>['controller/'+name,m.sha256])]),boundaryHashes:Object.fromEntries(Object.entries(payload.boundaries).map(([name,m])=>[name,m.sha256])),worldSha256:payload.world.sha256,annexSha256:canonicalHash(payload.annex),controllerAnnexSha256:canonicalHash(payload.controllerAnnex),controllerLifecycleSha256:canonicalHash(payload.controllerLifecycle),controllerSourcesSha256:canonicalHash(payload.controllerSources),branchDiagnosticsSha256:canonicalHash(payload.branchDiagnostics),provenanceSha256:canonicalHash(payload.provenance),runtimePin:payload.runtimePin};}
export async function createSuite(manifestPath,countryPath,{commit,runId,controllerManifestPath=manifestPath}={}) {
  const {manifest}=verifySources();assert.match(commit||'',/^[0-9a-f]{40}$/,'Exact application commit required');assert.ok(runId,'Run identity required');
  const real=await realCases(manifestPath),original=[...syntheticCases(),...real.cases],baseline=browserBaseline();
  assert.equal(canonicalHash(original),baseline.capture.casesSha256,'Original 24 captured inputs must remain unchanged');
  const cases=[...original,...stressCases(original)];assert.equal(cases.length,42);assert.equal(new Set(cases.map(c=>c.name)).size,42);
  const controllerReal=controllerManifestPath===manifestPath?real:await realCases(controllerManifestPath);
  const controllerSources=controllerSourceBundle();
  const modules={};
  const modulePath={
    'river-territory-partition.js':path.join(riverRoot,'original/river-territory-partition.js'),
    'planar-graph-faces.js':path.join(riverRoot,'original/planar-graph-faces.js'),
    'polygon-clipping-0.15.7.js':path.join(root,'assets/geometry/polygon-clipping-0.15.7.js'),
    'polygon-geometry.js':path.join(riverRoot,'original/polygon-geometry.js'),
  };
  for(const name of ['map-edit-geometry.js','coordinate-bounds.js','polygon-clipping-calculation.js','map-edit-country-commands.js'])modulePath[name]=path.join(root,'tests/fixtures/web-m97/source/assets/js/modules',name);
  for(const [name,file] of Object.entries(modulePath)){const source=fs.readFileSync(file,'utf8');modules[name]={source,sha256:sha256(source)};}
  const boundaries={};for(const name of ['bridge.js','presentation.js','workspace-observation.js']){const source=fs.readFileSync(path.join(riverRoot,name),'utf8');boundaries[name]={source,sha256:sha256(source)};}
  {const source=fs.readFileSync(path.join(root,'tools/m97/river/browser/annex.js'),'utf8');boundaries['annex.js']={source,sha256:sha256(source)};}
  for(const name of ['controller-annex.js']){const source=fs.readFileSync(path.join(root,'tools/m97/river/browser',name),'utf8');boundaries[name]={source,sha256:sha256(source)};}
  boundaries['controller-runtime.js']=controllerSources.runtime;
  const bytes=fs.readFileSync(countryPath);assert.equal(sha256(bytes),manifest.countrySource.sha256,'Original full-country validation context');
  const traced=instrumentRiverSource(modules['river-territory-partition.js'].source);
  const payload={schema:'river-native-chromium-suite-v2',commit,runId,runtimePin,cases,modules,boundaries,controllerSources,provenance:real.provenance,
    controllerLifecycle:{scenarios:requiredControllerLifecycleScenarios()},
    controllerAnnex:{role:controllerAnnexRole,scenarios:requiredControllerAnnexScenarios(),source:{version:controllerReal.provenance.version.slice(1),indexSha256:controllerReal.provenance.indexSha256,manifestSha256:controllerReal.provenance.manifestSha256,provenance:controllerReal.provenance,inputs:['SRB','MDA'].map(donorId=>({donorId,riverFeatures:controllerReal.cases.find(row=>row.name===donorId+'-raw').request.riverFeatures}))}},
    world:{source:bytes.toString('utf8'),sha256:sha256(bytes),bytes:bytes.length,webCommit:manifest.webCommit,path:manifest.countrySource.path,gitBlob:manifest.countrySource.gitBlob},
    branchDiagnostics:{targets:collinearTargets,source:traced.source,sha256:sha256(traced.source),replacements:traced.replacements,role:'Separate instrumented run; its complete raw output must equal the uninstrumented browser output.'},
    annex:{role:annexRole,scenarios:requiredAnnexScenarios()}};
  payload.identity=suiteIdentity(payload);return payload;
}
export function verifySuite(payload) {
  assert.equal(payload.schema,'river-native-chromium-suite-v2');assertAnnexContract(payload.annex);assertControllerAnnexContract(payload.controllerAnnex);assertControllerLifecycleContract(payload.controllerLifecycle);assert.equal(payload.cases.length,42);assert.equal(new Set(payload.cases.map(c=>c.name)).size,42);assert.deepEqual(payload.cases.slice(24),stressCases(payload.cases.slice(0,24)),'Exact bounded stress construction');
  assert.deepEqual(payload.runtimePin,runtimePin);assert.deepEqual(payload.identity,suiteIdentity(payload));
  for(const item of [...Object.values(payload.modules),...Object.values(payload.controllerSources.modules),...Object.values(payload.boundaries)])assert.equal(sha256(item.source),item.sha256);
  assert.equal(sha256(payload.world.source),payload.world.sha256);assert.equal(Buffer.byteLength(payload.world.source),payload.world.bytes);
  const {manifest,pins}=verifySources();for(const [name,hash] of Object.entries(pins))assert.equal(payload.modules[name].sha256,hash);
  for(const source of manifest.sources){const name=path.basename(source.path);if(payload.modules[name])assert.equal(payload.modules[name].sha256,source.sha256,name);}
  assert.equal(payload.modules['polygon-clipping-0.15.7.js'].sha256,'8c1ed56df8b1f97b047f82d91b910aacdaff67d8d9a55f2495eb26e8369186f7');
  const controller=controllerSourceBundle();assert.equal(canonicalHash(payload.controllerSources),canonicalHash(controller),'Exact verified production controller source bundle');
  const current=manifest.datasets['v'+payload.controllerAnnex.source.version];assert.ok(current,'Pinned configured controller source');assert.equal(payload.controllerAnnex.source.manifestSha256,current.manifestSha256);assert.equal(payload.controllerAnnex.source.indexSha256,current.indexSha256);
  assert.deepEqual(payload.controllerAnnex.source.inputs.map(row=>row.donorId),['SRB','MDA']);assert.ok(payload.controllerAnnex.source.inputs.every(row=>row.riverFeatures.length>0));
  for(const [name,item] of Object.entries(payload.boundaries)){if(name==='controller-runtime.js'){assert.equal(item.sha256,controller.runtime.sha256);continue;}const file=['annex.js','controller-annex.js'].includes(name)?path.join(root,'tools/m97/river/browser',name):path.join(riverRoot,name);assert.equal(item.sha256,sha256(fs.readFileSync(file)),name);}
  assert.equal(payload.world.sha256,manifest.countrySource.sha256);assert.equal(canonicalHash(payload.cases.slice(0,24)),browserBaseline().capture.casesSha256);
  const trace=instrumentRiverSource(payload.modules['river-territory-partition.js'].source);assert.equal(trace.source,payload.branchDiagnostics.source);assert.equal(sha256(trace.source),payload.branchDiagnostics.sha256);
}
