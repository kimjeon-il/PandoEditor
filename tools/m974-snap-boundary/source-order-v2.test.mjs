import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {spawnSync} from 'node:child_process';
import {loadNodeSources,readPinnedSources,sha256} from './sources.mjs';
import {runtimePin} from './protocol.mjs';
const mod=await import('./source-order-v2.mjs').catch(error=>{if(error.code!=='ERR_MODULE_NOT_FOUND')throw error;return {};});
const options={commit:'a'.repeat(40),runId:'unit-source-order-only'};
const preserved=['input-manifest.json','snap-cases.json','boundary-cases.json','supplemental-v1-manifest.json','supplemental-v1-cases.json','source-manifest.json','production-sources.json.gz'];
const oldHashes=()=>Object.fromEntries(preserved.map(name=>[name,sha256(fs.readFileSync(new URL('../../tests/fixtures/web-m974/'+name,import.meta.url)))]));

test('separate source-order v2 suite pins the exact production closure and stimulus inputs without old fixture edits',async()=>{
 assert.equal(typeof mod.createSourceOrderSuite,'function','Separate source-order suite builder must exist');
 const before=oldHashes(),suite=await mod.createSourceOrderSuite(options);
 assert.equal(mod.verifySourceOrderSuite(suite),suite);
 assert.equal(suite.schema,'pando-m974-source-order-suite');assert.equal(suite.version,2);
 assert.deepEqual(suite.sources,readPinnedSources().sources);assert.equal(suite.cases.length,1);
 assert.deepEqual(suite.cases[0].features.map(f=>f.id),['a','b']);
 assert.deepEqual(suite.cases[0].features[0].geometry,suite.cases[0].features[1].geometry);
 assert.deepEqual(suite.cases[0].patchIds,['a']);assert.equal(suite.cases[0].observationOnly,true);
 assert.deepEqual(oldHashes(),before);
 for(const change of [s=>{s.cases[0].features.reverse();},s=>{s.cases[0].query.margin=1;},s=>{delete s.sources['assets/js/modules/map-edit-worker-client.js'];},s=>{s.runtime.source+=' ';},s=>{s.identity.runId='different';},s=>{s.runtimePin.chromium='other';}]){
  const invalid=structuredClone(suite);change(invalid);assert.throws(()=>mod.verifySourceOrderSuite(invalid));
 }
 await assert.rejects(()=>mod.createSourceOrderSuite({commit:'short',runId:'x'}));
 await assert.rejects(()=>mod.createSourceOrderSuite({commit:options.commit}));
});

test('portable runtime performs actual immediate source remove/restore syncPatch with no intervening query',async()=>{
 assert.equal(typeof mod.runSourceOrderCase,'function','Actual client/Worker diagnostic must exist');
 const suite=await mod.createSourceOrderSuite(options),loaded=await loadNodeSources();
 try{
  const portable=(0,eval)(suite.runtime.source),row=await portable.runSourceOrderCase(loaded,suite.cases[0]);
  assert.deepEqual(row.stages.warm.sourceArrayOrder,['a','b']);assert.deepEqual(row.stages.removed.sourceArrayOrder,['b']);
  assert.deepEqual(row.stages.restored.sourceArrayOrder,['a','b']);assert.deepEqual(row.stages.afterRestore.sourceArrayOrder,['a','b']);
  assert.equal(row.stages.removed.syncPatch,true);assert.equal(row.stages.restored.syncPatch,true);
  assert.deepEqual(row.transport.filter(e=>e.direction==='request').map(e=>[e.phase,e.message.type]),[
   ['warm','rebase'],['warm','execute'],['removed','sync-patch'],['restored','sync-patch'],['afterRestore','execute']]);
  assert.deepEqual(row.transport.map(e=>e.sequence),[0,1,2,3,4,5,6,7]);
  for(const phase of ['warm','afterRestore']){
   const stage=row.stages[phase];assert.deepEqual(stage.candidates,stage.response.result.candidates);
   assert.deepEqual(stage.candidateOwnerOrder,stage.candidates.map(c=>c.ownerIds));
   assert.deepEqual(stage.firstCandidateOwnerIds,stage.candidates[0].ownerIds);
  }
  // Explicit Node-only reproduction, never a generated browser expectation.
  assert.deepEqual(row.stages.warm.firstCandidateOwnerIds,['a']);
  assert.deepEqual(row.stages.afterRestore.firstCandidateOwnerIds,['b']);
  assert.equal(row.observationLimits.webApplicationHistoryUndo,false);
  assert.equal(row.observationLimits.nativeControllerDeleteUndoSeparate,true);
 }finally{loaded.cleanup();}
});

test('report protocol rejects missing or changed envelopes, order, revisions, receipt data, and stimulus claims',async()=>{
 assert.equal(typeof mod.verifySourceOrderReport,'function','Strict source-order report verifier must exist');
 const suite=await mod.createSourceOrderSuite(options),loaded=await loadNodeSources();
 try{
  const row=await mod.runSourceOrderCase(loaded,suite.cases[0]);
  // A schema test scaffold only. It is never written as an actual Chromium artifact.
  const report={schema:'pando-m974-actual-chromium-source-order',version:2,state:'complete',identity:suite.identity,
   sourceHashes:Object.fromEntries(suite.manifest.sources.map(r=>[r.path,r.sha256])),
   runtime:{userAgent:'HeadlessChrome/'+runtimePin.chromium,playwright:runtimePin.playwright,browserVersion:runtimePin.chromium,chromiumRevision:runtimePin.revision,cdp:{product:'HeadlessChrome/'+runtimePin.chromium,jsVersion:runtimePin.v8}},
   cases:[row],observationLimits:structuredClone(mod.sourceOrderObservationLimits)};
  assert.equal(mod.verifySourceOrderReport(suite,report),report);
  const mutations=[
   ['missing case',r=>{r.cases=[];}],['extra case',r=>{r.cases.push(structuredClone(r.cases[0]));}],
   ['fake browser pin',r=>{r.runtime.browserVersion='0';}],['missing source hash',r=>{delete r.sourceHashes['assets/js/modules/map-edit-worker-client.js'];}],
   ['rewritten identity',r=>{r.identity.commit='b'.repeat(40);} ],
   ['no requests',r=>{r.cases[0].transport=r.cases[0].transport.filter(e=>e.direction!=='request');}],
   ['missing ready envelope',r=>{r.cases[0].transport.splice(1,1);}],
   ['missing result envelope',r=>{r.cases[0].transport.pop();}],
   ['extra query',r=>{r.cases[0].transport.splice(5,0,structuredClone(r.cases[0].transport[2]));}],
   ['patch order reversed',r=>{const t=r.cases[0].transport;[t[4],t[5]]=[t[5],t[4]];}],
   ['missing sync patch geometry revision',r=>{delete r.cases[0].transport[4].message.geometryRevision;}],
   ['invented removed ids',r=>{r.cases[0].transport[4].message.removedIds=[];}],
   ['fake source revision',r=>{r.cases[0].transport[5].message.editSources.sourceRevision=2;}],
   ['lost restored feature properties',r=>{delete r.cases[0].transport[5].message.features[0].properties;}],
   ['truncated outgoing payload',r=>{delete r.cases[0].transport[6].message.payload.sourceKey;}],
   ['duplicate request id',r=>{r.cases[0].transport[6].message.requestId=1;}],
   ['stale incoming revision',r=>{r.cases[0].transport[7].message.targetRevision=0;}],
   ['incoming result rejection',r=>{r.cases[0].transport[7].message.ok=false;}],
   ['receipt differs from response',r=>{r.cases[0].stages.afterRestore.response.result.candidates.reverse();}],
   ['candidate summary sorting',r=>{r.cases[0].stages.afterRestore.candidates.reverse();}],
   ['owner summary sorting',r=>{r.cases[0].stages.afterRestore.candidateOwnerOrder.reverse();}],
   ['first owner overwritten',r=>{r.cases[0].stages.afterRestore.firstCandidateOwnerIds=['invented'];}],
   ['source array normalized to worker order',r=>{r.cases[0].stages.afterRestore.sourceArrayOrder=['b','a'];}],
   ['false syncPatch outcome',r=>{r.cases[0].stages.removed.syncPatch=false;}],
   ['missing restore stage',r=>{delete r.cases[0].stages.restored;}],
   ['app Undo claim',r=>{r.cases[0].observationLimits.webApplicationHistoryUndo=true;}],
   ['native equivalent stimulus claim',r=>{r.observationLimits.nativeControllerDeleteUndoSeparate=false;}],
   ['unrecognized field',r=>{r.cases[0].transport[4].message.extra=true;}],
  ];
  for(const [label,mutate]of mutations){const invalid=structuredClone(report);mutate(invalid);assert.throws(()=>mod.verifySourceOrderReport(suite,invalid),undefined,label);}
  const nodeReport={...report,schema:'pando-m974-node-source-order-discovery',runtime:{node:process.version},authoritativeBrowserObservation:false};
  assert.throws(()=>mod.verifySourceOrderReport(suite,nodeReport));
 }finally{loaded.cleanup();}
});

test('browser page contains only portable runtime and CLI refuses non-CI launch before writing files',async()=>{
 const browser=await import('./source-order-v2-browser-runner.mjs').catch(error=>{if(error.code!=='ERR_MODULE_NOT_FOUND')throw error;return {};});
 assert.equal(typeof browser.createSourceOrderPage,'function','Dedicated capture page must exist');
 const html=browser.createSourceOrderPage(await mod.createSourceOrderSuite(options));
 assert.ok(html.includes('DecompressionStream'));assert.ok(html.includes('__m974SourceOrderReport'));assert.ok(!html.includes('node:'));
 const env={...process.env};delete env.GITHUB_ACTIONS;
 const result=spawnSync(process.execPath,[new URL('./source-order-v2-browser-runner.mjs',import.meta.url).pathname,'/tmp/source-order-should-not-launch'],{env,encoding:'utf8'});
 assert.notEqual(result.status,0);assert.match(result.stderr,/authorized exact-commit CI/);
 assert.equal(fs.existsSync('/tmp/source-order-should-not-launch'),false);
});

test('CI preflight refuses missing run identity, malformed commit, and a different checked-out commit',async()=>{
 const {verifySourceOrderCI}=await import('./source-order-v2-browser-runner.mjs');
 const env={GITHUB_ACTIONS:'true',GITHUB_SHA:'a'.repeat(40),GITHUB_RUN_ID:'123456'};
 assert.doesNotThrow(()=>verifySourceOrderCI(env,env.GITHUB_SHA));
 for(const [change,head]of [[{GITHUB_ACTIONS:'false'},env.GITHUB_SHA],[{GITHUB_RUN_ID:''},env.GITHUB_SHA],[{GITHUB_RUN_ID:'discovery'},env.GITHUB_SHA],[{GITHUB_SHA:'short'},env.GITHUB_SHA],[{},'b'.repeat(40)]])assert.throws(()=>verifySourceOrderCI({...env,...change},head));
});

test('bounded browser export preserves Unicode and detects corrupted chunks without a persisted final artifact',async()=>{
 const {exportSourceOrderReport}=await import('./source-order-v2-browser-runner.mjs'),os=await import('node:os'),path=await import('node:path');
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'m974-source-order-transfer-'));
 try{
  const report={state:'complete',runtime:{},unicode:'🧭'.repeat(200000)},page={evaluate:async(fn,arg)=>fn(arg)};
  globalThis.__m974SourceOrderReport=structuredClone(report);
  const destination=path.join(dir,'report.json'),receipt=await exportSourceOrderReport(page,destination,{testOnly:true});
  assert.ok(receipt.chunks>1);assert.equal(receipt.sha256,sha256(fs.readFileSync(destination)));
  assert.deepEqual(JSON.parse(fs.readFileSync(destination)),{...report,runtime:{testOnly:true}});
  assert.equal(globalThis.__m974SourceOrderExport,undefined);
  const badPage={evaluate:async(fn,arg)=>{const result=await fn(arg);if(arg&&Object.hasOwn(arg,'offset'))result.text='x'+result.text.slice(1);return result;}};
  globalThis.__m974SourceOrderReport=structuredClone(report);
  await assert.rejects(()=>exportSourceOrderReport(badPage,path.join(dir,'bad.json'),{}),/Complete report digest/);
  assert.equal(fs.existsSync(path.join(dir,'bad.json')),false);assert.equal(globalThis.__m974SourceOrderExport,undefined);
  globalThis.__m974SourceOrderReport={state:'pending'};
  await assert.rejects(()=>exportSourceOrderReport(page,path.join(dir,'pending.json'),{}),/Missing complete actual source-order observation/);
  assert.equal(fs.existsSync(path.join(dir,'pending.json')),false);
 }finally{delete globalThis.__m974SourceOrderReport;delete globalThis.__m974SourceOrderExport;fs.rmSync(dir,{recursive:true,force:true});}
});

test('Node discovery retains separate identity and can never pass as an actual Chromium report',async()=>{
 const {runSourceOrderDiscovery}=await import('./source-order-v2-node-runner.mjs'),report=await runSourceOrderDiscovery(options),suite=await mod.createSourceOrderSuite(options);
 assert.equal(report.schema,'pando-m974-node-source-order-discovery');assert.equal(report.authoritativeBrowserObservation,false);
 assert.equal(report.runtime.node,process.version);assert.equal(report.cases.length,1);assert.deepEqual(report.identity,suite.identity);
 assert.throws(()=>mod.verifySourceOrderReport(suite,report));
});
