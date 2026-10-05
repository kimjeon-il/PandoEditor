import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
const api=await import('./m974-native-source-history.mjs').catch(error=>{if(error.code!=='ERR_MODULE_NOT_FOUND')throw error;return {};});
const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
function pair(){
 const geometry={type:'Polygon',coordinates:[[[0,0],[0,2],[2,2],[2,0],[0,0]]]},ref={id:'g',version:1};
 const document={format:'pandoeditor-project',version:9,units:[],timelineRecords:{geometryBindings:[]},geometries:[{...ref,geojson:geometry}],content:{genericFeatures:[{id:'ga',geometryRef:ref},{id:'gb',geometryRef:ref}]}},bytes=Buffer.from(JSON.stringify(document)),sources=['ga','gb'].map(id=>({domain:'generic',id,geometry}));
 const canonical=JSON.stringify({entities:[],genericFeatures:sources.map(({id,geometry})=>({id,geometry})),hydroEdits:[],labels:[],distributionLayers:[],distributionEntries:[]});
 const candidate={kind:'vertex',coordinate:[0,0],ownerIds:['ga'],nodeKey:'0.0000000,0.0000000'},indicator={...candidate,segmentKey:null,segmentEndpoints:null};
 const stage={indicator,canonical,sourceRows:['generic:ga','generic:gb'],candidates:[candidate],winner:'ga'};
 return [{case:'generic-unsynced',input:{nativeCase:'no-query-retains',queryCoordinate:[0,0]},stages:{warm:stage,restored:{canonical,sourceRows:stage.sourceRows,canonicalEqual:true},final:stage}},
 {case:'no-query-retains',scope:'native-controller-source-history',sourceGeometries:sources,restoredSourceGeometries:sources,warm:{indicator},final:{indicator},canonicalEqualAfterUndo:true,beforeDocumentBytesBase64:bytes.toString('base64'),afterDocumentBytesBase64:bytes.toString('base64'),beforeDocumentSha256:digest(bytes),afterDocumentSha256:digest(bytes),transitions:{revisionBefore:0,revisionAfterDelete:1,revisionAfterUndo:2,submittedBeforeDelete:1,submittedAfterUndo:1,submittedAfterFinal:2}}];
}
test('source-history comparison authenticates same geometry and exact restored bytes',()=>{assert.equal(typeof api.compareSourceHistoryCase,'function');const result=api.compareSourceHistoryCase(...pair());assert.equal(result.passed,true);assert.equal(result.rawParity,false);});
test('source-history comparison rejects altered winners, identity and missing observations',()=>{assert.equal(typeof api.compareSourceHistoryCase,'function');for(const mutate of [n=>n.final.indicator.ownerIds=['gb'],n=>n.case='unknown',n=>n.afterDocumentSha256='0'.repeat(64),n=>n.beforeDocumentBytesBase64+='!',n=>delete n.transitions.submittedAfterFinal,n=>n.sourceGeometries.reverse(),n=>n.scope='full-parity']){const [w,n]=structuredClone(pair());mutate(n);assert.equal(api.compareSourceHistoryCase(w,n).passed,false);}});
test('source-history comparison rejects web restoration mismatch and nonexact coordinates',()=>{assert.equal(typeof api.compareSourceHistoryCase,'function');for(const mutate of [w=>w.stages.restored.canonical+=' ',w=>w.stages.final.candidates[0].coordinate[0]=Number.MIN_VALUE,w=>w.stages.warm.sourceRows.reverse(),w=>w.stages.restored.canonicalEqual=false]){const [w,n]=structuredClone(pair());mutate(w);assert.equal(api.compareSourceHistoryCase(w,n).passed,false);}});
test('collection gate rejects missing coverage and duplicate case identities',()=>{assert.equal(typeof api.compareSourceHistoryCollections,'function');for(const native of [{schema:'pando-m974-native-source-history',version:1,cases:[]},{schema:'pando-m974-native-source-history',version:1,cases:[pair()[1],pair()[1]]}])assert.throws(()=>api.compareSourceHistoryCollections({cases:[pair()[0]]},native));});
test('source-history CLI requires exact nonambiguous commit/run identities',()=>{assert.equal(typeof api.parseArguments,'function');assert.throws(()=>api.parseArguments([]));assert.throws(()=>api.parseArguments(['--unexpected','x']));assert.throws(()=>api.parseArguments(['--expected-run-id','2','--expected-run-id','3']));});
test('fixed paired collection succeeds and keeps unpaired evidence explicitly separate',()=>{
 const cases=api.requiredNativeCases.map(id=>{const [w,n]=structuredClone(pair());w.input.nativeCase=id;w.case=id;n.case=id;if(!api.requiredNativeCases.slice(0,5).includes(id))n.scope='native-controller-selection-source-history';if(id==='stopped-worker-ignores-root-delete-undo')n.transitions.revisionAfterUndo=4;return [w,n];});
 const extras=['query-while-deleted-appends','root-addition-syncs-generic-deletion','replacement-resets','child-addition-defers','root-metadata-defers'].map(id=>({...pair()[1],case:id}));
 const result=api.compareSourceHistoryCollections({cases:cases.map(row=>row[0])},{schema:'pando-m974-native-source-history',version:1,cases:[...cases.map(row=>row[1]),...extras]});assert.equal(result.passed,true);assert.equal(result.pairedCases,11);assert.equal(result.nativeOnlyCases.length,5);assert.equal(result.rawParity,false);
});

test('comparison uses resolved browser indicator rather than the first raw candidate',()=>{const [w,n]=structuredClone(pair());w.stages.final.indicator={...w.stages.final.indicator,ownerIds:['gb']};assert.equal(api.compareSourceHistoryCase(w,n).passed,false);});
