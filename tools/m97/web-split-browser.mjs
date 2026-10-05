/** Browser-only runner. Asset URLs are served at their original relative paths. */
export async function runSplitBrowserOracle({baseUrl,bundle}){
 const digest=async text=>Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(text)))).map(value=>value.toString(16).padStart(2,'0')).join('');
 const canonical=value=>Array.isArray(value)?value.map(canonical):value&&typeof value==='object'?Object.fromEntries(Object.keys(value).sort().map(key=>[key,canonical(value[key])])):value;
 const assert={ok(value,message){if(!value)throw Error(message||'Expected truthy value');},equal(actual,expected,message){if(actual!==expected)throw Error(message||`Expected ${JSON.stringify(expected)}, received ${JSON.stringify(actual)}`);},deepEqual(actual,expected,message){if(JSON.stringify(canonical(actual))!==JSON.stringify(canonical(expected)))throw Error(message||'Deep equality failed');}};
 const sourceHashes={};
 for(const [path,item] of Object.entries(bundle.sources)){
  const response=await fetch(new URL(path,baseUrl));assert.ok(response.ok,'Missing pinned asset '+path);
  sourceHashes[path]=await digest(await response.text());assert.equal(sourceHashes[path],item.sha256,'Pinned asset hash mismatch '+path);
 }
 assert.equal(await digest(bundle.runtime.source),bundle.runtime.sha256,'Harness runtime hash mismatch');
 for(const vendor of ['polygon-clipping.min.js','d3.min.js'])await new Promise((resolve,reject)=>{const script=document.createElement('script');script.src=new URL('assets/js/vendor/'+vendor,baseUrl);script.onload=resolve;script.onerror=()=>reject(Error('Vendor load failed '+vendor));document.head.append(script);});
 await import(new URL('assets/js/modules/polygon-geometry.js',baseUrl));
 const modules=await Promise.all(bundle.entrypoints.map(name=>import(new URL('assets/js/modules/'+name+'.js',baseUrl))));
 const api={...Object.assign({},...modules),...globalThis.PandoLabPolygonGeometry,clipper:globalThis.polygonClipping,d3:globalThis.d3};
 assert.equal(typeof api.clipper?.difference,'function');
 const runtime=(0,eval)(bundle.runtime.source)({assert});
 const loaded={api,createWorker:()=>new Worker(new URL('assets/js/workers/map-edit-worker.js',baseUrl))};
 const cases=[];
 for(const definition of bundle.cases){const row=await runtime.runSplitLifecycleCase(loaded,structuredClone(definition));cases.push(row);globalThis.__splitSummary={state:'running',completed:cases.length,total:bundle.cases.length,last:row.case};}
 return {schema:'pando-web-split-browser-observations',version:1,baseBehavioralCommit:bundle.baseBehavioralCommit,behavioralCommit:bundle.behavioralCommit,sourceChain:bundle.sourceChain,...(bundle.testOnlyCandidate?{testOnlyCandidate:true,baseApprovedBehavioralCommit:bundle.baseApprovedBehavioralCommit,candidateChanges:bundle.candidateChanges}:{}),runtime:{userAgent:navigator.userAgent},sourceHashes,harnessRuntimeSha256:bundle.runtime.sha256,cases};
}
