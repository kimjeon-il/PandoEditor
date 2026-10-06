import test from 'node:test';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {boundaryTimingCases,runBoundaryTimingCase} from './runtime.mjs';
import {loadSourceHistoryNodeSources} from '../m974-snap-boundary/source-history-sources.mjs';
import {verifyNativeTiming,compareTimingObservations} from './compare.mjs';
const probe=process.env.M977_BOUNDARY_PROBE;
test('native public-controller timing and truthful comparison', {skip:!probe}, async()=>{
 const cases=boundaryTimingCases(),result=spawnSync(probe,[],{input:JSON.stringify(cases),encoding:'utf8',timeout:180000,maxBuffer:32*1024*1024});assert.equal(result.status,0,result.stderr);const receipt=JSON.parse(result.stdout);verifyNativeTiming(cases,receipt);
 const loaded=await loadSourceHistoryNodeSources();
 try{
  const rows=[];for(const definition of cases)rows.push(await runBoundaryTimingCase(loaded,definition));
  const comparison=compareTimingObservations({cases:rows},receipt);assert.equal(comparison.parityAccepted,false);assert.equal(comparison.rawParity,false);assert.deepEqual(comparison.summary,{cases:9,publicSelectionCases:3,boundaryReadinessDivergences:0,previewDivergences:0,nativeAbsentIntervalCases:6,canonicalOrHistoryMutations:0});
 }finally{loaded.cleanup();}
 for(const mutate of [
  r=>r.rows[0].completionBarrier.workerPoolCompletedBeforeFinalSnapshot=false,
  r=>r.rows[0].stages.settled.state.canonicalBytesBase64+='QQ==',
  r=>r.rows[0].stages.settled.state.documentSha256='0'.repeat(64),
  r=>r.rows[0].stages.settled.state.history.canUndo=true,
  r=>r.rows[0].stages.interval.outcome.workerStarted=true,
  r=>r.rows[3].stages.interval.observed=true,
  r=>r.rows[3].limits.renderAdoptionPending=true,
  r=>r.rows[7].stages.settled.edit.previewReady=true,
  r=>r.rows[2].stages.settled.edit.targets=[{domain:'territorial',id:'C'}],
  r=>r.rows[2].stages.settled.selection.id='A',
  r=>r.rows[2].stages.afterAction.selection.id='A',
  r=>r.rows[2].stages.settled.selectionItems[0].domain='generic',
  r=>r.rows[2].stages.settled.selection.domain='generic',
  r=>r.rows[0].stages.settled.state.unchangedFromBefore=false,
  r=>r.rows[0].stages.settled.state.canonicalBytesBase64+='!',
  r=>{const s=r.rows[0].stages.settled.state,bytes=Buffer.concat([Buffer.from(s.canonicalBytesBase64,'base64'),Buffer.from(' ')]);s.canonicalBytesBase64=bytes.toString('base64');s.documentSha256=createHash('sha256').update(bytes).digest('hex');},
  r=>r.rows[2].stages.settled.selectionItems=[],
  r=>r.rows.pop(),
 ]){const bad=structuredClone(receipt);mutate(bad);assert.throws(()=>verifyNativeTiming(cases,bad));}
});

// Mutated native receipts below exercise the comparator; they are not captures.
test('comparison computes selection differences from observed states instead of case labels', {skip:!probe}, async()=>{
 const cases=boundaryTimingCases(),result=spawnSync(probe,[],{input:JSON.stringify(cases),encoding:'utf8',timeout:180000,maxBuffer:32*1024*1024});assert.equal(result.status,0,result.stderr);const receipt=JSON.parse(result.stdout);
 const loaded=await loadSourceHistoryNodeSources();
 try{
  const rows=[];for(const definition of cases)rows.push(await runBoundaryTimingCase(loaded,definition));const web={cases:rows};
  const matching=structuredClone(receipt);for(const [i,definition]of cases.entries())if(definition.action==='select')Object.assign(matching.rows[i].stages.settled.edit,{boundaryStatus:'ready',previewReady:definition.phase==='move-queued'});
  const matched=compareTimingObservations(web,matching);assert.equal(matched.summary.boundaryReadinessDivergences,0);assert.equal(matched.summary.previewDivergences,0);assert.equal(matched.summary.nativeAbsentIntervalCases,6);assert.ok(matched.cases.every(row=>row.differences.length===0));
  const changed=structuredClone(matching);Object.assign(changed.rows[8].stages.settled.edit,{boundaryStatus:'error',previewReady:false});
  const different=compareTimingObservations(web,changed);assert.equal(different.summary.boundaryReadinessDivergences,1);assert.equal(different.summary.previewDivergences,1);assert.deepEqual(different.cases[8].differences,[{field:'boundaryStatus',web:'ready',native:'error'},{field:'preview',web:true,native:false}]);assert.ok(different.remaining.some(reason=>reason.includes('Public selection')));
  const previewOnly=structuredClone(matching);previewOnly.rows[8].stages.settled.edit.previewReady=false;const previewDifference=compareTimingObservations(web,previewOnly);assert.equal(previewDifference.summary.boundaryReadinessDivergences,0);assert.equal(previewDifference.summary.previewDivergences,1);
  for(const mutate of [
   r=>r.rows[0].stages.settled.state.history.canUndo=true,
   r=>r.rows[0].stages.settled.state.canonicalBytesBase64+='QQ==',
   r=>r.rows[2].stages.settled.edit.targets=[{domain:'territorial',id:'C'}],
   r=>r.rows[2].stages.afterAction.selection.id='A',
   r=>r.rows[2].stages.settled.selectionItems[0].domain='generic',
   r=>r.rows[3].stages.interval.observed=true,
   r=>r.rows[7].stages.settled.edit.previewReady=true,
  ]){const bad=structuredClone(matching);mutate(bad);assert.throws(()=>compareTimingObservations(web,bad));}
  for(const mutate of [r=>r.cases[0].stages.settled.history.undo++,r=>r.cases[0].stages.settled.canonical+=' ',r=>r.cases[8].stages.settled.preparation.ownerIds=['C']]){const bad=structuredClone(web);mutate(bad);assert.throws(()=>compareTimingObservations(bad,matching));}
 }finally{loaded.cleanup();}
});

test('native timing integrity requires actual revisions lifecycle target domains and limits', {skip:!probe}, async t=>{
 const cases=boundaryTimingCases(),result=spawnSync(probe,[],{input:JSON.stringify(cases),encoding:'utf8',timeout:180000,maxBuffer:32*1024*1024});assert.equal(result.status,0,result.stderr);const receipt=JSON.parse(result.stdout);
 for(const [name,mutate]of [
  ['all revision observations missing',r=>{for(const stage of Object.values(r.rows[0].stages))delete stage.revision;}],
  ['entered active observation missing',r=>delete r.rows[0].stages.entered.edit.active],
  ['entered active observation forged false',r=>r.rows[0].stages.entered.edit.active=false],
  ['target domain missing',r=>delete r.rows[0].stages.entered.edit.target.domain],
  ['target domain forged',r=>r.rows[0].stages.entered.edit.target.domain='generic'],
  ['history depth limit missing',r=>delete r.rows[0].limits.historyDepth],
  ['move preview was not queued',r=>r.rows[6].stages.actionBoundary.outcome.workerQueued=false],
  ['move preview was not calculating',r=>r.rows[6].stages.actionBoundary.edit.calculating=false],
  ['preparation timing equivalence forged',r=>r.rows[0].stages.actionBoundary.outcome.timingEquivalent=false],
 ])await t.test(name,()=>{const bad=structuredClone(receipt);mutate(bad);assert.throws(()=>verifyNativeTiming(cases,bad));});
});

test('native canonical fixture cannot be resealed independently of declared and flattened source', {skip:!probe},async t=>{
 const cases=boundaryTimingCases(),result=spawnSync(probe,[],{input:JSON.stringify(cases),encoding:'utf8',timeout:180000,maxBuffer:32*1024*1024});assert.equal(result.status,0,result.stderr);const receipt=JSON.parse(result.stdout);
 for(const [name,mutate]of [
  ['geometry',d=>d.geometries[0].geojson.coordinates[0][1][0]=0.5],
  ['canonical unit identity',d=>d.units[0].id='forged'],
  ['canonical unit lock',d=>d.units[0].locked=true],
  ['parent binding',d=>d.timelineRecords.parentRelations[0].parentId='B'],
  ['geometry binding',d=>d.timelineRecords.geometryBindings[0].geometryRef.id='B'],
 ])await t.test(name,()=>{const bad=structuredClone(receipt);for(const stage of Object.values(bad.rows[0].stages))if(stage.observed){mutate(stage.state.nativeCanonicalDocument);const bytes=Buffer.from(JSON.stringify(stage.state.nativeCanonicalDocument));stage.state.canonicalBytesBase64=bytes.toString('base64');stage.state.documentSha256=createHash('sha256').update(bytes).digest('hex');}assert.throws(()=>verifyNativeTiming(cases,bad));});
 await t.test('all later flattened documents remain tied to authenticated baseline',()=>{const bad=structuredClone(receipt);bad.rows[0].stages.settled.state.document.entities[0].geometry.coordinates[0][1][0]=0.5;assert.throws(()=>verifyNativeTiming(cases,bad));});
});
