import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {gzipSync} from 'node:zlib';
import {createRuntime,observe,referenceEffects} from './web-lifecycle.mjs';
import {selectionEntrypoints,createSelectionRuntime,observeSelection,seedSelectionFeatures,square,selectionCutView} from './web-selection.mjs';
import {loadSplitModules,splitCaseIds,splitCaseDefinition,seedSplitFeatures,createSplitRuntime,settleSplit,runSplitLifecycleCase} from './web-split.mjs';
import {loadSplitCandidateModules} from './web-split-candidate.mjs';
import {runSplitBrowserOracle} from './web-split-browser.mjs';
const hash=source=>createHash('sha256').update(source).digest('hex');
export function splitBrowserRuntime(){
 const lifecycleSource=readFileSync(new URL('./web-lifecycle.mjs',import.meta.url),'utf8');
 const seed=lifecycleSource.match(/function seedFeatures\(api\) \{[\s\S]*?\n\}\n\nfunction createRuntime/);
 assert.ok(seed,'Pinned harness seed function boundary changed');
 const seedFunction=seed[0].slice(0,-'\n\nfunction createRuntime'.length);
 const functions={square,selectionCutView,seedSelectionFeatures,createRuntime,observe,referenceEffects,createSelectionRuntime,observeSelection,seedSplitFeatures,createSplitRuntime,settleSplit,runSplitLifecycleCase};
 const declarations=Object.entries(functions).map(([name,fn])=>`const ${name}=${fn.toString()};`).join('\n');
 const source=`(({assert})=>{const clone=value=>structuredClone(value),noop=()=>{};${seedFunction}\n${declarations}\nreturn {createSplitRuntime,runSplitLifecycleCase};})`;
 return {source,sha256:hash(source)};
}
export async function splitBrowserSourceBundle({caseIds=splitCaseIds,caseDefinitions=null,candidateRoot=null,candidateChanges=null}={}){
 assert.equal(Boolean(candidateRoot),Boolean(candidateChanges),'Candidate root and explicit changes must be provided together');
 const loaded=candidateRoot?await loadSplitCandidateModules({candidateRoot,candidateChanges}):await loadSplitModules();
 try{
  const sources={};
  for(const row of loaded.manifest.sources){const source=readFileSync(resolve(loaded.selectionRoot,row.path),'utf8');sources[row.path]={source,sha256:hash(source)};}
  for(const row of loaded.supplemental?.sources||[]){const source=readFileSync(resolve(loaded.selectionRoot,row.path),'utf8');sources[row.path]={source,sha256:hash(source)};}
  const lifecycleEntrypoints=['territorial-units','territorial-entity-store','territorial-entity-repository','territorial-service','project-command-pipeline','project-state','history-service','app-project-snapshots','distribution-model','app-hydro-settings','app-layer-list','generic-feature-service','layer-presentation','project-invariants','geometry-preview','app-geometry-preview','app-territorial-drafts','app-object-presentation','map-object-categories','app-object-metadata','builtin-subunits','map-edit-worker-client','geometry-metrics'];
  const entrypoints=[...new Set([...lifecycleEntrypoints,...selectionEntrypoints,'app-country-validation','app-object-picking'])];
  for(const name of entrypoints)assert.ok(sources[`assets/js/modules/${name}.js`],name);
  return {schema:'pando-web-split-browser-source-bundle',version:1,baseBehavioralCommit:loaded.manifest.behavioralCommit,behavioralCommit:loaded.testOnlyCandidate?'test-only-candidate':loaded.sourceChain.at(-1).behavioralCommit,...(loaded.testOnlyCandidate?{testOnlyCandidate:true,baseApprovedBehavioralCommit:loaded.sourceChain.at(-1).behavioralCommit,candidateChanges:loaded.candidateChanges}:{}),sourceChain:loaded.sourceChain,supplementalSources:loaded.supplemental||null,sources,entrypoints,runtime:splitBrowserRuntime(),cases:caseDefinitions?structuredClone(caseDefinitions):caseIds.map(splitCaseDefinition)};
 }finally{loaded.cleanup();}
}
export function createSplitBrowserPage(bundle,{assetPrefix='/split-oracle/'}={}){
 assert.match(assetPrefix,/^\/[a-zA-Z0-9/_-]*\/$/);
 const encoded=gzipSync(JSON.stringify(bundle),{level:9}).toString('base64');
 return `<!doctype html><meta charset="utf-8"><title>Pinned production split lifecycle oracle</title><pre id="summary">Running split lifecycle</pre><script type="module">
const node=document.querySelector('#summary');
const poll=setInterval(()=>{node.textContent=JSON.stringify(globalThis.__splitSummary||{state:'starting'},null,2)},1000);
try {
const bytes=Uint8Array.from(atob('${encoded}'),value=>value.charCodeAt(0));
const bundle=JSON.parse(await new Response(new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'))).text());
const run=${runSplitBrowserOracle.toString()};
const report=await run({baseUrl:new URL(${JSON.stringify(assetPrefix)},location.href),bundle});
globalThis.__splitReport=report;
globalThis.__splitSummary={state:'complete',total:report.cases.length,userAgent:report.runtime.userAgent,cases:report.cases.map(row=>({case:row.case,cutValid:row.assessment.valid,candidates:row.assessment.split?.candidates?.length||0,canAddPart:row.stages.selected.selection?.canAddPart,confirm:row.stages.confirm?.outcome,referenceEffects:row.referenceEffects}))};
}catch(error){globalThis.__splitSummary={state:'failed',error:String(error),stack:error.stack};}
clearInterval(poll);node.textContent=JSON.stringify(globalThis.__splitSummary,null,2);
</script>`;
}
