import test from 'node:test';
import assert from 'node:assert/strict';
const mod=await import('./source-history-suite.mjs').catch(error=>{if(error.code!=='ERR_MODULE_NOT_FOUND')throw error;return {};});
const options={commit:'a'.repeat(40),runId:'unit-source-history-only'};
test('independent source-history suite pins source closure, synthetic inputs, runtime and limits',async()=>{
 assert.equal(typeof mod.createSourceHistorySuite,'function','New separate source-history suite required');
 const suite=await mod.createSourceHistorySuite(options);assert.equal(mod.verifySourceHistorySuite(suite),suite);
 assert.equal(suite.version,1);assert.equal(suite.manifest.sources.length,106);assert.equal(suite.identity.commit,options.commit);
 assert.equal(suite.identity.observationLimits.rawParity,false);assert.ok(suite.cases.length>=12);
 const runtime=Function('return '+suite.runtime.source)();assert.equal(typeof runtime.runSourceHistoryCase,'function');assert.equal(typeof runtime.selectionRuntime.createSelectionRuntime,'function');
});
test('strict source-history verifier rejects altered transport, candidates, canonical, runtime and input identity',async()=>{
 assert.equal(typeof mod.verifySourceHistoryReport,'function','Strict source-history report verifier required');
 const {loadSourceHistoryNodeSources}=await import('./source-history-sources.mjs'),{runSourceHistoryCase}=await import('./source-history-runtime.mjs');
 const suite=await mod.createSourceHistorySuite(options),loaded=await loadSourceHistoryNodeSources();
 try{
 const runtime=Function('return '+suite.runtime.source)();loaded.selectionRuntime=runtime.selectionRuntime;
 const cases=[];for(const definition of suite.cases)cases.push(await runSourceHistoryCase(loaded,definition));
 const report={schema:'pando-m974-actual-chromium-source-history',version:1,state:'complete',identity:suite.identity,sourceHashes:Object.fromEntries(suite.manifest.sources.map(r=>[r.path,r.sha256])),runtime:{playwright:mod.runtimePin.playwright,browserVersion:mod.runtimePin.chromium,chromiumRevision:mod.runtimePin.revision,cdp:{product:'HeadlessChrome/'+mod.runtimePin.chromium,jsVersion:mod.runtimePin.v8},userAgent:'unit-verifier-not-browser'},cases,observationLimits:mod.sourceHistoryObservationLimits};
 assert.equal(mod.verifySourceHistoryReport(suite,report),report);
 const acceptedMutations=[];for(const [name,mutate]of [
 ['missing response',r=>r.cases[0].transport.splice(r.cases[0].transport.findIndex(e=>e.direction==='response'),1)],
 ['wrong sequence',r=>r.cases[0].transport[0].sequence=99],
 ['wrong source order',r=>r.cases[0].transport.find(e=>e.message?.type==='rebase').message.editSources.patches.reverse()],
 ['wrong winner',r=>r.cases[0].stages.final.winner='gb'],
 ['canonical lie',r=>r.cases[0].stages.restored.canonical+=' '],
 ['no stop',r=>r.cases.find(c=>c.input.scenario==='boundary-held-result-stop').transport=r.cases.find(c=>c.input.scenario==='boundary-held-result-stop').transport.filter(e=>e.direction!=='terminate')],
 ['missing case',r=>r.cases.pop()],['wrong run',r=>r.identity.runId='wrong'],['wrong V8',r=>r.runtime.cdp.jsVersion='fake'],['wrong source hash',r=>r.sourceHashes[Object.keys(r.sourceHashes)[0]]='0'.repeat(64)],['raw parity true',r=>r.observationLimits.rawParity=true],
 ['cache falsely submitted',r=>r.cases.find(c=>c.case==='annex-components-cache-hit').stages.operation.cacheHit=false],
 ['pending state false',r=>r.cases.find(c=>c.case==='component-request-pending-cancel').stages.operation.selection.workerRequests=0],
 ['wrong split preview',r=>r.cases.find(c=>c.case==='root-line-cut-preview').stages.operation.previewReady=false],
 ['failed error status',r=>r.cases.find(c=>c.case==='settled-boundary-error-retains-history').stages.operation.preparation.status='ready'],
 ['unpaired masquerade',r=>r.cases.find(c=>c.case==='generic-unsynced').input.nativeCase=null],
 ['post-settlement observation deleted',r=>r.cases.find(c=>c.case==='settled-success-observes-late-deletion').transport.find(e=>e.phase==='releaseResult'&&e.message?.type==='edit-sync').message.removedKeys=[]],
 ['canceled ticket mislabeled',r=>r.cases.find(c=>c.case==='cancelled-ticket-skips-post-sync').stages.resultSettled.clientStatus='resolved'],
 ['controlled stop disguised',r=>r.cases.find(c=>c.case==='root-line-cut-after-stop').stages.controlledStop.controlledStop.injected=false],
 ['restart detached from transport',r=>r.cases.find(c=>c.case==='annex-preview-after-stop').stages.operation.restartEvidence.worker=99],
 ['fixture geometry changed',r=>{const s=r.cases[0].stages.warm,c=JSON.parse(s.canonical);c.entities[0].geometry.coordinates[0][0][0][0]+=1;s.canonical=JSON.stringify(c);}],
 ['final query omitted and resequenced',r=>{const c=r.cases[0];c.requests=c.requests.filter(q=>q.phase!=='final');c.transport=c.transport.filter(e=>e.phase!=='final').map((e,i)=>({...e,sequence:i}));}],
 ['removed stage never deleted ga',r=>{const c=r.cases[0];c.stages.removed.canonical=c.stages.warm.canonical;c.stages.removed.sourceRows=[...c.stages.warm.sourceRows];}],
 ['ready delivery omitted and resequenced',r=>{const c=r.cases[0];c.transport=c.transport.filter(e=>!(e.direction==='delivered'&&e.message?.type==='ready')).map((e,i)=>({...e,sequence:i}));}],
 ['client settlement omitted and resequenced',r=>{const c=r.cases[0];c.transport=c.transport.filter(e=>!(e.direction==='client-resolved'&&e.phase==='final')).map((e,i)=>({...e,sequence:i}));}],
 ['incoming ready omitted and resequenced',r=>{const c=r.cases[0];c.transport=c.transport.filter(e=>!(e.direction==='response'&&e.message?.type==='ready')).map((e,i)=>({...e,sequence:i}));}],
 ['rebase root geometry forged',r=>r.cases[0].transport.find(e=>e.direction==='request'&&e.message?.type==='rebase').message.editSources.patches[0].geometry.coordinates[0][0][0][0]+=9],
 ['rebase generic geometry forged',r=>r.cases[0].transport.find(e=>e.direction==='request'&&e.message?.type==='rebase').message.editSources.patches.find(p=>p.key==='generic:ga').geometry.coordinates[0][0][0]+=9],
 ['restore patch geometry forged',r=>r.cases.find(c=>c.case==='ready-boundary-worker-observes-deletion').transport.find(e=>e.direction==='request'&&e.phase==='final'&&e.message?.type==='edit-sync').message.patches.find(p=>p.key==='generic:ga').geometry.coordinates[0][0][0]+=9],
 ['boundary-sync forged geometry',r=>{const c=r.cases[0],f=JSON.parse(c.stages.warm.canonical).entities[0];f.boundaryLocked=false;f.geometry.coordinates[0][0][0][0]+=9;c.transport.splice(4,0,{phase:'warm',direction:'request',worker:1,message:{type:'boundary-sync',features:[f],removedIds:[]}});c.transport.forEach((e,i)=>e.sequence=i);}],
 ['boundary-sync forged metadata',r=>{const c=r.cases[0],f=JSON.parse(c.stages.warm.canonical).entities[0];f.boundaryLocked=false;f.properties.parentId='forged';c.transport.splice(4,0,{phase:'warm',direction:'request',worker:1,message:{type:'boundary-sync',features:[f],removedIds:[]}});c.transport.forEach((e,i)=>e.sequence=i);}],
 ['new source missing geometry',r=>{delete r.cases.find(c=>c.case==='ready-boundary-worker-observes-deletion').transport.find(e=>e.direction==='request'&&e.phase==='final'&&e.message?.type==='edit-sync').message.patches.find(p=>p.key==='generic:ga').geometry;}],
 ['transport source revision forged',r=>{r.cases[0].transport.find(e=>e.direction==='request'&&e.message?.type==='rebase').message.editSources.sourceRevision=999;}],
 ['request query differs from observed stage',r=>{const c=r.cases[0];c.requests.find(q=>q.phase==='final').message.payload.coordinate=[999,999];c.transport.find(e=>e.phase==='final'&&e.direction==='request'&&e.message.type==='execute').message.payload.coordinate=[999,999];}],
 ['unknown protocol event',r=>{const c=r.cases[0];c.transport.splice(1,0,{phase:'warm',direction:'request',worker:1,message:{type:'fabricated-operation'}});c.transport.forEach((e,i)=>e.sequence=i);}],
 ['resolved vertex coordinate forged',r=>{const s=r.cases[0].stages.final;s.result.coordinate=[999,999];s.indicator.coordinate=[999,999];}],
 ['resolved segment endpoints forged',r=>{const s=r.cases.find(c=>c.case==='preview-stop-ready-snap-hit').stages.final;s.result.segmentEndpoints=[[998,998],[999,999]];s.indicator.segmentEndpoints=[[998,998],[999,999]];}],
 ['deselection loses request owner',r=>r.cases.find(c=>c.case==='pending-selection-deselect-clear').stages.operation.afterMicrotasks.workerRequests=0],
 ]){const bad=structuredClone(report);mutate(bad);try{mod.verifySourceHistoryReport(suite,bad);acceptedMutations.push(name);}catch{}}
 assert.deepEqual(acceptedMutations,[],'Every semantic mutation must fail, including resequenced omissions');
 }finally{loaded.cleanup();}
});
