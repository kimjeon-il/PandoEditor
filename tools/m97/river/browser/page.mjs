import fs from 'node:fs';
import path from 'node:path';
import {gzipSync} from 'node:zlib';
import {root,riverRoot,sha256,outputHash,loadOracle,syntheticCases,realCases,assertBehavior} from '../oracle.mjs';
export async function createPayload(manifestPath) {
  const oracle=await loadOracle(),real=await realCases(manifestPath);
  const cases=[...syntheticCases(),...real.cases];
  if(cases.length!==24)throw new Error('Expected exactly 18 synthetic + 6 raw/live full-source cases');
  const observations=cases.map(oracle.observe);assertBehavior(observations,cases);
  const modules={};for(const name of ['river-territory-partition.js','planar-graph-faces.js','polygon-clipping-0.15.7.js']){
    const source=fs.readFileSync(path.join(name.startsWith('polygon-')?path.join(root,'assets/geometry'):path.join(riverRoot,'original'),name),'utf8');modules[name]={source,sha256:sha256(source)};
  }
  return {cases,modules,bridge:fs.readFileSync(path.join(riverRoot,'bridge.js'),'utf8'),
    expected:Object.fromEntries(observations.map(r=>[r.name,{sha256:outputHash(r),cells:r.result.candidates.length,keys:r.result.candidates.map(c=>c.key)}])),
    node:{version:process.version,v8:process.versions.v8},provenance:real.provenance};
}
export function createPage(payload) {
  const encoded=gzipSync(JSON.stringify(payload),{level:9}).toString('base64');
  return `<!doctype html><meta charset="utf-8"><title>Pinned river Chromium oracle</title><pre id="summary">Running 24 cases</pre><script type="module">
const summary=document.querySelector('#summary');
const digest=async text=>Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(text)))).map(x=>x.toString(16).padStart(2,'0')).join('');
const canonical=value=>Array.isArray(value)?value.map(canonical):value&&typeof value==='object'?Object.fromEntries(Object.keys(value).sort().map(k=>[k,canonical(value[k])])):value;
try {
  const bytes=Uint8Array.from(atob('${encoded}'),x=>x.charCodeAt(0));
  const payload=JSON.parse(await new Response(new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'))).text());
  const sourceHashes={};
  for(const [name,entry] of Object.entries(payload.modules)){sourceHashes[name]=await digest(entry.source);if(sourceHashes[name]!==entry.sha256)throw new Error('SOURCE_HASH_MISMATCH: '+name);}
  const blob=source=>URL.createObjectURL(new Blob([source],{type:'text/javascript'}));
  await import(blob(payload.modules['polygon-clipping-0.15.7.js'].source));
  const planar=blob(payload.modules['planar-graph-faces.js'].source),source=payload.modules['river-territory-partition.js'].source,needle="'./planar-graph-faces.js'";
  if(source.split(needle).length!==2)throw new Error('Original relative module import count changed');
  const module=await import(blob(source.replace(needle,JSON.stringify(planar))));
  const bridge=(0,eval)(payload.bridge),observations=[],fullOutputs=[];
  for(const row of payload.cases){
    const result={name:row.name,...bridge(structuredClone(row),module,globalThis.polygonClipping)};
    const computeMs=result.result.diagnostics.computeMs;delete result.result.diagnostics.computeMs;
    const hash=await digest(JSON.stringify(canonical(result))),expected=payload.expected[row.name];
    observations.push({name:row.name,exact:hash===expected.sha256,sha256:hash,expectedSha256:expected.sha256,cells:result.result.candidates.length,computeMs});fullOutputs.push(result);
  }
  globalThis.__riverFullOutputs=fullOutputs;
  globalThis.__riverSummary={state:'complete',userAgent:navigator.userAgent,node:payload.node,sourceHashes,cosineArgument:0.7719766168394622,cosine:Math.cos(0.7719766168394622),exactMatches:observations.filter(r=>r.exact).length,total:observations.length,synthetic:18,actualSource:6,provenance:payload.provenance,observations};
}catch(error){globalThis.__riverSummary={state:'failed',error:String(error),stack:error.stack};}
summary.textContent=JSON.stringify(globalThis.__riverSummary,null,2);
</script>`;
}
