import {gzipSync} from 'node:zlib';
export function createPage(payload) {
  const encoded=gzipSync(JSON.stringify(payload),{level:9}).toString('base64');
  return `<!doctype html><meta charset="utf-8"><title>Direct native / official Chromium river oracle</title><pre id="summary">Running 42 cases</pre><script type="module">
const summary=document.querySelector('#summary');
const digest=async text=>Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(text)))).map(x=>x.toString(16).padStart(2,'0')).join('');
const canonical=value=>Array.isArray(value)?value.map(canonical):value&&typeof value==='object'?Object.fromEntries(Object.keys(value).sort().map(k=>[k,canonical(value[k])])):value;
const hash=value=>digest(JSON.stringify(canonical(value)));
const rawHash=value=>{const copy=structuredClone(value);delete copy.result.diagnostics.computeMs;return hash(copy);};
try {
  const bytes=Uint8Array.from(atob('${encoded}'),x=>x.charCodeAt(0));
  const payload=JSON.parse(await new Response(new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'))).text());
  const sourceHashes={},boundaryHashes={};
  for(const [name,item] of Object.entries(payload.modules)){sourceHashes[name]=await digest(item.source);if(sourceHashes[name]!==item.sha256)throw new Error('SOURCE_HASH_MISMATCH '+name);}
  for(const [name,item] of Object.entries(payload.boundaries)){boundaryHashes[name]=await digest(item.source);if(boundaryHashes[name]!==item.sha256)throw new Error('BOUNDARY_HASH_MISMATCH '+name);}
  if(await digest(payload.world.source)!==payload.world.sha256)throw new Error('FULL_WORLD_HASH_MISMATCH');
  if(payload.cases.length!==42||new Set(payload.cases.map(c=>c.name)).size!==42)throw new Error('42 unique cases required');
  const urls={},namespaces={},rewrites={};
  const blob=source=>URL.createObjectURL(new Blob([source],{type:'text/javascript'}));
  async function load(name){
    if(namespaces[name])return namespaces[name];let source=payload.modules[name].source;
    const dependencies=[...source.matchAll(/(?:from\\s*|import\\s*)['"]\\.\\/([^'"]+)['"]/g)].map(m=>m[1]);
    for(const dependency of new Set(dependencies)){await load(dependency);let count=0;for(const quote of ["'",'"']){const before=quote+'./'+dependency+quote;count+=source.split(before).length-1;source=source.split(before).join(JSON.stringify(urls[dependency]));}rewrites[name+'/'+dependency]=count;}
    urls[name]=blob(source);namespaces[name]=await import(urls[name]);return namespaces[name];
  }
  await load('polygon-clipping-0.15.7.js');await load('polygon-geometry.js');
  const module=await load('river-territory-partition.js');
  const bridge=(0,eval)(payload.boundaries['bridge.js'].source),present=(0,eval)(payload.boundaries['presentation.js'].source),trace=(0,eval)(payload.boundaries['workspace-observation.js'].source);
  const observations=[],presentations=[],traces=[],caseSummary=[];
  for(const row of payload.cases){
    const raw={name:row.name,...bridge(structuredClone(row),module,globalThis.polygonClipping)};
    const shown={name:row.name,...present(row,raw,module,globalThis.PandoLabPolygonGeometry.normalizePolygonGeometry)};
    const workspace={name:row.name,workspaces:trace(module,row.request)};
    observations.push(raw);presentations.push(shown);traces.push(workspace);
    caseSummary.push({name:row.name,inputSha256:await hash(row),rawSha256:await rawHash(raw),presentationSha256:await hash(shown),workspaceSha256:await hash(workspace),cells:raw.result.candidates.length});
  }
  if(await digest(payload.branchDiagnostics.source)!==payload.branchDiagnostics.sha256)throw new Error('TRACE_SOURCE_HASH_MISMATCH');
  const instrumentedSource=payload.branchDiagnostics.source.replace("'./planar-graph-faces.js'",JSON.stringify(urls['planar-graph-faces.js']));
  const instrumented=await import(blob(instrumentedSource)),branchObservations=[];
  globalThis.__riverTraceTargets=payload.branchDiagnostics.targets;
  for(const id of ['SRB','HRV','MDA']){
    const row=payload.cases.find(r=>r.name===id+'-raw');globalThis.__riverBranchTrace=[];
    const output={name:row.name,...bridge(structuredClone(row),instrumented,globalThis.polygonClipping)};
    if(await rawHash(output)!==await rawHash(observations.find(r=>r.name===row.name)))throw new Error('Instrumentation changed complete raw output '+id);
    if(globalThis.__riverBranchTrace.length!==1||Math.abs(globalThis.__riverBranchTrace[0].denominator)>=1e-6)throw new Error('Required near-collinear branch not observed '+id);
    branchObservations.push({name:row.name,instrumentedSourceSha256:payload.branchDiagnostics.sha256,outputExact:true,segments:structuredClone(globalThis.__riverBranchTrace)});
  }
  const below=traces.find(r=>r.name==='cosine-floor-below').workspaces[0],above=traces.find(r=>r.name==='cosine-floor-above').workspaces[0];
  if(!(below.cosine>1e-6&&above.cosine<1e-6&&below.cosineScale>1e-6&&above.cosineScale===1e-6))throw new Error('Cosine-floor cases must straddle actual clamp');
  const countryModule=await load('map-edit-country-commands.js');
  const annex=await (0,eval)(payload.boundaries['annex.js'].source)(payload,observations,presentations,countryModule.createCountryCommandCalculator(globalThis.polygonClipping),globalThis.polygonClipping,digest,canonical);
  const metamorphic=[];for(const row of payload.cases.filter(r=>r.stressProvenance?.base)){const base=observations.find(r=>r.name===row.stressProvenance.base),changed=observations.find(r=>r.name===row.name);metamorphic.push({name:row.name,base:base.name,completeRawEqualExceptNameAndComputeMs:await rawHash({...changed,name:base.name})===await rawHash(base)});}
  globalThis.__riverReport={schema:'river-browser-observations-v2',identity:payload.identity,runtime:{userAgent:navigator.userAgent},sourceHashes,boundaryHashes,staticImportRewrites:rewrites,observations,presentations,traces,branchObservations,annex,metamorphic};
  globalThis.__riverSummary={state:'complete',total:42,original:24,stress:18,normalizerExecutedInBrowser:true,annexRole:payload.annex.role,caseSummary,annex:await Promise.all(annex.map(async r=>({name:r.name,sha256:await hash(r),selectedKeys:r.selectedCells.map(c=>c.key),autoIncludedSlivers:r.result.autoIncludedSlivers})))};
}catch(error){globalThis.__riverSummary={state:'failed',error:String(error),stack:error.stack};}
summary.textContent=JSON.stringify(globalThis.__riverSummary,null,2);
</script>`;
}
