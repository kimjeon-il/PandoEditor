import assert from 'node:assert/strict';
import test from 'node:test';
import {createHash} from 'node:crypto';
import {compareBoundaryCase, compareBoundaryObservations,verifyBoundaryCaptureIdentity,parseBoundaryCaptureArgs} from './m974-native-boundary.mjs';

// Independent synthetic receipts exercise the comparator, never modify the oracle.
const copy = value => structuredClone(value);
const sealNativeState=state=>{const bytes=Buffer.from(JSON.stringify(state.nativeCanonicalDocument));state.canonicalBytesBase64=bytes.toString('base64');state.documentSha256=createHash('sha256').update(bytes).digest('hex');};
const shape = (x = 1) => ({type:'Polygon',coordinates:[[[0,0],[x,0],[x,1],[0,1],[0,0]]]});
const properties = id => ({schemaVersion:5,entityKind:'general',name:id,parentId:'',coverageMode:'explicit',style:{},locked:false,validFrom:null,validTo:null,notes:'',metadata:{},sourceFolderId:'',sourceLibraryId:'',sourceGeometryVersion:''});
const feature = (id, geometry=shape()) => ({type:'Feature',id,properties:properties(id),geometry});
const entity = f => ({id:f.id,parentId:f.properties.parentId,coverageMode:f.properties.coverageMode,entityKind:f.properties.entityKind,properties:copy(f.properties),geometry:copy(f.geometry)});
const unobserved = () => ({observed:false,reason:'The production workflow has not reached this stage.'});
function pair() {
 const input={id:'root-triple',features:[feature('A'),feature('B')],selectedIds:['A','B'],seedId:'A',move:{nodeKey:'1,0',coordinate:[1.25,0]},expectedPath:'root',expected:{entry:true,prepared:true,gesture:true,confirm:true,movedOwnerIds:['A','B']}};
 const handles=[{key:'1,0',nodeKey:'1,0',coordinate:[1,0],coord:[1,0],polygonIndex:0,ringIndex:0,index:1,refs:[{featureId:'A',polygonIndex:0,ringIndex:0,vertexIndex:1},{featureId:'B',polygonIndex:0,ringIndex:0,vertexIndex:1}],virtualRefs:[],ownerIds:['A','B'],boundaryKind:'shared',fixed:false,segments:[{start:[1,0],end:[1,1]}]}];
 const segments=[{key:'1,0|1,1',kind:'shared',start:[1,0],end:[1,1],geometry:{type:'LineString',coordinates:[[1,0],[1,1]]}}];
 const moved=input.features.map(f=>feature(f.id,shape(1.25)));
 const b={case:input.id,input:copy(input),entry:{ok:true,path:'root'},gesture:{ok:true},preparation:{status:'ready',valid:true,current:true,selectedIds:['A','B'],isolatedIds:[],handles:copy(handles),segments:copy(segments)},movedOwnerIds:['A','B'],movedFeatures:copy(moved),visualEvents:[{kind:'move',value:[{start:[1.25,0],end:[1,1]}]},{kind:'begin',value:{key:'border:A|B:1,0'}},{kind:'move',value:[{start:[1.25,0],end:[1,1]}]}],stages:{}};
 const n={case:input.id,input:copy(input),entry:{ok:true},gesture:{ok:true,picked:true},events:[],inputObservations:[{exact:true,intended:[1.25,0],inverse:[1.25,0]}],replayGestures:[],observationLimits:{browserPointerProjection:false,historyDepth:false,labelSettingVisible:false,nativeRawPreviewGeometry:false,workerResultInterception:false},stages:{},topologyHelper:{observed:true,inputOwnerIds:['A','B'],selectedIdsInInputOrder:['A','B'],preparation:{valid:true,error:'',handles:handles.map(({key,coord,boundaryKind,...h})=>({...h,rawRefs:copy(h.refs),rawVirtualRefs:copy(h.virtualRefs),quantizedKey:[1,0]})),segments:segments.map(({kind,geometry,...s})=>({...s,ownerIds:['A','B'],startNodeIndex:0,endNodeIndex:0}))},gesture:{ok:true},move:{changed:true,coordinate:[1.25,0],movedOwnerIds:['A','B'],features:moved.map(({id,geometry})=>({id,geometry:copy(geometry)}))},activeSegments:[{start:[1.25,0],end:[1,1]}],preview:{ok:true,prepareOk:true,error:'',prepareError:'',entities:moved.map(entity),removedIds:[],reparented:[],rows:input.features.map(f=>({id:f.id,before:copy(f.geometry),after:shape(1.25)}))}}};
 function state(features,side,name) {
  const entities=features.map(entity),refs=[{id:'entry-A',kind:'distribution',target:'A'},{id:'A',kind:'label-settings',target:'A'}];
  const presentation=side==='web'?{itemVisibility:{countries:{A:false}},labelSettings:{'territorial:A':{visible:false}},layerPresentation:{styles:{},objectStyles:{'territorial:entity:A':{opacity:0.5}},objectOrder:[]}}:{hiddenItems:[{group:'countries',id:'A'}],labelSettings:[{id:'A',pinned:false,collisionGroup:'map'}],objectStyles:[{key:'territorial:entity:A',opacity:0.5}]};
  const undo=['confirm','redo','settled'].includes(name),redo=name==='undo';
  const document={entities,distributionEntries:[{id:'entry-A',layerId:'distribution',territorialUnitId:'A',value:1}]};
  const timelineRecords={schemaVersion:1,lifetimes:features.map(f=>({id:'lifetime:'+f.id,entityId:f.id,validFrom:null,validTo:null})),parentRelations:features.map(f=>({id:'parent:'+f.id,entityId:f.id,parentId:'',coverageMode:'explicit',validFrom:null,validTo:null})),geometryBindings:features.map(f=>({id:'geometry:'+f.id,entityId:f.id,validFrom:null,validTo:null,geometryRef:{id:f.id,version:1}}))};
  const nativeCanonicalDocument={entities:copy(entities),references:copy(refs),presentation:copy(presentation),timelineRecords,geometries:features.map(f=>({id:f.id,version:1,geojson:copy(f.geometry)}))};
  if(side==='native')nativeCanonicalDocument.presentation={webPresentation:{hiddenItems:{countries:['A']},labelSettings:[{ref:{domain:'territorial',id:'A'},pinned:false,collisionGroup:'map'}],objectStyles:{'territorial:entity:A':{opacity:0.5}}}};
  if(side==='web'){document.timelineRecords=copy(timelineRecords);for(const r of document.timelineRecords.geometryBindings)r.geometryRef.id='territorial-geometry:'+r.geometryRef.id;document.geometryVersions=features.map(f=>({id:'territorial-geometry:'+f.id,version:1,geojson:copy(f.geometry)}));}
  return {document,presentation,history:side==='web'?{undo:Number(undo),redo:Number(redo)}:{canUndo:undo,canRedo:redo},...(side==='web'?{preview:null}:{references:refs,nativeCanonicalDocument,documentSha256:undo?'b'.repeat(64):'a'.repeat(64),unchangedFromBefore:!undo})};
 }
 for(const name of ['before','cold','pending','prepared','drag','preview','cancel','impactCancel','confirm','undo','redo','settled']) {
  if(name==='impactCancel'){b.stages[name]=unobserved();n.stages[name]={observed:false,reason:'The actual native workflow did not reach this stage.'};continue;}
  const features=['confirm','redo','settled'].includes(name)?moved:input.features;
  const outcome=['before','cold'].includes(name)?null:{ok:true,...(name==='drag'?{changed:true,coordinate:[1.25,0],affectedIds:['A','B']}: {})};
  b.stages[name]={observed:true,outcome,state:state(features,'web',name)};
  n.stages[name]={observed:true,outcome:outcome?copy(outcome):{},state:state(features,'native',name),draftPaths:[],edit:{active:!['before','cold','confirm','undo','redo','settled'].includes(name),previewReady:name==='preview',calculating:name==='pending',boundaryStatus:name==='pending'?'preparing':'ready'}};
 }
 b.stages.preview.state.preview={beforeFeatures:copy(input.features),afterFeatures:copy(moved),removedIds:[],affectedIds:['A','B']};
 n.replayGestures=[{afterCancel:'cancel',foundOriginalHandle:true,picked:true,began:true,moved:true,automaticPreviewReady:true,requestedNodeKey:'1,0',coordinate:[1.25,0],handle:{nodeKey:'1,0',ownerIds:['A','B'],fixed:false},before:copy(n.stages.cancel),after:copy(n.stages.preview)}];
 for(const name of Object.keys(b.stages))if(b.stages[name].observed){const ids=['undo','redo','settled'].includes(name)?[]:name==='confirm'?['A']:['A','B'];const primary=ids.length?(['before','cold','confirm'].includes(name)?'A':'B'):null;const items=ids.map(id=>({domain:'territorial',id,key:'territorial:entity:'+id}));b.stages[name].selection={items,keys:items.map(r=>r.key),primaryKey:primary?'territorial:entity:'+primary:null};n.stages[name].selectionItems=copy(items);n.stages[name].selection=primary?{domain:'territorial',id:primary}:{};}
 n.replayGestures[0].before=copy(n.stages.cancel);n.replayGestures[0].after=copy(n.stages.preview);
 for(const stage of Object.values(n.stages))if(stage.observed)sealNativeState(stage.state);for(const replay of n.replayGestures){sealNativeState(replay.before.state);sealNativeState(replay.after.state);}
 return [b,n];
}
const bad=(mutate,pattern)=>{const [b,n]=pair();mutate(b,n);const result=compareBoundaryCase(b,n);assert.equal(result.passed,false,JSON.stringify(result));if(pattern)assert.match(JSON.stringify(result.differences),pattern);};
test('accepts complete matching observable receipts with raw parity explicitly false',()=>{const [b,n]=pair();const r=compareBoundaryCase(b,n);assert.equal(r.passed,true,JSON.stringify(r));assert.equal(r.rawParity,false);});
test('missing native case fails closed',()=>{const [b]=pair();assert.equal(compareBoundaryObservations([b],{rows:[]},{expectedCaseIds:['root-triple']}).passed,false);});
test('missing stage fails closed',()=>bad((b,n)=>delete n.stages.undo,/undo/));
test('missing helper fails closed',()=>bad((b,n)=>delete n.topologyHelper,/helper/i));
test('missing helper result fails closed',()=>bad((b,n)=>delete n.topologyHelper.move,/move/i));
test('missing preview prepared receipt fails closed',()=>bad((b,n)=>delete n.topologyHelper.preview,/preview/i));
test('one ULP helper coordinate is rejected without rounding',()=>bad((b,n)=>n.topologyHelper.move.features[0].geometry.coordinates[0][1][0]+=Number.EPSILON,/move/i));
test('one ULP canonical boundary is rejected without polygon XOR tolerance',()=>bad((b,n)=>n.stages.confirm.state.document.entities[0].geometry.coordinates[0][1][0]+=Number.EPSILON,/geometry/));
test('wrong ordered owner is rejected',()=>bad((b,n)=>n.topologyHelper.preparation.handles[0].ownerIds.reverse(),/owner|handle/i));
test('wrong virtual ref is rejected',()=>bad((b,n)=>n.topologyHelper.preparation.handles[0].virtualRefs.push({featureId:'X'}),/ref|handle/i));
test('wrong parent is rejected',()=>bad((b,n)=>n.stages.confirm.state.document.entities[0].parentId='B',/parent|entities/i));
test('wrong reference target is rejected',()=>bad((b,n)=>n.stages.confirm.state.references[0].target='B',/reference/i));
test('deleted entity with stale hidden presentation fails',()=>bad((b,n)=>n.stages.confirm.state.presentation.hiddenItems.push({group:'countries',id:'ghost'}),/hidden|presentation/i));
test('supported label setting payload is not dropped',()=>bad((b,n)=>n.stages.confirm.state.presentation.labelSettings[0].pinned=true,/label/i));
test('wrong history outcome fails',()=>bad((b,n)=>n.stages.undo.outcome.ok=false,/undo/));
test('wrong native exact undo document fails',()=>bad((b,n)=>n.stages.undo.state.nativeCanonicalDocument.entities[0].properties.name='wrong',/undo/));
test('wrong web exact redo document fails',()=>bad((b,n)=>b.stages.redo.state.document.entities[0].properties.name='wrong',/redo/));
test('missing replay of original node fails',()=>bad((b,n)=>n.replayGestures=[],/replay/i));
test('substituting moved node for original node on replay fails',()=>bad((b,n)=>n.replayGestures[0].requestedNodeKey='1.25,0',/replay/i));
test('unexpected unobserved stage is rejected',()=>bad((b,n)=>n.stages.confirm={observed:false,reason:'No exact public native projection preimage within four ULPs for the source fixture coordinate.'},/confirm/));
test('caller cannot add arbitrary exclusions',()=>{const [b,n]=pair();delete n.stages.undo;const r=compareBoundaryObservations([b],{rows:[n]},{expectedCaseIds:['root-triple'],exclusions:[{case:'root-triple',stage:'undo'}]});assert.equal(r.passed,false);});
test('canonical ring start and orientation are exact representation mappings',()=>{const [b,n]=pair();for(const s of Object.values(n.stages))if(s.observed){for(const e of s.state.document.entities){const r=e.geometry.coordinates[0];r.reverse();}}const r=compareBoundaryCase(b,n);assert.equal(r.passed,true,JSON.stringify(r));});

test('removing both sides of a required successful stage cannot hide missing observation',()=>bad((b,n)=>{b.stages.undo=unobserved();n.stages.undo={observed:false,reason:'The actual native workflow did not reach this stage.'};},/undo/));
test('observed post-confirm selected IDs are compared',()=>bad((b,n)=>n.stages.confirm.selectionItems.push({domain:'territorial',id:'B'}),/selection/));
test('declared native observation limits may not be silently dropped',()=>bad((b,n)=>delete n.observationLimits.historyDepth,/limit/i));
test('returned move owner order is not sorted away',()=>bad((b,n)=>n.topologyHelper.move.movedOwnerIds.reverse(),/movedOwnerIds/));
test('helper selected owner raw refs cannot hide a foreign owner',()=>bad((b,n)=>n.topologyHelper.preparation.handles[0].rawRefs[0].featureId='ghost',/ref/i));

test('canonical geometry reference target cannot be hidden by flattened entity receipts',()=>bad((b,n)=>{n.stages.confirm.state.nativeCanonicalDocument.timelineRecords.geometryBindings[0].geometryRef.id='B';n.stages.redo.state.nativeCanonicalDocument.timelineRecords.geometryBindings[0].geometryRef.id='B';},/geometry|reference/i));
test('canonical parent relation cannot be hidden by flattened entity receipts',()=>bad((b,n)=>{n.stages.confirm.state.nativeCanonicalDocument.timelineRecords.parentRelations[0].parentId='B';n.stages.redo.state.nativeCanonicalDocument.timelineRecords.parentRelations[0].parentId='B';},/parent|timeline/i));
test('missing actual pending state is rejected',()=>bad((b,n)=>{n.stages.pending.edit.calculating=false;n.stages.pending.edit.boundaryStatus='ready';},/pending/));

test('wrong canonical label ref domain cannot hide behind flattened reference receipt',()=>bad((b,n)=>{for(const name of ['confirm','redo'])n.stages[name].state.nativeCanonicalDocument.presentation.webPresentation.labelSettings[0].ref.domain='generic';},/label|presentation|ref/i));
test('wrong canonical label ref presence cannot hide behind flattened reference receipt',()=>bad((b,n)=>{for(const name of ['confirm','redo'])n.stages[name].state.nativeCanonicalDocument.presentation.webPresentation.labelSettings=[];},/label|presentation|ref/i));

function descendantPair(){
 const [b,n]=pair();b.case=n.case=b.input.id=n.input.id='root-descendant-effects';
 const removed=['removed','removed-deep'];
 for(const side of [b,n])side.input.features.push(...removed.map(id=>feature(id)));
 for(const [name,ws] of Object.entries(b.stages))if(ws.observed){
  const ns=n.stages[name],before=!['confirm','redo','settled'].includes(name);
  if(before){
   for(const id of removed){const f=feature(id),e=entity(f);ws.state.document.entities.push(copy(e));ns.state.document.entities.push(copy(e));ns.state.nativeCanonicalDocument.entities.push(copy(e));
    for(const [native,document] of [[false,ws.state.document],[true,ns.state.nativeCanonicalDocument]]){document.timelineRecords.lifetimes.push({id:'lifetime:'+id,entityId:id,validFrom:null,validTo:null});document.timelineRecords.parentRelations.push({id:'parent:'+id,entityId:id,parentId:'',coverageMode:'explicit',validFrom:null,validTo:null});document.timelineRecords.geometryBindings.push({id:'geometry:'+id,entityId:id,validFrom:null,validTo:null,geometryRef:{id:(native?'':'territorial-geometry:')+id,version:1}});(native?document.geometries:document.geometryVersions).push({id:(native?'':'territorial-geometry:')+id,version:1,geojson:shape()});}
    if(name!=='undo'){ws.state.presentation.labelSettings['territorial:'+id]={visible:false};ns.state.presentation.labelSettings.push({id,pinned:false,collisionGroup:'map'});ns.state.references.push({id,kind:'label-settings',target:id});ns.state.nativeCanonicalDocument.presentation.webPresentation.labelSettings.push({ref:{domain:'territorial',id},pinned:false,collisionGroup:'map'});}
   }
  }
 }
 b.stages.preview.state.preview.removedIds=removed;b.stages.preview.state.preview.affectedIds.push(...removed);n.topologyHelper.preview.removedIds=removed;n.topologyHelper.preview.rows.push(...removed.map(id=>({id,before:shape(),after:null})));
 for(const stage of Object.values(n.stages))if(stage.observed)sealNativeState(stage.state);n.stages.undo.state.unchangedFromBefore=false;
 n.replayGestures[0].before=copy(n.stages.cancel);n.replayGestures[0].after=copy(n.stages.preview);
 return [b,n];
}
test('two precise descendant Undo cases require actual oracle label-reference omission and exact remaining canonical state',()=>{const [b,n]=descendantPair();const r=compareBoundaryCase(b,n);assert.equal(r.passed,true,JSON.stringify(r));});
test('descendant Undo mapping cannot erase an unrelated native document change',()=>{const [b,n]=descendantPair();n.stages.undo.state.nativeCanonicalDocument.entities[0].properties.name='corrupt';const r=compareBoundaryCase(b,n);assert.equal(r.passed,false);assert.match(JSON.stringify(r.differences),/undo/);});
function delayedPair(){
 const [b,n]=pair();b.case=n.case=b.input.id=n.input.id='root-stale-preview';for(const r of [b,n]){r.input.scenario='stale-preview';r.input.expected.confirm=false;}
 for(const name of ['preview','cancel','impactCancel','confirm','undo','redo']){b.stages[name]={observed:false,reason:'A real worker reply was rejected after cancellation or a stale session/revision.'};n.stages[name]={observed:false,reason:'The actual native workflow did not reach this stage.'};}
 b.stages.settled=copy(b.stages.before);b.stages.settled.outcome={ok:false};n.stages.settled=copy(n.stages.before);n.stages.settled.outcome={ok:false};n.stages.settled.edit={active:true,calculating:false,previewReady:false,boundaryStatus:'error'};b.stages.settled.preparation={status:'error'};
 b.stages.delayed=copy(b.stages.drag);b.stages.delayed.outcome={actualWorkerResultHeld:true};n.stages.delayed=copy(n.stages.drag);n.stages.delayed.edit.calculating=true;n.stages.delayed.outcome={workerCompleted:true};
 n.stages.settled.selectionItems=[{domain:'territorial',id:'B'}];n.stages.settled.selection={domain:'territorial',id:'B'};n.stages.settled.outcome.trigger='selection revision change before preview delivery';b.stages.settled.selection=copy(b.stages.pending.selection);
 n.observationLimits.completedWorkerOwnerDeliveryWithheld=true;n.deliveryBarrier={mechanism:'native-worker-completed-owner-delivery-withheld',timeoutMs:30000,completed:true,ownerEventsProcessed:false};n.replayGestures=[];
 return [b,n];
}
test('actual completed-worker owner-delivery barrier is accepted with browser RPC interception still false',()=>{const [b,n]=delayedPair();const r=compareBoundaryCase(b,n);assert.equal(r.passed,true,JSON.stringify(r));});
test('applicable native delayed stage cannot be omitted',()=>{const [b,n]=delayedPair();delete n.stages.delayed;delete n.deliveryBarrier;n.observationLimits.completedWorkerOwnerDeliveryWithheld=false;assert.equal(compareBoundaryCase(b,n).passed,false);});
test('timed-out completion barrier fails closed',()=>{const [b,n]=delayedPair();n.deliveryBarrier.completed=false;assert.equal(compareBoundaryCase(b,n).passed,false);});
test('processing owner events defeats withheld-delivery observation',()=>{const [b,n]=delayedPair();n.deliveryBarrier.ownerEventsProcessed=true;assert.equal(compareBoundaryCase(b,n).passed,false);});
test('delayed stage must preserve native canonical bytes',()=>{const [b,n]=delayedPair();n.stages.delayed.state.nativeCanonicalDocument.entities[0].properties.name='changed';assert.equal(compareBoundaryCase(b,n).passed,false);});
test('complete corpus envelope must identify the native workflow schema',()=>{const [b,n]=pair();const r=compareBoundaryObservations([b],{rows:[n]},{expectedCaseIds:['root-triple']});assert.equal(r.passed,false);assert.match(JSON.stringify(r.differences),/schema/i);});
test('valid native workflow envelope passes a explicitly bounded test corpus',()=>{const [b,n]=pair();assert.equal(compareBoundaryObservations([b],{schema:'pando-m974-native-boundary-workflows',version:1,rows:[n]},{expectedCaseIds:['root-triple']}).passed,true);});
test('legacy receipt must not claim rejected outcome while a preview survives',()=>{const [b,n]=delayedPair();n.stages.settled.edit={active:true,calculating:false,previewReady:true,boundaryStatus:'ready'};const r=compareBoundaryCase(b,n);assert.equal(r.passed,false);assert.match(JSON.stringify(r.differences),/settled|preview/i);});

const identityFixture=()=>{const expected={expectedCommit:'a'.repeat(40),expectedRunId:'123'};const identity={commit:expected.expectedCommit,runId:expected.expectedRunId};return {expected,suite:{...identity,identity:copy(identity)},browser:{identity:copy(identity)}};};
test('capture identity requires caller pinned commit and run',()=>{const {suite,browser}=identityFixture();assert.throws(()=>verifyBoundaryCaptureIdentity(suite,browser,{}),/commit|run/i);});
test('capture identity rejects an authenticated older commit',()=>{const {suite,browser,expected}=identityFixture();expected.expectedCommit='b'.repeat(40);assert.throws(()=>verifyBoundaryCaptureIdentity(suite,browser,expected),/commit/i);});
test('capture identity rejects a different run of the same commit',()=>{const {suite,browser,expected}=identityFixture();expected.expectedRunId='124';assert.throws(()=>verifyBoundaryCaptureIdentity(suite,browser,expected),/run/i);});
test('browser identity must independently match the expected commit',()=>{const {suite,browser,expected}=identityFixture();browser.identity.commit='b'.repeat(40);assert.throws(()=>verifyBoundaryCaptureIdentity(suite,browser,expected),/commit/i);});
test('suite top level cannot disagree with its identity',()=>{const {suite,browser,expected}=identityFixture();suite.identity.runId='124';assert.throws(()=>verifyBoundaryCaptureIdentity(suite,browser,expected),/run/i);});
test('supplied native commit must match the requested capture commit',()=>{const {suite,browser,expected}=identityFixture();assert.throws(()=>verifyBoundaryCaptureIdentity(suite,browser,{...expected,nativeCommit:'b'.repeat(40)}),/native.*commit/i);});
test('matching identities and well-formed supplied binary identity are accepted',()=>{const {suite,browser,expected}=identityFixture();assert.doesNotThrow(()=>verifyBoundaryCaptureIdentity(suite,browser,{...expected,nativeCommit:expected.expectedCommit,nativeBinarySha256:'c'.repeat(64)}));});
test('named CLI preserves caller supplied exact identities',()=>{assert.deepEqual(parseBoundaryCaptureArgs(['--browser-directory','browser','--native-file','native.json','--output-file','report.json','--expected-commit','a'.repeat(40),'--expected-run-id','123']),{browserDirectory:'browser',nativeFile:'native.json',outputFile:'report.json',expectedCommit:'a'.repeat(40),expectedRunId:'123'});});
test('CLI rejects positional, duplicate and unknown capture options',()=>{for(const args of [['browser','native'],['--native-file','a','--native-file','b'],['--ignore-mismatch','true']])assert.throws(()=>parseBoundaryCaptureArgs(args),/argument|option/i);});

test('entry primary selection is compared independently of ordered items',()=>bad((b,n)=>n.stages.pending.selection.id='A',/selection.*primary/i));
test('missing before-stage selection fails closed',()=>bad((b,n)=>delete n.stages.before.selectionItems,/selection/i));
test('before-stage selected item order is compared',()=>bad((b,n)=>n.stages.before.selectionItems.reverse(),/selection/i));

function absentSelectionPair(){
 const [b,n]=pair();b.case=n.case=b.input.id=n.input.id='mixed-domain-selection';
 b.input.expected.confirm=n.input.expected.confirm=false;b.entry.ok=n.entry.ok=false;
 delete b.preparation;delete b.gesture;delete n.gesture;delete n.topologyHelper;n.replayGestures=[];
 const absent={domain:'generic',id:'B',key:'generic:feature:B'},a={domain:'territorial',id:'A',key:'territorial:entity:A'};
 for(const name of Object.keys(b.stages)){
  if(!['before','cold','pending','settled'].includes(name)){b.stages[name]={observed:false,reason:'The production entrypoint rejected this selection.'};n.stages[name]=unobserved();n.stages[name].reason='The actual native workflow did not reach this stage.';continue;}
  const wb=copy(b.stages.before),nb=copy(n.stages.before);b.stages[name]=wb;n.stages[name]=nb;
  wb.selection={items:[a,absent],keys:[a.key,absent.key],primaryKey:a.key};nb.selectionItems=[{domain:'territorial',id:'A'}];nb.selection={domain:'territorial',id:'A'};
  wb.outcome=name==='before'?null:{ok:false};nb.outcome=name==='before'?{}:{ok:false};nb.edit={active:false};
 }
 return [b,n];
}
test('absent generic reference remains an explicit selection-only non-equivalent input',()=>{
 const [b,n]=absentSelectionPair(),r=compareBoundaryCase(b,n);assert.equal(r.passed,true,JSON.stringify(r));assert.ok(r.unobserved.some(x=>x.scope==='selection arrays for absent-object input references'));assert.equal(r.rawParity,false);
});
test('a valid generic object cannot use the absent-selection exception',()=>{
 const [b,n]=absentSelectionPair();b.input.genericFeatures=n.input.genericFeatures=[{id:'B'}];const r=compareBoundaryCase(b,n);assert.equal(r.passed,false);assert.match(JSON.stringify(r.differences),/valid generic selection/);
});
test('absent-selection exception cannot admit the operation or arbitrary refs',()=>{
 for(const field of ['entry','items']){const [b,n]=absentSelectionPair();if(field==='entry')n.entry.ok=true;else n.stages.pending.selectionItems.push({domain:'territorial',id:'B'});const r=compareBoundaryCase(b,n);assert.equal(r.passed,false);}
});

test('different stale stimuli are labeled and cannot hide the actual native selection action',()=>{const [b,n]=delayedPair();const r=compareBoundaryCase(b,n);assert.equal(r.passed,true,JSON.stringify(r));assert.ok(r.unobserved.some(x=>x.scope==='settled selection parity after different stale-response stimuli'));n.stages.settled.selectionItems=[{domain:'territorial',id:'A'}];n.stages.settled.selection={domain:'territorial',id:'A'};assert.equal(compareBoundaryCase(b,n).passed,false);});
test('stale selection exception requires the recorded real public action',()=>{const [b,n]=delayedPair();n.stages.settled.outcome.trigger='synthetic revision mutation';assert.equal(compareBoundaryCase(b,n).passed,false);});

test('canonical native bytes are mandatory and authenticated against the observed SHA',()=>{for(const mutate of [s=>delete s.canonicalBytesBase64,s=>s.canonicalBytesBase64+='!',s=>s.documentSha256='0'.repeat(64)])bad((b,n)=>mutate(n.stages.pending.state),/canonical|SHA|base64/i);});
test('native parsed canonical receipt cannot disagree with authenticated bytes',()=>bad((b,n)=>n.stages.pending.state.nativeCanonicalDocument.entities[0].properties.name='forged',/canonical|document/i));
test('unselected ghost raw refs cannot disappear through selected-owner filtering',()=>{
 for(const key of ['rawRefs','rawVirtualRefs'])bad((b,n)=>n.topologyHelper.preparation.handles[0][key].push({featureId:'ghost',polygonIndex:0,ringIndex:0,vertexIndex:0,segmentIndex:0,t:0.5}),/raw|ref|feature/i);
});
test('raw virtual reference indices and interpolation must be valid source addresses',()=>{
 for(const value of [{polygonIndex:99,t:.5},{segmentIndex:99,t:.5},{t:NaN},{t:2}])bad((b,n)=>n.topologyHelper.preparation.handles[0].rawVirtualRefs.push({featureId:'A',polygonIndex:0,ringIndex:0,segmentIndex:0,t:.5,...value}),/raw|ref|index|interpolation/i);
});
test('observed delayed selection cannot be omitted reordered or forged',()=>{
 for(const mutate of [s=>delete s.selectionItems,s=>s.selectionItems.reverse(),s=>s.selection={domain:'territorial',id:'ghost'}]){const [b,n]=delayedPair();mutate(n.stages.delayed);assert.equal(compareBoundaryCase(b,n).passed,false);}
});
function projectionGapPair(){
 const [b,n]=pair();b.case=n.case=b.input.id=n.input.id='root-uneven-multipolygon';n.inputObservations=[];n.replayGestures=[];
 for(const name of ['drag','preview','cancel','impactCancel','confirm','undo','redo'])n.stages[name]={observed:false,reason:name==='drag'?'No exact public native projection preimage within four ULPs for the source fixture coordinate.':'The actual native workflow did not reach this stage.'};
 n.stages.settled=copy(n.stages.prepared);n.stages.settled.outcome={ok:false,inputObserved:false};return [b,n];
}
test('projection-gap settled selection is preserved from actual preparation',()=>{const [b,n]=projectionGapPair();assert.equal(compareBoundaryCase(b,n).passed,true,JSON.stringify(compareBoundaryCase(b,n)));for(const mutate of [s=>delete s.selectionItems,s=>s.selectionItems.reverse(),s=>s.selection={domain:'territorial',id:'ghost'}]){const [w,a]=projectionGapPair();mutate(a.stages.settled);assert.equal(compareBoundaryCase(w,a).passed,false);}});
test('replay snapshots authenticate bytes history and selection for both sides of replay',()=>{
 for(const side of ['before','after'])for(const mutate of [s=>s.state.canonicalBytesBase64='e30=',s=>s.state.history={canUndo:true,canRedo:true},s=>{s.selectionItems=[{domain:'territorial',id:'ghost'}];s.selection={domain:'territorial',id:'ghost'};}])bad((b,n)=>mutate(n.replayGestures[0][side]),/replay|canonical|history|selection/i);
});
test('replay must retain exact initial bytes and explicit unchanged status',()=>{
 for(const side of ['before','after'])for(const mutate of [s=>s.unchangedFromBefore=false,s=>{const bytes=Buffer.concat([Buffer.from(s.canonicalBytesBase64,'base64'),Buffer.from(' ')]);s.canonicalBytesBase64=bytes.toString('base64');s.documentSha256=createHash('sha256').update(bytes).digest('hex');}])bad((b,n)=>mutate(n.replayGestures[0][side].state),/replay|bytes|unchanged/i);
});

test('non-equivalent legacy selection reports actual successful preview rather than requiring stale-project rejection',()=>{
 const [b,n]=delayedPair();b.stages.settled.preparation={status:'ready'};n.stages.settled.edit={active:true,calculating:false,previewReady:true,boundaryStatus:'ready'};n.stages.settled.outcome.ok=true;
 const r=compareBoundaryCase(b,n);assert.equal(r.passed,false);assert.ok(r.unobserved.some(x=>x.scope==='settled selection parity after different stale-response stimuli'));
 assert.deepEqual(r.differences,[{field:'settled.outcome.ok',web:false,native:true},{field:'settled.nonEquivalentStimuli.previewReady',web:false,native:true}]);
});
test('legacy native outcome cannot lie about a surviving preview',()=>{
 const [b,n]=delayedPair();b.stages.settled.preparation={status:'ready'};n.stages.settled.edit={active:true,calculating:false,previewReady:true,boundaryStatus:'ready'};
 const r=compareBoundaryCase(b,n);assert.ok(r.differences.some(d=>/outcome.*actual|actual.*outcome/.test(d.error||'')));
});

test('legacy selection-only settlement preserves exact authenticated bytes and unchanged flag',()=>{
 for(const mutate of [s=>s.unchangedFromBefore=false,s=>{const bytes=Buffer.concat([Buffer.from(s.canonicalBytesBase64,'base64'),Buffer.from(' ')]);s.canonicalBytesBase64=bytes.toString('base64');s.documentSha256=createHash('sha256').update(bytes).digest('hex');s.unchangedFromBefore=false;}]){
  const [b,n]=delayedPair();mutate(n.stages.settled.state);const result=compareBoundaryCase(b,n);assert.equal(result.passed,false);assert.ok(result.differences.some(d=>/settled/.test(d.field)));
 }
});
