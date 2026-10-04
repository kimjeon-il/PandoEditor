import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import crypto from 'node:crypto';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {gunzipSync} from 'node:zlib';
export const root=fileURLToPath(new URL('../../../',import.meta.url));
export const fixture=path.join(root,'tests/fixtures/web-m972-river');
export const riverRoot=path.join(root,'assets/geometry/river');
export const sha256=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
export const canonical=value=>Array.isArray(value)?value.map(canonical):value&&typeof value==='object'?Object.fromEntries(Object.keys(value).sort().map(k=>[k,canonical(value[k])])):value;
export function stable(observation) {const copy=structuredClone(observation);if(copy.result)delete copy.result.diagnostics.computeMs;return copy;}
export const outputHash=row=>sha256(JSON.stringify(canonical(stable(row))));
export function verifySources() {
  const pins={
    'river-territory-partition.js':'18b32eb7db238beaed99bce8bac980e547a48c785a7bd2071fa56fa63f51db24',
    'planar-graph-faces.js':'283da7701c21cb80e4fd9ef97e8f68e69a8d6f0ec9fc611ee8c23cab45d6c804',
  };
  for(const [name,hash] of Object.entries(pins))assert.equal(sha256(fs.readFileSync(path.join(riverRoot,'original',name))),hash,name);
  assert.equal(sha256(fs.readFileSync(path.join(root,'assets/geometry/polygon-clipping-0.15.7.js'))),'8c1ed56df8b1f97b047f82d91b910aacdaff67d8d9a55f2495eb26e8369186f7');
  const manifest=JSON.parse(fs.readFileSync(path.join(fixture,'manifest.json')));
  for(const source of manifest.sources)assert.equal(sha256(fs.readFileSync(path.join(root,source.localPath))),source.sha256,source.path);
  assert.equal(sha256(fs.readFileSync(path.join(fixture,'countries.json'))),manifest.countrySource.subsetSha256);
  return {pins,manifest};
}
// Large geometry deepEqual failures can spend gigabytes formatting a diff.
// Compare complete canonical bytes, but report only bounded leaf discrepancies.
export function assertExact(actual,expected,label) {
  const a=JSON.stringify(canonical(actual)),b=JSON.stringify(canonical(expected));
  if(a===b)return;
  const differences=[];
  const concise=v=>{
    if(typeof v==='string'&&v.length>240)return {prefix:v.slice(0,160),characters:v.length,sha256:sha256(v)};
    if(v&&typeof v==='object')return {type:Array.isArray(v)?'array':'object',entries:Object.keys(v).length,sha256:sha256(JSON.stringify(canonical(v)))};
    return v;
  };
  const visit=(x,y,at)=>{
    if(differences.length>=12)return;
    if(x===y)return;
    if(x&&y&&typeof x==='object'&&typeof y==='object'){
      for(const key of new Set([...Object.keys(x),...Object.keys(y)]))visit(x[key],y[key],at+'/'+key);
    }else differences.push({path:at,actual:concise(x),expected:concise(y)});
  };
  visit(actual,expected,'');
  throw new Error(label+' exact mismatch: '+JSON.stringify({actualSha256:sha256(a),expectedSha256:sha256(b),firstDifferences:differences}));
}
export async function loadOracle() {
  verifySources();const context=vm.createContext({});
  vm.runInContext(fs.readFileSync(path.join(root,'assets/geometry/polygon-clipping-0.15.7.js'),'utf8'),context);
  const module=await import(pathToFileURL(path.join(riverRoot,'original/river-territory-partition.js')));
  const bridge=vm.runInThisContext(fs.readFileSync(path.join(riverRoot,'bridge.js'),'utf8'));
  // Original presentation normalizer is already pinned by the M9.7 oracle.
  await import(pathToFileURL(path.join(root,'tests/fixtures/web-m97/source/assets/js/modules/polygon-geometry.js')));
  return {module,clipper:context.polygonClipping,
    observe:row=>({name:row.name,...bridge(structuredClone(row),module,context.polygonClipping)}),
    normalize:geometry=>globalThis.PandoLabPolygonGeometry.normalizePolygonGeometry(geometry)};
}
export function syntheticCases() {return JSON.parse(fs.readFileSync(path.join(fixture,'synthetic-cases.json')));}
export async function realCases(manifestPath,{detail=false}={}) {
  const {manifest:provenance}=verifySources();
  assert.ok(manifestPath,'Required full hydro manifest missing. Set PANDOEDITOR_HYDRO_FULL_MANIFEST.');
  const bytes=fs.readFileSync(manifestPath),manifest=JSON.parse(bytes),version='v'+manifest.version;
  assert.equal(sha256(bytes),provenance.datasets[version]?.manifestSha256,'Pinned full hydro manifest');
  const accessed=[];const readAsset=spec=>{const filename=path.resolve(path.dirname(manifestPath),spec.url);const data=fs.readFileSync(filename);
    assert.equal(data.length,spec.bytes,spec.url+' bytes');assert.equal(sha256(data),spec.sha256,spec.url+' hash');accessed.push({url:spec.url,sha256:spec.sha256,bytes:data.length});return data;};
  const context=vm.createContext({TextDecoder,URL,performance,structuredClone,inputManifest:manifest});
  context.self=context;context.importScripts=()=>{};context.onmessage=null;
  let message;context.postMessage=value=>{message=structuredClone(value);};
  for(const name of ['earcut.min.js','geographic-boundary-core.js','hydro-tile-worker.js'])
    vm.runInContext(fs.readFileSync(path.join(root,'tests/fixtures/web-hydro/source',name),'utf8'),context);
  vm.runInContext('manifest = inputManifest',context);
  const decoder=vm.runInContext('({readGlobalIndex,readFeatureMetadata,readPack,mergeLogicalFragments,logicalPacks,packSpecs,queryLogicalFeatures,ensureDetailMetadata})',context);
  decoder.readGlobalIndex(gunzipSync(readAsset(manifest.index)));decoder.readFeatureMetadata(gunzipSync(readAsset(manifest.metadata.core)));
  if(detail) {context.__detail=gunzipSync(readAsset(manifest.metadata.detail));vm.runInContext('fetchGzip = async () => __detail',context);await decoder.ensureDetailMetadata();}
  const {createRiverCandidates}=await import(pathToFileURL(path.join(fixture,'source/assets/js/modules/app-river-candidates.js')));
  const source=createRiverCandidates();source.connect({geometryPreview:{geometryPolygonSets:g=>g.type==='Polygon'?[g.coordinates]:g.coordinates}});
  const countries=JSON.parse(fs.readFileSync(path.join(fixture,'countries.json'))).features;
  const output=[],sources=[],shards=new Map(),packs=new Map();
  for(const id of ['SRB','HRV','MDA']) {
    const country=countries.find(f=>f.id===id),bounds=source.riverPartitionQueryBounds(country.geometry),ids=new Set();
    for(const bound of bounds){decoder.queryLogicalFeatures({bounds:bound,category:'river'});assert.equal(message.type,'logical-features');for(const id of message.logicalFids)ids.add(id);}
    const logicalIds=[...ids].sort((a,b)=>a-b),packIds=[...new Set(logicalIds.flatMap(id=>[...(decoder.logicalPacks.get(id)||[])]))];
    for(const id of packIds)if(!packs.has(id)){const spec=decoder.packSpecs.get(id);if(!shards.has(spec.shard))shards.set(spec.shard,readAsset(manifest.shards.find(s=>Number(s.id)===spec.shard)));
      packs.set(id,decoder.readPack(gunzipSync(shards.get(spec.shard).subarray(spec.offset,spec.offset+spec.length)),id).features);}
    const fragments=packIds.flatMap(id=>packs.get(id));
    const features=structuredClone(logicalIds.map(id=>decoder.mergeLogicalFragments(fragments.filter(f=>f.properties.__logicalFid===id))));
    assert.ok(features.every(Boolean));
    const donors=[{countryId:id,geometry:country.geometry,geometryRevision:1}];
    const components=(country.geometry.type==='Polygon'?[country.geometry.coordinates]:country.geometry.coordinates).map((coordinates,index)=>({key:`component:${id}:${index}`,countryId:id,polygonIndex:index,sourcePolygonIndex:index,componentKey:`${id}:${index}`,geometry:{type:'Polygon',coordinates}}));
    const request={donors,riverFeatures:features,hydroRevision:''};
    for(const mode of ['raw','live'])output.push({name:`${id}-${mode}`,request:structuredClone(request),components:structuredClone(components),includeIdentity:true,...(mode==='live'?{liveDonorIndices:[0]}:{})});
    sources.push({id,logicalIds,packIds,bounds,riverCount:features.length,lineParts:features.reduce((n,f)=>n+(f.geometry.type==='LineString'?1:f.geometry.coordinates.length),0)});
  }
  return {cases:output,provenance:{webCommit:provenance.webCommit,datasetCommit:provenance.currentDatasetCommit,version,manifestSha256:sha256(bytes),indexSha256:manifest.index.sha256,detailMetadata:detail,sources,assets:[...new Map(accessed.map(a=>[a.url,a])).values()]}};
}
export function assertBehavior(observations,cases) {
  const rows=new Map(observations.map(r=>[r.name,stable(r)]));
  for(const c of cases) {const r=rows.get(c.name);assert.equal(r.status,'Completed',c.name);assert.equal(r.inputUnchanged,true,c.name);assert.equal(r.composeInputUnchanged,true,c.name);if(c.expectedCount!=null)assert.equal(r.result.candidates.length,c.expectedCount,c.name);}
  if(rows.has('crossing-pair')) {
    assert.deepEqual(rows.get('crossing-pair').result,rows.get('crossing-pair-reversed').result);
    const branch=rows.get('branch-with-dangling').result;assert.ok(branch.diagnostics.prunedRiverEdges>0);assert.ok(branch.candidates.every(c=>!c.sourceRiverIds.includes('dangling')));
    const invalid=rows.get('donor-wide-invalidation');assert.equal(invalid.result.donorResults[0].status,'invalid');assert.equal(invalid.result.diagnostics.tracedFaceCount,4);assert.deepEqual(invalid.composed.invalidDonorIds,['invalid']);assert.deepEqual(invalid.composed.items.map(i=>i.countryId),['unsplit']);
    const multi=rows.get('multipolygon-retains-original').composed;assert.equal(multi.items.length,3);assert.deepEqual(multi.items.map(c=>c.sourcePolygonIndex),[7,7,9]);assert.equal(multi.items[2].partitionKind,'original');assert.ok(!('sourceRiverIds' in multi.items[2]));
  }
  for(const [id,count] of [['SRB',17],['HRV',12],['MDA',18]])if(rows.has(id+'-raw')){
    assert.equal(rows.get(id+'-raw').result.candidates.length,count,id);assert.equal(rows.get(id+'-live').result.candidates.length,count,id);
    assert.notDeepEqual(rows.get(id+'-raw').result.candidates.map(c=>c.key),rows.get(id+'-live').result.candidates.map(c=>c.key));
  }
}
