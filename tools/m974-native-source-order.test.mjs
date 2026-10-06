import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
const api=await import('./m974-native-source-order.mjs').catch(e=>{if(e.code!=='ERR_MODULE_NOT_FOUND')throw e;return {};});
const hash=b=>createHash('sha256').update(b).digest('hex');
function pair(){
 const geometry={type:'Polygon',coordinates:[[[0,0],[0,2],[2,2],[2,0],[0,0]]]},sources=['a','b'].map(id=>({id,geometry:structuredClone(geometry)}));
 const document={format:'pandoeditor-project',version:9,units:sources.map(({id})=>({id,kind:'general'})),timelineRecords:{geometryBindings:sources.map(({id})=>({entityId:id,validFrom:null,validTo:null,geometryRef:{id,version:1}}))},geometries:sources.map(({id,geometry})=>({id,version:1,geojson:geometry}))};
 const bytes=Buffer.from(JSON.stringify(document)),indicator=id=>({kind:'vertex',coordinate:[0,0],ownerIds:[id],nodeKey:'0.0000000,0.0000000',segmentKey:null,segmentEndpoints:null});
 const web={case:'source-order-v2-coincident-remove-restore-a',input:{features:sources.map(s=>({...s,type:'Feature'})),query:{coordinate:[0,0]}},stages:{warm:{candidates:[indicator('a')],firstCandidateOwnerIds:['a']},afterRestore:{candidates:[indicator('b')],firstCandidateOwnerIds:['b']}}};
 const native={schema:'pando-m974-native-controller-source-order',version:1,case:web.case,scope:'native-controller-delete-undo',sourceGeometries:sources,restoredSourceGeometries:structuredClone(sources),warm:{indicator:indicator('a')},afterRestore:{indicator:indicator('b')},transitions:{deleteStarted:true,deleteApplied:true,undoAvailable:true,undoApplied:true,revisionBefore:0,revisionAfterDelete:1,revisionAfterUndo:2,submittedBeforeDelete:1,submittedAfterUndo:1},canonicalEqualAfterUndo:true,beforeDocumentSha256:hash(bytes),afterDocumentSha256:hash(bytes),beforeDocumentBytesBase64:bytes.toString('base64'),afterDocumentBytesBase64:bytes.toString('base64')};return [web,native];
}
test('source-order comparison preserves separate stimulus layers',()=>{assert.equal(typeof api.compareSourceOrder,'function');const r=api.compareSourceOrder(...pair());assert.equal(r.passed,true);assert.equal(r.rawParity,false);assert.equal(r.fullWebUndoParity,false);});
test('wrong winner coordinate and missing values fail closed',()=>{for(const mutate of [n=>n.afterRestore.indicator.ownerIds=['a'],n=>n.afterRestore.indicator.coordinate=[Number.MIN_VALUE,0],n=>delete n.warm,n=>delete n.transitions.undoApplied,n=>n.transitions.submittedAfterUndo=2]){const [w,n]=pair();mutate(n);assert.equal(api.compareSourceOrder(w,n).passed,false);}});
test('native canonical bytes authenticate both hash and source geometry',()=>{for(const mutate of [n=>n.beforeDocumentBytesBase64+='!',n=>n.afterDocumentSha256='0'.repeat(64),n=>n.sourceGeometries[0].geometry.coordinates[0][1][0]=1,n=>n.restoredSourceGeometries.reverse()]){const [w,n]=pair();mutate(n);assert.equal(api.compareSourceOrder(w,n).passed,false);}});
test('unknown exclusions and schema versions are rejected',()=>{for(const mutate of [n=>n.exclusions=['winner'],n=>n.version=2,n=>n.case='other']){const [w,n]=pair();mutate(n);assert.equal(api.compareSourceOrder(w,n).passed,false);}});
test('CLI requires exact identities and rejects ambiguous options',()=>{assert.equal(typeof api.parseArguments,'function');for(const args of [[],['--unknown','x'],['--expected-run-id','1','--expected-run-id','2']])assert.throws(()=>api.parseArguments(args));});
test('artifact identity rejects stale same-commit runs and missing identity',()=>{assert.equal(typeof api.verifyIdentity,'function');const expected={expectedCommit:'a'.repeat(40),expectedRunId:'123'},suite={commit:expected.expectedCommit,runId:'123',identity:{commit:expected.expectedCommit,runId:'123'}},report={identity:{...suite.identity}};api.verifyIdentity(suite,report,expected);assert.throws(()=>api.verifyIdentity(suite,report,{...expected,expectedRunId:'124'}));assert.throws(()=>api.verifyIdentity(suite,{identity:{}},expected));});

// Retain the original native-v9 cases above. These variants exercise current
// native-v10 bytes without changing any pinned browser or M972–4 fixtures.
function rewriteCanonical(native,mutate,sides=['before','after']) {
 for(const side of sides){const document=JSON.parse(Buffer.from(native[side+'DocumentBytesBase64'],'base64'));mutate(document);const bytes=Buffer.from(JSON.stringify(document));native[side+'DocumentBytesBase64']=bytes.toString('base64');native[side+'DocumentSha256']=hash(bytes);}
}
function versionedPair(version) {
 const [web,native]=structuredClone(pair());rewriteCanonical(native,document=>{document.version=version;if(version===10)document.geometryProvenance={schemaVersion:1,originalArchive:[],inlineAllocations:[],opaqueBaseline:[],opaqueUncertain:false};});return [web,native];
}
test('native v10 format retains exact canonical history and source checks',()=>{const result=api.compareSourceOrder(...versionedPair(10));assert.equal(result.passed,true,result.differences.join('\n'));assert.equal(result.rawParity,false);});
test('native document version marker rejects unsupported and mistyped values',()=>{for(const version of [0,8,11,9.5,'9','10',null]){const result=api.compareSourceOrder(...versionedPair(version));assert.equal(result.passed,false,'unexpected native version '+JSON.stringify(version));}});
test('native v10 requires its complete typed provenance format marker',()=>{
 const mutations=[d=>delete d.geometryProvenance,d=>d.geometryProvenance=null,d=>d.geometryProvenance=[],d=>d.geometryProvenance={},d=>d.geometryProvenance.schemaVersion=2,d=>d.geometryProvenance.schemaVersion='1',d=>d.geometryProvenance.opaqueUncertain='false',d=>delete d.geometryProvenance.opaqueUncertain,d=>d.geometryProvenance.future=true];
 for(const key of ['originalArchive','inlineAllocations','opaqueBaseline'])mutations.push(d=>delete d.geometryProvenance[key],d=>d.geometryProvenance[key]={});
 for(const mutate of mutations){const [web,native]=versionedPair(10);rewriteCanonical(native,mutate);assert.equal(api.compareSourceOrder(web,native).passed,false);}
});
test('native project format marker remains required in both supported versions',()=>{for(const version of [9,10])for(const mutate of [d=>delete d.format,d=>d.format='other',d=>d.format=10]){const [web,native]=versionedPair(version);rewriteCanonical(native,mutate);assert.equal(api.compareSourceOrder(web,native).passed,false);}});
test('native v9 cannot smuggle the native-v10 provenance marker',()=>{const [web,native]=versionedPair(9);rewriteCanonical(native,d=>d.geometryProvenance={schemaVersion:1,originalArchive:[],inlineAllocations:[],opaqueBaseline:[],opaqueUncertain:false});assert.equal(api.compareSourceOrder(web,native).passed,false);});
test('native v10 still authenticates geometry and the entire restored ledger',()=>{
 const [web,native]=versionedPair(10);rewriteCanonical(native,d=>d.geometries[0].geojson.coordinates[0][1][0]=1);assert.equal(api.compareSourceOrder(web,native).passed,false);
 const [otherWeb,otherNative]=versionedPair(10);rewriteCanonical(otherNative,d=>d.geometryProvenance.opaqueUncertain=true,['after']);assert.equal(api.compareSourceOrder(otherWeb,otherNative).passed,false);
});
