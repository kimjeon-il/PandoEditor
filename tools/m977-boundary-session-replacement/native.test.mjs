import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {boundarySessionReplacementCases} from './runtime.mjs';
const probe=process.env.M977_SESSION_REPLACEMENT_PROBE;
test('native replacement probe and fail-closed comparator are implemented',()=>{
 assert.ok(fs.existsSync(new URL('../../tests/m977_boundary_session_replacement_probe.cpp',import.meta.url)),'Native public replacement probe is missing');
 assert.ok(fs.existsSync(new URL('./compare.mjs',import.meta.url)),'Paired replacement comparator is missing');
});
test('actual public native completions and paired observations preserve replacement identity',{skip:!probe},async()=>{
 const {verifyNativeSessionReplacement,compareSessionReplacement}=await import('./compare.mjs');
 const {runSessionReplacementDiscovery}=await import('./node-runner.mjs');
 const definitions=boundarySessionReplacementCases(),result=spawnSync(probe,[],{input:JSON.stringify(definitions),encoding:'utf8',timeout:180000,maxBuffer:32*1024*1024});assert.equal(result.status,0,result.stderr);const receipt=JSON.parse(result.stdout);verifyNativeSessionReplacement(receipt);


 for(const change of [c=>c.geometries[0].geojson.coordinates[0][1][0]=0.5,c=>c.units[0].locked=true,c=>c.units[0].kind='regional',c=>c.timelineRecords.parentRelations[0].parentId='C',c=>c.timelineRecords.geometryBindings[0].geometryRef.id='B',c=>c.timelineRecords.lifetimes[0].validFrom='2000-01-01']){
  const bad=structuredClone(receipt);for(const stage of Object.values(bad.rows[0].stages)){change(stage.state.nativeCanonicalDocument);const bytes=Buffer.from(JSON.stringify(stage.state.nativeCanonicalDocument));stage.state.canonicalBytesBase64=bytes.toString('base64');stage.state.documentSha256=createHash('sha256').update(bytes).digest('hex');}
  assert.throws(()=>verifyNativeSessionReplacement(bad),'resealed canonical input cannot contradict unchanged declared/flattened fixture');
 }
 {const bad=structuredClone(receipt);bad.rows[0].stages.settled.state.document.entities[0].geometry.coordinates[0][1][0]=0.5;assert.throws(()=>verifyNativeSessionReplacement(bad),'later flattened document must still match authenticated baseline');}
 for(const change of [e=>e.edit.previewReady=true,e=>e.edit.targets.push({domain:'territorial',id:'C'}),e=>e.edit.active=false]){
  const bad=structuredClone(receipt),r=bad.rows[0],drain=r.events.find(e=>e.kind==='owner-completion-drain'),index=drain.sequence+1,signal=structuredClone(r.events.find(e=>e.kind==='geometry-signal'&&e.sequence>drain.sequence));change(signal);r.events.splice(index,0,signal);r.events.forEach((e,j)=>e.sequence=j);for(const stage of Object.values(r.stages))if(stage.eventSequence>=index)stage.eventSequence++;
  assert.throws(()=>verifyNativeSessionReplacement(bad),'transient old publication/reset cannot hide behind correct final replacement');
 }
 const web=await runSessionReplacementDiscovery(),comparison=compareSessionReplacement(web,receipt);assert.equal(comparison.passed,true);assert.equal(comparison.rawParity,false);assert.equal(comparison.parityAccepted,false);assert.equal(comparison.summary.matchedCases,2);
 for(const mutate of [r=>r.rows.pop(),r=>r.rows.reverse(),r=>r.rows[0].stages.settled.state.documentSha256='0'.repeat(64),r=>r.rows[0].stages.settled.state.canonicalBytesBase64+='!',r=>r.rows[0].stages.settled.state.history.canUndo=true,r=>r.rows[0].stages.replacementSelected.selection.id='B',r=>r.rows[0].stages.replacementEntered.selection.id='A',r=>r.rows[0].stages.settled.edit.previewReady=true,r=>r.rows[0].stages.settled.edit.targets[0].domain='generic',r=>r.rows[0].stages.settled.draftPaths.flatMap(p=>p.vertices).find(v=>v.nodeKey==='1,1').fixed=false,r=>r.rows[0].completionBarrier.ownerEventsProcessedBeforeReplacement=true,r=>r.rows[0].events.find(e=>e.kind==='owner-completion-drain').sequence=0,r=>delete r.rows[0].stages.oldCompleted,r=>r.rows[0].limits.privateRequestIds=true,r=>r.rows[0].stageOrder.reverse()]){const bad=structuredClone(receipt);mutate(bad);assert.throws(()=>verifyNativeSessionReplacement(bad));}
});
