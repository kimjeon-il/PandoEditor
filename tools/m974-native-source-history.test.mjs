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

// Retain the original native-v9 cases above. These variants exercise current
// native-v10 bytes without changing any pinned browser or M972–4 fixtures.
function rewriteCanonical(native,mutate,sides=['before','after']) {
 for(const side of sides){const document=JSON.parse(Buffer.from(native[side+'DocumentBytesBase64'],'base64'));mutate(document);const bytes=Buffer.from(JSON.stringify(document));native[side+'DocumentBytesBase64']=bytes.toString('base64');native[side+'DocumentSha256']=digest(bytes);}
}
function versionedPair(version) {
 const [web,native]=structuredClone(pair());rewriteCanonical(native,document=>{document.version=version;if(version===10)document.geometryProvenance={schemaVersion:1,originalArchive:[],inlineAllocations:[],opaqueBaseline:[],opaqueUncertain:false};});return [web,native];
}
test('native v10 format retains exact canonical history and source checks',()=>{const result=api.compareSourceHistoryCase(...versionedPair(10));assert.equal(result.passed,true,result.differences.join('\n'));assert.equal(result.rawParity,false);});
test('native document version marker rejects unsupported and mistyped values',()=>{for(const version of [0,8,11,9.5,'9','10',null]){const result=api.compareSourceHistoryCase(...versionedPair(version));assert.equal(result.passed,false,'unexpected native version '+JSON.stringify(version));}});
test('native v10 requires its complete typed provenance format marker',()=>{
 const mutations=[d=>delete d.geometryProvenance,d=>d.geometryProvenance=null,d=>d.geometryProvenance=[],d=>d.geometryProvenance={},d=>d.geometryProvenance.schemaVersion=2,d=>d.geometryProvenance.schemaVersion='1',d=>d.geometryProvenance.opaqueUncertain='false',d=>delete d.geometryProvenance.opaqueUncertain,d=>d.geometryProvenance.future=true];
 for(const key of ['originalArchive','inlineAllocations','opaqueBaseline'])mutations.push(d=>delete d.geometryProvenance[key],d=>d.geometryProvenance[key]={});
 for(const mutate of mutations){const [web,native]=versionedPair(10);rewriteCanonical(native,mutate);assert.equal(api.compareSourceHistoryCase(web,native).passed,false);}
});
test('native project format marker remains required in both supported versions',()=>{for(const version of [9,10])for(const mutate of [d=>delete d.format,d=>d.format='other',d=>d.format=10]){const [web,native]=versionedPair(version);rewriteCanonical(native,mutate);assert.equal(api.compareSourceHistoryCase(web,native).passed,false);}});
test('native v9 cannot smuggle the native-v10 provenance marker',()=>{const [web,native]=versionedPair(9);rewriteCanonical(native,d=>d.geometryProvenance={schemaVersion:1,originalArchive:[],inlineAllocations:[],opaqueBaseline:[],opaqueUncertain:false});assert.equal(api.compareSourceHistoryCase(web,native).passed,false);});
test('native v10 still authenticates geometry and the entire restored ledger',()=>{
 const [web,native]=versionedPair(10);rewriteCanonical(native,d=>d.geometries[0].geojson.coordinates[0][1][0]=1);assert.equal(api.compareSourceHistoryCase(web,native).passed,false);
 const [otherWeb,otherNative]=versionedPair(10);rewriteCanonical(otherNative,d=>d.geometryProvenance.opaqueUncertain=true,['after']);assert.equal(api.compareSourceHistoryCase(otherWeb,otherNative).passed,false);
});
