import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import fs from 'node:fs';
const mod=await import('./m974-native-supplemental.mjs').catch(error=>{if(error.code!=='ERR_MODULE_NOT_FOUND')throw error;return {};});
test('supplemental gate requires all exact ordered case identities and missing native values fail',()=>{
 assert.equal(typeof mod.compareSupplementalObservations,'function','Supplemental native comparator must exist');
 const result=mod.compareSupplementalObservations([],{schema:'pando-m974-native-boundary-workflows',version:1,rows:[]});
 assert.equal(result.passed,false);assert.equal(result.rawParity,false);
});
test('supplemental gate requires explicit commit/run and rejects caller-defined exclusions',()=>{
 assert.equal(typeof mod.parseArguments,'function');
 assert.throws(()=>mod.parseArguments(['--browser-directory','x','--native-probe','p','--output','o']));
 assert.throws(()=>mod.parseArguments(['--exclusions','anything']));
});

// Independent protocol-only receipts. These are never persisted as native or browser captures.
function sealCanonical(stage){const bytes=Buffer.from(JSON.stringify(stage.state.nativeCanonicalDocument));stage.state.canonicalBytesBase64=bytes.toString('base64');stage.state.documentSha256=createHash('sha256').update(bytes).digest('hex');}
function mutateAllCanonical(p,change){for(const stage of Object.values(p.native.stages))if(stage.observed){change(stage.state.nativeCanonicalDocument);sealCanonical(stage);}}
function pair(){
 const delta=4e-10,properties=id=>({schemaVersion:5,entityKind:'general',name:id,parentId:'',coverageMode:'explicit',style:{},locked:false,validFrom:null,validTo:null,notes:'',metadata:{},sourceFolderId:'',sourceLibraryId:'',sourceGeometryVersion:''});
 const feature=(id,ring)=>({type:'Feature',id,properties:properties(id),geometry:{type:'Polygon',coordinates:[ring]}}),features=[feature('A',[[0,0],[1,0],[1,1],[1,2],[0,2],[0,0]]),feature('B',[[1,0],[2,0],[2,2],[1,2],[1+delta,1],[1,0]])];
 const input={id:'supp-v1-owner-already-destination-4e-10',features,selectedIds:['A','B'],seedId:'A',inspectCoordinate:[1,1],move:{nodeKey:'1,1',coordinate:[1+delta,1]},expectedPath:'root',observationOnly:true};
 const entity=f=>({id:f.id,parentId:'',coverageMode:'explicit',entityKind:'general',properties:structuredClone(f.properties),geometry:structuredClone(f.geometry)}),entities=features.map(entity);
 const refs=(a,b)=>[{featureId:'A',polygonIndex:0,ringIndex:0,vertexIndex:a},{featureId:'B',polygonIndex:0,ringIndex:0,vertexIndex:b}];
 const handles=[0,1,2].map((y,index)=>({nodeKey:`1,${y}`,coordinate:[1,y],polygonIndex:0,ringIndex:0,index:index+1,refs:refs(index+1,[0,4,3][index]),virtualRefs:[],ownerIds:['A','B'],fixed:false,segments:[]}));
 const segments=[{key:'1,0|1,1',start:[1,0],end:[1,1]},{key:'1,1|1,2',start:[1,1],end:[1,2]}];
 const nativeCanonicalDocument={units:features.map(f=>({id:f.id,baseName:f.properties.name,name:f.properties.name,nameExplicit:true,kind:f.properties.entityKind,libraryOrigin:null,locked:f.properties.locked,metadata:f.properties.metadata,notes:f.properties.notes,sourceFolderId:'',sourceLibraryId:'',sourceGeometryVersion:''})),geometries:features.map(f=>({id:f.id,version:1,geojson:structuredClone(f.geometry)})),timelineRecords:{schemaVersion:1,lifetimes:features.map(f=>({id:'lifetime:'+f.id,entityId:f.id,validFrom:null,validTo:null})),geometryBindings:features.map(f=>({id:'geometry:'+f.id,entityId:f.id,validFrom:null,validTo:null,geometryRef:{id:f.id,version:1}})),parentRelations:features.map(f=>({id:'parent:'+f.id,entityId:f.id,validFrom:null,validTo:null,parentId:'',coverageMode:'explicit'}))},content:Object.fromEntries(['distributionEntries','genericFeatures','hydro','labels','symbols','countryDetails'].map(k=>[k,[]]))};
 const web={case:input.id,input:structuredClone(input),inspect:{nodeKey:'1,1'},topology:{segments:segments.map(s=>({...s,ownerIds:['A','B']}))},directPreparation:{ok:true,result:{valid:true,selectedIds:['A','B'],handles:structuredClone(handles),segments:structuredClone(segments)}},directMove:{ok:true,result:{affectedIds:['A','B'],features:structuredClone(features)}},workflow:{entry:{ok:true},gesture:{ok:true},stages:{},visualEvents:[]}};
 const native={case:input.id,input:structuredClone(input),entry:{ok:true},gesture:{ok:true,picked:true},stages:{},events:[{calculating:true,boundaryStatus:'preparing'}],inputObservations:[{exact:true,intended:[1+delta,1],inverse:[1+delta,1],maximumUlpRadius:4,xUlpSteps:0,yUlpSteps:0}],replayGestures:[],observationLimits:{browserPointerProjection:false,nativeRawPreviewGeometry:false,workerResultInterception:false,labelSettingVisible:false,historyDepth:false,completedWorkerOwnerDeliveryWithheld:false},topologyHelper:{observed:true,scope:'topology-and-receipt-helper-only',inputOwnerIds:['A','B'],selectedIdsInInputOrder:['A','B'],inspect:{coordinate:[1,1],nodeKey:'1,1',quantizedKey:[1,1]},preparation:{valid:true,handles:handles.map(h=>({...structuredClone(h),rawRefs:structuredClone(h.refs),rawVirtualRefs:[],quantizedKey:[...h.coordinate]})),segments:segments.map((s,i)=>({...structuredClone(s),ownerIds:['A','B'],startNodeIndex:i,endNodeIndex:i+1}))},gesture:{ok:true},move:{changed:false,coordinate:[1+delta,1],features:[],movedOwnerIds:[]}}};
 for(const name of ['before','cold','pending','prepared','drag','preview','cancel','impactCancel','confirm','undo','redo','settled']){
  const observed=['before','cold','pending','prepared','drag','settled'].includes(name);if(!observed){web.workflow.stages[name]={observed:false,reason:'No canonical preview was created.'};native.stages[name]={observed:false,reason:'The actual native workflow did not reach this stage.'};continue;}
  const primary=['before','cold'].includes(name)?'A':'B',items=features.map(f=>({domain:'territorial',id:f.id,key:'territorial:entity:'+f.id})),outcome=name==='drag'?{ok:true,changed:false,coordinate:[1+delta,1]}:name==='settled'?{ok:false}:['pending','prepared'].includes(name)?{ok:true}:{};
  web.workflow.stages[name]={observed:true,outcome:structuredClone(outcome),selection:{items,primaryKey:'territorial:entity:'+primary},state:{document:{entities:structuredClone(entities),distributionEntries:[]},history:{undo:0,redo:0}}};
  native.stages[name]={observed:true,outcome:structuredClone(outcome),selection:{domain:'territorial',id:primary},selectionItems:items.map(({domain,id})=>({domain,id})),edit:['before','cold'].includes(name)?{active:false}:{active:true,calculating:name==='pending',boundaryStatus:name==='pending'?'preparing':'ready',previewReady:false,error:''},state:{document:{entities:structuredClone(entities),distributionEntries:[]},references:[],nativeCanonicalDocument:structuredClone(nativeCanonicalDocument),presentation:{hiddenItems:[],objectStyles:[],labelSettings:[]},history:{canUndo:false,canRedo:false},unchangedFromBefore:true}};
 }
 native.stages.cold.outcome={canEnter:true};
 for(const stage of Object.values(native.stages))if(stage.observed)sealCanonical(stage);
 return {web,native};
}
test('a matching UI no-op passes while direct-worker-only movement remains explicitly unproven',()=>{
 const {web,native}=pair(),result=mod.compareSupplementalCase(web,native);assert.equal(result.passed,true,JSON.stringify(result));assert.equal(result.rawParity,false);assert.equal(result.compared.move,false);assert.ok(result.unobserved.some(row=>row.scope==='direct-worker-only movement'));
});
for(const [name,mutate]of [
 ['missing native case',p=>{p.native=null;}],
 ['missing stage',p=>{delete p.native.stages.drag;}],
 ['missing helper',p=>{delete p.native.topologyHelper;}],
 ['missing inspect numeric key',p=>{delete p.native.topologyHelper.inspect;}],
 ['wrong quantized key',p=>{p.native.topologyHelper.inspect.quantizedKey[0]+=1e-7;}],
 ['node-owner order',p=>{p.native.topologyHelper.preparation.handles[1].ownerIds.reverse();}],
 ['virtual ref omission',p=>{delete p.native.topologyHelper.preparation.handles[1].virtualRefs;}],
 ['one-ULP handle coordinate',p=>{p.native.topologyHelper.preparation.handles[1].coordinate[0]+=Number.EPSILON;}],
 ['selection primary mismatch',p=>{p.native.stages.pending.selection.id='A';}],
 ['canonical hidden mutation',p=>{p.native.stages.settled.state.nativeCanonicalDocument.units.push({id:'ghost'});}],
 ['unexpected history',p=>{p.native.stages.settled.state.history.canUndo=true;}],
 ['false input equivalence',p=>{p.native.inputObservations[0].inverse[0]+=Number.EPSILON;}],
 ['arbitrary projection exclusion',p=>{p.native.stages.drag={observed:false,reason:'No exact public native projection preimage within four ULPs for the source fixture coordinate.'};}],
 ['promoted direct-worker move',p=>{p.native.topologyHelper.move.changed=true;}],
 ['omitted observation limit',p=>{delete p.native.observationLimits.nativeRawPreviewGeometry;}],
])test('rejects '+name,()=>{const p=pair();mutate(p);const result=mod.compareSupplementalCase(p.web,p.native);assert.equal(result.passed,false,JSON.stringify(result));});
test('authenticated supplemental inputs reject stale identity, corrupt archives and changed exports',async()=>{
 const fs=await import('node:fs'),path=await import('node:path'),{tmpdir}=await import('node:os'),{createHash}=await import('node:crypto'),{gzipSync}=await import('node:zlib');
 const {loadNodeSources}=await import('./m974-snap-boundary/sources.mjs'),{createSupplementalSuite,runSupplementalCase}=await import('./m974-snap-boundary/supplemental.mjs'),{runBoundaryCase}=await import('./m974-snap-boundary/boundary-runtime.mjs'),{runtimePin}=await import('./m974-snap-boundary/protocol.mjs');
 const root=fs.mkdtempSync(path.join(tmpdir(),'m974-supplemental-gate-test-')),loaded=await loadNodeSources(),sha=value=>createHash('sha256').update(value).digest('hex');
 try{
  const suite=await createSupplementalSuite({commit:'a'.repeat(40),runId:'123'});loaded.runBoundaryCase=runBoundaryCase;const cases=[];for(const definition of suite.cases)cases.push(await runSupplementalCase(loaded,definition));
  // These runtime labels are synthetic protocol test tags, never browser evidence.
  const report={schema:'pando-m974-actual-chromium-supplemental',version:1,state:'complete',identity:suite.identity,sourceHashes:Object.fromEntries(suite.base.manifest.sources.map(row=>[row.path,row.sha256])),runtime:{playwright:runtimePin.playwright,browserVersion:runtimePin.chromium,chromiumRevision:runtimePin.revision,cdp:{product:'HeadlessChrome/'+runtimePin.chromium,jsVersion:runtimePin.v8}},cases,observationLimits:{rawParity:false,geographicOnly:true,originalCorpusUnchanged:true,UIAndDirectWorkerAreSeparate:true}};
  const suiteText=JSON.stringify(suite),text=JSON.stringify(report),chunkCharacters=256*1024,files={
   'suite.json.gz':gzipSync(suiteText),'suite-pin.json':JSON.stringify({identity:suite.identity,decompressedSha256:sha(suiteText),decompressedBytes:Buffer.byteLength(suiteText)}),
   'browser-report.json':text,'browser-report.json.gz':gzipSync(text),'report-transfer.json':JSON.stringify({characters:text.length,bytes:Buffer.byteLength(text),sha256:sha(text),chunkCharacters,chunks:Math.ceil(text.length/chunkCharacters)}),
   'supplemental-observations.json':JSON.stringify(cases),'capture-verification.json':JSON.stringify({identity:suite.identity,passed:true,cases:12,reportDecompressedSha256:sha(text),rawParity:false}),
  };
  const reset=()=>{for(const[name,value]of Object.entries(files))fs.writeFileSync(path.join(root,name),value);};reset();const options={browserDirectory:root,expectedCommit:'a'.repeat(40),expectedRunId:'123'};
  assert.equal(mod.readCapture(options).report.cases.length,12);
  assert.throws(()=>mod.readCapture({...options,expectedCommit:'b'.repeat(40)}));assert.throws(()=>mod.readCapture({...options,expectedRunId:'124'}));
  for(const[name,changed]of [['browser-report.json',text+' '],['browser-report.json.gz',gzipSync('{}')],['supplemental-observations.json','[]'],['suite-pin.json',JSON.stringify({identity:suite.identity,decompressedSha256:'0'.repeat(64),decompressedBytes:Buffer.byteLength(suiteText)})],['capture-verification.json',JSON.stringify({identity:suite.identity,passed:false,cases:12,reportDecompressedSha256:sha(text),rawParity:false})]]){reset();fs.writeFileSync(path.join(root,name),changed);assert.throws(()=>mod.readCapture(options),undefined,name);}
  reset();fs.unlinkSync(path.join(root,'report-transfer.json'));assert.throws(()=>mod.readCapture(options));
 }finally{loaded.cleanup();fs.rmSync(root,{recursive:true,force:true});}
});
function movementPair(){
 const p=pair(),id='supp-v1-diagonal-epsilon-below',destination=[1.2,1];p.web.case=p.web.input.id=p.native.case=p.native.input.id=id;p.web.input.move.coordinate=[...destination];p.native.input.move.coordinate=[...destination];
 const moved=structuredClone(p.web.input.features);moved[0].geometry.coordinates[0][2]=[...destination];moved[1].geometry.coordinates[0][4]=[...destination];
 p.web.directMove.result={affectedIds:['A','B'],features:structuredClone(moved)};p.web.workflow.stages.drag.outcome={ok:true,changed:true,coordinate:[...destination]};
 p.web.workflow.visualEvents=[{kind:'move',value:[{start:[1,0],end:[...destination]},{start:[...destination],end:[1,2]}]}];
 p.web.workflow.diagnostics=[{kind:'error',code:'PL-TERRITORIAL-EDIT',message:'국경 조정으로 선택 국가의 바깥 경계를 변경할 수 없습니다.'}];
 p.native.topologyHelper.move={changed:true,coordinate:[...destination],movedOwnerIds:['A','B'],features:moved.map(({id,geometry})=>({id,geometry}))};p.native.topologyHelper.activeSegments=structuredClone(p.web.workflow.visualEvents[0].value);p.native.topologyHelper.preview={ok:false,error:'BOUNDARY_OUTER_UNION_CHANGED'};
 p.native.stages.drag={observed:false,reason:'No exact public native projection preimage within four ULPs for the source fixture coordinate.'};p.native.stages.settled.outcome={ok:false,inputObserved:false};p.native.inputObservations=[];return p;
}
test('exact helper movement is separate from an explicitly unavailable controller input',()=>{
 const p=movementPair(),result=mod.compareSupplementalCase(p.web,p.native);assert.equal(result.passed,true,JSON.stringify(result));assert.equal(result.compared.move,true);assert.equal(result.compared.controllerStages,4);assert.ok(result.unobserved.some(row=>row.scope.includes('controller geographic input')));
});
for(const [name,mutate]of [
 ['one-ULP moved coordinate',p=>{p.native.topologyHelper.move.features[0].geometry.coordinates[0][2][0]+=Number.EPSILON;}],
 ['moved owner order',p=>{p.native.topologyHelper.move.movedOwnerIds.reverse();}],
 ['missing helper preview receipt',p=>{delete p.native.topologyHelper.preview;}],
 ['incorrect native rejection class',p=>{p.native.topologyHelper.preview.error='UNRELATED_ERROR';}],
 ['omitted input-gap receipt',p=>{delete p.native.stages.settled.outcome.inputObserved;}],
 ['fabricated controller input',p=>{p.native.inputObservations=[{exact:true}];}],
 ['hidden controller publication',p=>{p.native.stages.settled.state.nativeCanonicalDocument.units.push({id:'ghost'});}],
])test('rejects '+name,()=>{const p=movementPair();mutate(p);assert.equal(mod.compareSupplementalCase(p.web,p.native).passed,false);});
for(const [name,mutate]of [
 ['missing initial edit',p=>{delete p.native.stages.before.edit;}],
 ['missing cold edit',p=>{delete p.native.stages.cold.edit;}],
 ['inactive prepared editor',p=>{p.native.stages.prepared.edit.active=false;}],
 ['pending preview readiness',p=>{p.native.stages.pending.edit.previewReady=true;}],
 ['prepared preview readiness',p=>{p.native.stages.prepared.edit.previewReady=true;}],
 ['rejected entry capability',p=>{p.native.stages.cold.outcome.canEnter=false;}],
 ['still calculating after settlement',p=>{p.native.stages.settled.edit.calculating=true;}],
 ['unrelated controller error',p=>{p.native.stages.settled.edit.error='UNRELATED_ERROR';}],
 ['ghost raw ref',p=>{p.native.topologyHelper.preparation.handles[0].rawRefs.push({featureId:'ghost',polygonIndex:0,ringIndex:0,vertexIndex:0});}],
 ['ghost raw virtual ref',p=>{p.native.topologyHelper.preparation.handles[0].rawVirtualRefs.push({featureId:'ghost',polygonIndex:0,ringIndex:0,vertexIndex:0,t:0.5});}],
 ['out of bounds raw ref',p=>{p.native.topologyHelper.preparation.handles[0].rawRefs.push({featureId:'A',polygonIndex:0,ringIndex:0,vertexIndex:99});}],
 ['missing exact canonical bytes',p=>{delete p.native.stages.before.state.canonicalBytesBase64;}],
 ['constant unauthenticated digest',p=>{for(const s of Object.values(p.native.stages))if(s.observed)s.state.documentSha256='a'.repeat(64);}],
 ['canonical unit metadata omission',p=>{mutateAllCanonical(p,d=>{delete d.units[0].name;});}],
 ['canonical unused geometry',p=>{mutateAllCanonical(p,d=>{d.geometries.push({id:'ghost',version:1,geojson:p.web.input.features[0].geometry});});}],
 ['canonical lifetime omission',p=>{mutateAllCanonical(p,d=>{d.timelineRecords.lifetimes.pop();});}],
])test('rejects reviewed '+name,()=>{const p=pair();mutate(p);assert.equal(mod.compareSupplementalCase(p.web,p.native).passed,false);});
test('rejects missing settled editor during declared projection gap',()=>{const p=movementPair();delete p.native.stages.settled.edit;assert.equal(mod.compareSupplementalCase(p.web,p.native).passed,false);});
test('attests the imported exact boundary comparator dependency',()=>{assert.deepEqual(Object.keys(mod.comparatorFingerprints()),['m974-native-supplemental.mjs','m97/split-differential.mjs']);for(const [file,sha]of Object.entries(mod.comparatorFingerprints()))assert.equal(sha,createHash('sha256').update(fs.readFileSync(new URL(file,import.meta.url))).digest('hex'));});
test('validates virtual refs with their segmentIndex field',()=>{const p=pair(),ref={featureId:'A',polygonIndex:0,ringIndex:0,segmentIndex:1,t:1};p.web.directPreparation.result.handles[1].virtualRefs=[ref];p.native.topologyHelper.preparation.handles[1].virtualRefs=[structuredClone(ref)];p.native.topologyHelper.preparation.handles[1].rawVirtualRefs=[structuredClone(ref)];assert.equal(mod.compareSupplementalCase(p.web,p.native).passed,true);});
for(const [name,mutate]of [
 ['omitted settled selection items',p=>{delete p.native.stages.settled.selectionItems;}],
 ['reordered settled selection items',p=>{p.native.stages.settled.selectionItems.reverse();}],
 ['ghost settled primary',p=>{p.native.stages.settled.selection.id='ghost';}],
 ['omitted settled primary',p=>{delete p.native.stages.settled.selection;}],
])test('projection gap rejects '+name,()=>{const p=movementPair();mutate(p);assert.equal(mod.compareSupplementalCase(p.web,p.native).passed,false);});
test('projection-gap selection invariance uses native prepared state while cross-engine settlement remains excluded',()=>{const p=movementPair();assert.notDeepEqual(p.native.stages.before.selection,p.native.stages.prepared.selection);p.web.workflow.stages.settled.selection.primaryKey='territorial:entity:A';assert.equal(mod.compareSupplementalCase(p.web,p.native).passed,true);});
