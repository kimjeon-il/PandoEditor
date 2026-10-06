import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
const commit='1'.repeat(40);
test('additive suite binds unchanged source closure, exact two inputs and full harness',async()=>{
 assert.ok(fs.existsSync(new URL('./suite.mjs',import.meta.url)),'Authenticated replacement suite is missing');
 const {createSessionReplacementSuite,verifySessionReplacementSuite,sessionReplacementRuntimeSource}=await import('./suite.mjs');
 const {readSourceHistorySources}=await import('../m974-snap-boundary/source-history-sources.mjs');
 const suite=createSessionReplacementSuite({commit,runId:'123'}),original=readSourceHistorySources();verifySessionReplacementSuite(suite);assert.deepEqual(suite.sources,original.sources);assert.deepEqual(suite.manifest,original.manifest);assert.equal(suite.cases.length,2);assert.equal(Object.keys(suite.sources).length,106);
 for(const name of ['../../.github/workflows/m97-editing-parity.yml','gate.mjs','compare.mjs','../../tests/m977_boundary_session_replacement_probe.cpp','../../tests/m974_boundary_probe.cpp','../m974-native-boundary.mjs','../m977-boundary-adoption/compare.mjs','../m97/split-differential.mjs','../m974-snap-boundary/protocol.mjs','../m974-snap-boundary/worker-host.mjs'])assert.match(suite.harnessFiles[name],/^[a-f0-9]{64}$/,name);
 assert.equal(typeof (0,eval)(sessionReplacementRuntimeSource()).runBoundarySessionReplacementCase,'function');
 for(const mutate of [s=>s.cases.pop(),s=>s.cases.reverse(),s=>s.cases[0].move.coordinate[1]=1.1,s=>s.sources[s.manifest.sources[0].path]+=' ',s=>s.manifest.behavioralCommit='0'.repeat(40),s=>delete s.harnessFiles['gate.mjs'],s=>s.harnessFiles['../m974-snap-boundary/protocol.mjs']='0'.repeat(64),s=>s.sessionRuntime.sha256='0'.repeat(64),s=>s.identity.inputSha256='0'.repeat(64),s=>s.legacyInputIdentity.sha256='0'.repeat(64)]){const bad=structuredClone(suite);mutate(bad);assert.throws(()=>verifySessionReplacementSuite(bad));}
});

test('raw byte binding rejects altered receipt, source identity, stages and runtime',async()=>{
 const {createSessionReplacementSuite,verifySessionReplacementReport,captureBinding,verifyCapture,runtimePin}=await import('./suite.mjs');const {runSessionReplacementDiscovery}=await import('./node-runner.mjs');
 const suite=createSessionReplacementSuite({commit,runId:'123'}),node=await runSessionReplacementDiscovery();
 assert.throws(()=>verifySessionReplacementReport(suite,node),'Node diagnostic cannot become browser evidence');
 // Schema-validation unit fixture only. Never emitted as an actual capture.
 const fixture={schema:'pando-m977-boundary-session-replacement-web',version:1,state:'complete',identity:suite.identity,sourceHashes:node.sourceHashes,runtime:{userAgent:'Chrome/'+runtimePin.chromium,node:false,document:true,worker:true,playwright:runtimePin.playwright,browserVersion:runtimePin.chromium,chromiumRevision:runtimePin.revision,cdp:{jsVersion:runtimePin.v8,product:'Chrome/'+runtimePin.chromium},ci:{commit,runId:'123'}},cases:node.cases,limits:suite.identity.limits};
 const suiteText=JSON.stringify(suite),text=JSON.stringify(fixture),binding=captureBinding(suiteText,text);verifyCapture(suiteText,text,binding);
 for(const mutate of [r=>r.runtime.node=true,r=>r.runtime.cdp.jsVersion='fake',r=>r.identity.runId='124',r=>r.cases.reverse(),r=>delete r.cases[0].stages.oldCompleted,r=>r.cases[0].extractionEvidence.sha256='0'.repeat(64),r=>r.sourceHashes[Object.keys(r.sourceHashes)[0]]='0'.repeat(64)]){const bad=structuredClone(fixture);mutate(bad);assert.throws(()=>verifySessionReplacementReport(suite,bad));}
 assert.throws(()=>verifyCapture(suiteText,text+' ',binding));assert.throws(()=>verifyCapture(suiteText+' ',text,binding));assert.throws(()=>verifyCapture(suiteText,text,{...binding,reportBytes:binding.reportBytes-1}));
});

test('a real deep dependency edit or added transitive import changes executable closure identity',async()=>{
 const path=await import('node:path'),os=await import('node:os');const {sessionReplacementHarnessHashes}=await import('./suite.mjs');const directory=fs.mkdtempSync(path.join(os.tmpdir(),'m977-deep-closure-')),harness=path.join(directory,'tools/m977-boundary-session-replacement');fs.mkdirSync(harness,{recursive:true});
 try{const hashes=sessionReplacementHarnessHashes();for(const name of Object.keys(hashes)){const destination=path.resolve(harness,name);fs.mkdirSync(path.dirname(destination),{recursive:true});fs.copyFileSync(new URL(name,import.meta.url),destination);}assert.deepEqual(sessionReplacementHarnessHashes(harness,directory),hashes);
  const deep='../m97/river/stress-cases.mjs';fs.appendFileSync(path.resolve(harness,deep),'\nimport "./replacement-deep-test.mjs";\n');fs.writeFileSync(path.resolve(harness,'../m97/river/replacement-deep-test.mjs'),'export const value=1;\n');
  const changed=sessionReplacementHarnessHashes(harness,directory);assert.notEqual(changed[deep],hashes[deep]);assert.match(changed['../m97/river/replacement-deep-test.mjs'],/^[a-f0-9]{64}$/);
 }finally{fs.rmSync(directory,{recursive:true});}
});
