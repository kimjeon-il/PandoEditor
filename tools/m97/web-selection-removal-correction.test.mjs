import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {prepareCorrectedSelectionSources} from './web-selection-correction.mjs';
import {prepareRiverRemovalCorrectedSelectionSources} from './web-selection-removal-correction.mjs';
import {loadSelectionModules,createSelectionRuntime,seedSelectionFeatures,settle} from './web-selection.mjs';

const blob=bytes=>createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`),bytes])).digest('hex');
const workflowPath='assets/js/modules/app-territory-selection-workflow.js';
test('approved river-removal overlay pins exact 53db to 12cd to 6c3 source ancestry',()=>{
  const corrected=prepareRiverRemovalCorrectedSelectionSources();
  try{
    assert.equal(corrected.manifest.behavioralCommit,'6c3f930b8573fa09991885b661879ea36725472e');
    assert.deepEqual(corrected.sourceChain.map(row=>row.behavioralCommit),['12cd8c8ec47c83cfb8c650e8f44c81cdfac10043','6c3f930b8573fa09991885b661879ea36725472e']);
    assert.equal(blob(readFileSync(resolve(corrected.root,workflowPath))),'9cd369065747de407e4e83df3b6060383b3636a8');
    assert.equal(blob(readFileSync(new URL('../../tests/fixtures/web-m97/lifecycle-source/'+workflowPath,import.meta.url))),'e99033f1402a6c1e55cfe2a32e8398026aff86fc');
    assert.equal(blob(readFileSync(new URL('../../tests/fixtures/web-m97/selection-correction-source/'+workflowPath,import.meta.url))),'8f04dc31c77de0e7168074d92195040fee3d0f7a');
  }finally{corrected.cleanup();}
});

async function removalCase(prepare,count){
  const corrected=prepare();let h;
  try{
    const api=await loadSelectionModules(corrected.root),features=seedSelectionFeatures(api,{remote:false});
    const riverFeatures=[2.5,5,7.5].map((x,i)=>({type:'Feature',id:'river-'+i,properties:{pandolab_id:'river-'+i,category:'river'},geometry:{type:'LineString',coordinates:[[x,-1],[x,11]]}}));
    h=createSelectionRuntime(api,{features,riverFeatures});const original=JSON.stringify(features);
    assert.ok(h.workflow.start('annex',{targetCountryId:'target',sourceCountryIds:['donor']}));assert.equal(await h.workflow.advance(),true);
    assert.equal(await h.workflow.selectMethod('components'),true);await settle(h);h.workflow.toggleRiverBoundaries(true);await settle(h);
    const initialItems=structuredClone(h.components.territoryComponentItems()),initialFeatures=structuredClone(h.workflow.activeSession().componentFeatures);
    for(const item of initialItems.slice(0,count)){assert.equal(h.workflow.toggleComponent(item.key),true);await settle(h);}
    assert.equal(h.workflow.addPart(),true);await settle(h);assert.equal(h.workflow.activeSession().parts.length,count);
    assert.equal(await h.workflow.selectMethod('components'),true);await settle(h);h.workflow.toggleRiverBoundaries(true);await settle(h);
    const beforeFeatures=structuredClone(h.workflow.activeSession().componentFeatures),beforeItems=structuredClone(h.components.territoryComponentItems()),parts=structuredClone(h.workflow.activeSession().parts);
    const index=count===1?0:1;assert.equal(h.workflow.removePart(parts[index].id),true);await settle(h);
    const state=h.workflow.activeSession();assert.equal(JSON.stringify(features),original);
    return {initialItems,initialFeatures,beforeFeatures,beforeItems,parts,index,components:structuredClone(h.components.territoryComponentItems()),componentFeatures:structuredClone(state.componentFeatures),remainingParts:structuredClone(state.parts),status:state.riverPartitionStatus,hasRiverIndex:!!state.componentIndex?.river,combinedGeometry:state.combinedGeometry,archivedGeometry:state.archivedGeometry,selectedKeys:structuredClone(state.selectedComponentKeys)};
  }finally{h?.workflow.clear();corrected.cleanup();}
}
for(const [label,count] of [['middle',3],['last',1]])test(`actual approved production workflow automatically rebuilds ${label} archived removal`,async()=>{
  const result=await removalCase(prepareRiverRemovalCorrectedSelectionSources,count);
  assert.equal(result.status,'ready');assert.equal(result.hasRiverIndex,true,'Current river index must be rebuilt without off/on workaround');assert.ok(result.components.length>0);
  assert.deepEqual(result.remainingParts,result.parts.filter((_,i)=>i!==result.index));assert.deepEqual(result.selectedKeys,[]);
  assert.notDeepEqual(result.componentFeatures,result.beforeFeatures);
  if(count===1){assert.deepEqual(result.componentFeatures,result.initialFeatures);assert.deepEqual(result.components,result.initialItems);assert.equal(result.combinedGeometry,null);assert.equal(result.archivedGeometry,null);}
});
test('earlier 12cd source remains an unchanged historical stale-index observation',async()=>{
  for(const count of [3,1]){const result=await removalCase(prepareCorrectedSelectionSources,count);assert.equal(result.hasRiverIndex,count===1);assert.deepEqual(result.components,count===1?result.beforeItems:[]);if(count===1)assert.deepEqual(result.componentFeatures,result.beforeFeatures);}
});

test('approved removal source manifest rejects forged ancestry and hashes before import',()=>{
  const manifest=JSON.parse(readFileSync(new URL('../../tests/fixtures/web-m97/selection-removal-correction-manifest.json',import.meta.url)));
  for(const mutate of [m=>m.baseBehavioralCommit='0'.repeat(40),m=>m.behavioralCommit='1'.repeat(40),m=>m.changes[0].baseBlob='2'.repeat(40),m=>m.changes[0].blob='3'.repeat(40),m=>m.changes[0].sha256='4'.repeat(64),m=>m.changes.push(m.changes[0])]){
    const changed=structuredClone(manifest);mutate(changed);assert.throws(()=>prepareRiverRemovalCorrectedSelectionSources({manifest:changed}));
  }
});
