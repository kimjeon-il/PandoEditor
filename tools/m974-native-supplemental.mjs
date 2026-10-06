/** Fresh-browser supplemental gate. No golden values or browser outputs enter the native probe. */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {gunzipSync} from 'node:zlib';
import {spawnSync} from 'node:child_process';
import {isDeepStrictEqual as equal} from 'node:util';
import {fileURLToPath} from 'node:url';
import {readSupplementalInputs,verifySupplementalSuite,verifySupplementalReport} from './m974-snap-boundary/supplemental.mjs';
import {exactGeometryBoundary} from './m97/split-differential.mjs';

const STAGES=['before','cold','pending','prepared','drag','preview','cancel','impactCancel','confirm','undo','redo','settled'];
const GAPS=new Set(['supp-v1-diagonal-epsilon-below','supp-v1-diagonal-epsilon-at','supp-v1-diagonal-epsilon-negative-at','supp-v1-half-grid-negative-below','supp-v1-half-grid-negative-at','supp-v1-half-grid-negative-above']);
const INPUT_REASON='No exact public native projection preimage within four ULPs for the source fixture coordinate.';
const NO_STAGE='The actual native workflow did not reach this stage.';
const NO_DIRECT_PEER='supp-v1-owner-already-destination-4e-10';
const digest=data=>createHash('sha256').update(data).digest('hex');
const object=(value,name)=>{assert.ok(value&&typeof value==='object'&&!Array.isArray(value),'missing '+name);return value;};
const list=(value,name)=>{assert.ok(Array.isArray(value),'missing '+name);return value;};
const required=(value,key)=>{assert.ok(value&&Object.hasOwn(value,key),'missing '+key);return value[key];};
const pick=(value,keys)=>Object.fromEntries(keys.map(key=>[key,required(value,key)]));
const rawFeatures=rows=>list(rows,'features').map(row=>pick(row,['id','geometry']));
const ids=()=>readSupplementalInputs().cases.map(row=>row.id);
function entity(feature){return {id:feature.id,parentId:feature.properties.parentId,coverageMode:feature.properties.coverageMode,entityKind:feature.properties.entityKind,properties:feature.properties,geometry:feature.geometry};}
function validateCanonicalInitial(stage,input){
 const state=object(stage.state,'native state'),document=object(state.nativeCanonicalDocument,'native canonical document'),entities=list(state.document?.entities,'native entity observations');
 assert.deepEqual(entities,input.features.map(entity),'native canonical source fields/coordinates');assert.deepEqual(state.document.distributionEntries,[]);assert.deepEqual(state.references,[]);
 const encoded=required(state,'canonicalBytesBase64');assert.equal(typeof encoded,'string');const bytes=Buffer.from(encoded,'base64');assert.ok(bytes.length>0);assert.equal(bytes.toString('base64'),encoded,'noncanonical base64');assert.equal(digest(bytes),state.documentSha256,'canonical document byte hash');assert.deepEqual(JSON.parse(bytes.toString('utf8')),document,'canonical bytes/document disagree');
 assert.deepEqual(document.units,input.features.map(({id,properties:p})=>({id,baseName:p.name,name:p.name,nameExplicit:true,kind:p.entityKind,libraryOrigin:null,locked:p.locked,metadata:p.metadata,notes:p.notes,sourceFolderId:p.sourceFolderId,sourceEntityId:p.sourceLibraryId,sourceGeometryVersion:p.sourceGeometryVersion})),'native complete unit metadata');
 assert.deepEqual(document.geometries,input.features.map(feature=>({id:feature.id,version:1,geojson:feature.geometry})),'native complete geometry collection');
 const timeline=object(document.timelineRecords,'native timeline');
 const validity=f=>({validFrom:f.properties.validFrom,validTo:f.properties.validTo});
 assert.deepEqual(timeline,{schemaVersion:1,lifetimes:input.features.map(f=>({id:'lifetime:'+f.id,entityId:f.id,...validity(f)})),geometryBindings:input.features.map(f=>({id:'geometry:'+f.id,entityId:f.id,...validity(f),geometryRef:{id:f.id,version:1}})),parentRelations:input.features.map(f=>({id:'parent:'+f.id,entityId:f.id,...validity(f),parentId:f.properties.parentId,coverageMode:f.properties.coverageMode}))},'native complete timeline metadata');
 for(const feature of input.features){
  const bindings=timeline.geometryBindings.filter(row=>row.entityId===feature.id);assert.equal(bindings.length,1);assert.equal(bindings[0].geometryRef.id,feature.id);assert.ok(Number.isInteger(bindings[0].geometryRef.version));
  const versions=document.geometries.filter(row=>row.id===bindings[0].geometryRef.id&&row.version===bindings[0].geometryRef.version);assert.equal(versions.length,1);assert.deepEqual(versions[0].geojson,feature.geometry);
  const relations=timeline.parentRelations.filter(row=>row.entityId===feature.id);assert.equal(relations.length,1);assert.equal(relations[0].parentId,feature.properties.parentId);assert.equal(relations[0].coverageMode,feature.properties.coverageMode);
 }
 for(const key of ['distributionEntries','genericFeatures','hydro','labels','symbols','countryDetails'])assert.deepEqual(document.content[key],[],'unexpected native canonical '+key);
 assert.deepEqual(state.presentation,{hiddenItems:[],objectStyles:[],labelSettings:[]});assert.match(state.documentSha256,/^[a-f0-9]{64}$/);assert.equal(state.unchangedFromBefore,true);assert.deepEqual(state.history,{canUndo:false,canRedo:false});
}
function validateEditor(stage,name,web){
 const edit=object(stage.edit,'native '+name+' editor');
 if(['before','cold'].includes(name)){assert.deepEqual(edit,{active:false});if(name==='cold')assert.equal(stage.outcome.canEnter,true);return;}
 assert.equal(edit.active,true);assert.equal(edit.calculating,name==='pending');assert.equal(edit.previewReady,false);
 const valid=web.workflow.stages.prepared.outcome.ok;
 assert.equal(edit.boundaryStatus,name==='pending'?'preparing':valid?'ready':'error');
 const error=name==='pending'?'':!valid?'BOUNDARY_ISOLATED_OWNER':name==='settled'&&web.case==='supp-v1-owner-already-destination-4e-8'?'BOUNDARY_OUTER_UNION_CHANGED':'';
 assert.equal(edit.error,error,'native controller error class');
}
function validateRawRef(ref,input,virtual){
 const feature=input.features.find(row=>row.id===ref.featureId);assert.ok(feature,'raw ref owner absent from source input');
 const indexKey=virtual?'segmentIndex':'vertexIndex';for(const key of ['polygonIndex','ringIndex',indexKey])assert.ok(Number.isInteger(ref[key])&&ref[key]>=0,'invalid raw ref '+key);
 const polygons=feature.geometry.type==='Polygon'?[feature.geometry.coordinates]:feature.geometry.coordinates;
 const ring=polygons[ref.polygonIndex]?.[ref.ringIndex];assert.ok(Array.isArray(ring)&&ref[indexKey]<ring.length-1,'out of bounds raw ref');
 if(virtual)assert.ok(Number.isFinite(ref.t)&&ref.t>=0&&ref.t<=1,'invalid raw virtual ref t');
}
function selection(stage,native){
 const rows=native?stage.selectionItems:stage.selection?.items;const items=list(rows,'selection items').map(row=>pick(row,['domain','id']));
 const primary=native?stage.selection:rows.find(row=>row.key===stage.selection.primaryKey);return {items,primary:primary?pick(primary,['domain','id']):null};
}
export function compareSupplementalCase(web,native){
 const differences=[],unobserved=[],compared={numericKey:false,preparation:false,move:false,preview:false,controllerStages:0};
 const check=(field,fn)=>{try{fn();}catch(error){differences.push({field,error:error.message});}};
 const same=(field,a,b)=>{if(!equal(a,b))differences.push({field,web:a,native:b});};
 if(!web||!native)return {case:web?.case,passed:false,rawParity:false,differences:[{field:'case',error:'missing native/browser case'}],unobserved,compared};
 const workflow=web.workflow;let gap=false;
 check('identity',()=>{assert.equal(native.case,web.case);assert.equal(native.error,undefined,'native probe error');assert.deepEqual(native.input,web.input);assert.equal(typeof native.entry?.ok,'boolean');same('entry',workflow.entry.ok,native.entry.ok);});
 check('native.observation-limits',()=>{for(const key of ['browserPointerProjection','nativeRawPreviewGeometry','workerResultInterception','labelSettingVisible','historyDepth'])assert.equal(native.observationLimits?.[key],false,'missing/overstated '+key);assert.equal(native.observationLimits.completedWorkerOwnerDeliveryWithheld,false);});
 check('stages',()=>{
  for(const name of STAGES){const stage=object(native.stages?.[name],'native '+name);assert.equal(typeof stage.observed,'boolean');if(!stage.observed)assert.equal(typeof stage.reason,'string');}
  for(const name of Object.keys(native.stages))assert.ok(STAGES.includes(name)||name==='delayed','unexpected native stage');
  if(native.stages.delayed){assert.equal(native.stages.delayed.observed,false);assert.equal(native.stages.delayed.reason,NO_STAGE);}
  if(native.stages.drag.observed===false&&native.stages.drag.reason===INPUT_REASON){assert.ok(GAPS.has(web.case),'undeclared native projection exclusion');gap=true;assert.deepEqual(native.inputObservations,[]);assert.equal(native.gesture?.ok,true);assert.equal(native.stages.settled.outcome.inputObserved,false);assert.equal(native.stages.settled.outcome.ok,false);for(const name of ['preview','cancel','impactCancel','confirm','undo','redo']){assert.equal(native.stages[name].observed,false);assert.equal(native.stages[name].reason,NO_STAGE);}unobserved.push({case:web.case,scope:'controller geographic input and downstream lifecycle',reason:INPUT_REASON});}
  else if(native.stages.drag.observed===false)assert.equal(native.stages.drag.reason,NO_STAGE,'undeclared native missing drag');
 });
 check('controller.initial-readiness',()=>{
  const before=object(native.stages.before,'native initial stage');assert.equal(before.observed,true);validateCanonicalInitial(before,web.input);
  for(const name of ['before','cold','pending','prepared']){
   const w=workflow.stages[name],n=native.stages[name];assert.equal(w?.observed,true);assert.equal(n?.observed,true);validateCanonicalInitial(n,web.input);validateEditor(n,name,web);assert.deepEqual(n.state.nativeCanonicalDocument,before.state.nativeCanonicalDocument);assert.equal(n.state.documentSha256,before.state.documentSha256);same(name+'.entities',w.state.document.entities,n.state.document.entities);same(name+'.selection',selection(w,false),selection(n,true));
   if(name==='pending'){assert.equal(n.edit.active,true);assert.equal(n.edit.calculating,true);assert.equal(n.edit.boundaryStatus,'preparing');}
   if(name==='prepared'){same('prepared.ok',w.outcome.ok,n.outcome.ok);assert.equal(n.edit.calculating,false);assert.equal(n.edit.boundaryStatus,w.outcome.ok?'ready':'error');}
   compared.controllerStages++;
  }
  assert.ok(native.events.some(event=>event.calculating===true&&event.boundaryStatus==='preparing'),'actual pending event');
  if(gap){const settled=native.stages.settled;assert.equal(settled.observed,true);assert.deepEqual(selection(settled,true),selection(native.stages.prepared,true),'native projection-gap settled selection changed after preparation');validateCanonicalInitial(settled,web.input);validateEditor(settled,'settled',web);assert.deepEqual(settled.state.nativeCanonicalDocument,before.state.nativeCanonicalDocument);assert.equal(settled.state.canonicalBytesBase64,before.state.canonicalBytesBase64);assert.deepEqual(native.replayGestures,[]);}
 });
 check('helper',()=>{
  const helper=object(native.topologyHelper,'native topology helper');assert.equal(helper.observed,true);assert.equal(helper.scope,'topology-and-receipt-helper-only');assert.deepEqual(helper.inputOwnerIds,web.input.selectedIds);const preparation=object(helper.preparation,'native preparation'),expected=web.directPreparation.result;assert.equal(web.directPreparation.ok,true);same('helper.preparation.valid',expected.valid,preparation.valid);compared.preparation=true;
  const inspect=object(helper.inspect,'native numeric key observation');same('helper.inspect.coordinate',web.input.inspectCoordinate,inspect.coordinate);same('helper.inspect.nodeKey',web.inspect.nodeKey,inspect.nodeKey);same('helper.inspect.quantizedKey',web.inspect.nodeKey.split(',').map(Number),inspect.quantizedKey);compared.numericKey=true;
  const handleKeys=['nodeKey','coordinate','polygonIndex','ringIndex','index','refs','virtualRefs','ownerIds','fixed','segments'];
  same('helper.handles.ordered',expected.handles.map(handle=>pick(handle,handleKeys)),list(preparation.handles,'native handles').map(handle=>pick(handle,handleKeys)));
  same('helper.segments.ordered',expected.segments.map(segment=>pick(segment,['key','start','end'])),list(preparation.segments,'native segments').map(segment=>pick(segment,['key','start','end'])));
  same('helper.selectedIds',expected.selectedIds,helper.selectedIdsInInputOrder);
  for(const handle of preparation.handles){for(const ref of list(handle.rawRefs,'raw refs'))validateRawRef(ref,web.input,false);for(const ref of list(handle.rawVirtualRefs,'raw virtual refs'))validateRawRef(ref,web.input,true);same('helper.refs.selected',handle.refs,list(handle.rawRefs,'raw refs').filter(ref=>expected.selectedIds.includes(ref.featureId)));same('helper.virtualRefs.selected',handle.virtualRefs,list(handle.rawVirtualRefs,'raw virtual refs').filter(ref=>expected.selectedIds.includes(ref.featureId)));same('helper.quantizedKey',handle.nodeKey.split(',').map(Number),handle.quantizedKey);}
  for(const segment of preparation.segments){const raw=web.topology.segments.find(row=>row.key===segment.key);assert.ok(raw,'segment absent from actual browser topology');assert.ok(Array.isArray(segment.ownerIds));same('helper.segment.owner-membership',[...raw.ownerIds].sort(),[...segment.ownerIds].sort());assert.equal(preparation.handles[segment.startNodeIndex]?.nodeKey,segment.key.split('|')[0]===preparation.handles[segment.startNodeIndex]?.nodeKey?segment.key.split('|')[0]:segment.key.split('|')[1]);same('helper.segment.start',preparation.handles[segment.startNodeIndex]?.coordinate,segment.start);same('helper.segment.end',preparation.handles[segment.endNodeIndex]?.coordinate,segment.end);}
  if(!expected.valid){assert.deepEqual(preparation.handles,[]);assert.deepEqual(preparation.segments,[]);assert.equal(helper.move,undefined);assert.equal(helper.preview,undefined);unobserved.push({case:web.case,scope:'direct native worker movement',reason:'Native preparation rejects this case; no separate native boundary-move RPC is exposed.'});return;}
  assert.equal(helper.gesture?.ok,true);same('helper.gesture',workflow.gesture?.ok,helper.gesture.ok);const move=object(helper.move,'native helper movement');same('helper.move.coordinate',web.input.move.coordinate,move.coordinate);same('helper.move.changed',workflow.stages.drag.outcome.changed,move.changed);
  if(web.case===NO_DIRECT_PEER){assert.equal(move.changed,false);assert.deepEqual(move.movedOwnerIds,[]);assert.deepEqual(move.features,[]);assert.equal(helper.preview,undefined);assert.equal(web.directMove.ok,true);unobserved.push({case:web.case,scope:'direct-worker-only movement',reason:'Native Session applies the 1e-9 gesture no-op guard. Its unchanged result is not the web direct-worker movement result.'});return;}
  assert.equal(web.directMove.ok,true);same('helper.move.owner-order',web.directMove.result.affectedIds,move.movedOwnerIds);same('helper.move.features.rawOrdered',rawFeatures(web.directMove.result.features),rawFeatures(move.features));compared.move=true;
  const visual=workflow.visualEvents.find(event=>event.kind==='move');assert.ok(visual);same('helper.activeSegments.ordered',visual.value,helper.activeSegments);
  const preview=object(helper.preview,'native helper preview receipt');same('helper.preview.ok',workflow.stages.preview.observed,preview.ok);compared.preview=true;
  if(!preview.ok){assert.equal(preview.error,'BOUNDARY_OUTER_UNION_CHANGED');assert.ok(workflow.diagnostics.some(row=>row.kind==='error'&&row.code==='PL-TERRITORIAL-EDIT'&&row.message==='국경 조정으로 선택 국가의 바깥 경계를 변경할 수 없습니다.'));return;}
  assert.equal(preview.prepareOk,true);const wp=object(workflow.stages.preview.state.preview,'actual web preview');same('helper.preview.removed',wp.removedIds,preview.removedIds);assert.deepEqual(preview.reparented,[]);assert.deepEqual(preview.impacts,[{id:move.movedOwnerIds[0],kind:'boundary',messageKey:'territorial.boundary.reconcile'}],'only the native internal boundary-operation record is permitted');
  const beforeById=new Map(web.input.features.map(feature=>[feature.id,feature])),afterById=new Map(wp.afterFeatures.map(feature=>[feature.id,feature]));assert.equal(new Set(preview.rows.map(row=>row.id)).size,preview.rows.length);same('helper.preview.rowIds',[...wp.affectedIds].sort(),preview.rows.map(row=>row.id).sort());
  for(const row of preview.rows){same('helper.preview.before.'+row.id,exactGeometryBoundary(beforeById.get(row.id)?.geometry),exactGeometryBoundary(row.before));same('helper.preview.after.'+row.id,exactGeometryBoundary(afterById.get(row.id)?.geometry),exactGeometryBoundary(row.after));}
  same('helper.preview.entities',wp.afterFeatures.map(entity).sort((a,b)=>a.id.localeCompare(b.id)).map(row=>({...row,geometry:exactGeometryBoundary(row.geometry)})),preview.entities.map(row=>({...row,geometry:exactGeometryBoundary(row.geometry)})).sort((a,b)=>a.id.localeCompare(b.id)));
 });
 check('controller.actual-stages',()=>{
  for(const name of STAGES){
   const w=workflow.stages[name],n=native.stages[name];assert.equal(typeof w?.observed,'boolean','missing browser '+name);
   if(gap&&['drag','preview','cancel','impactCancel','confirm','undo','redo','settled'].includes(name))continue;
   assert.equal(n.observed,w.observed,'native/browser reached-stage mismatch '+name);
   if(!n.observed){assert.ok([NO_STAGE,'The actual controller created no canonical preview for this move.'].includes(n.reason),'unrecognized native absence '+name);continue;}
   if(['before','cold','pending','prepared'].includes(name))continue;
   if(w.outcome&&Object.hasOwn(w.outcome,'ok'))same(name+'.outcome.ok',w.outcome.ok,n.outcome?.ok);
   same(name+'.selection',selection(w,false),selection(n,true));
   if(name==='drag'){same('drag.changed',w.outcome.changed,n.outcome?.changed);same('drag.coordinate',w.outcome.coordinate,n.outcome?.coordinate);const inputs=list(native.inputObservations,'actual controller input');assert.equal(inputs.length,1);const input=inputs[0];assert.equal(input.exact,true);assert.deepEqual(input.intended,web.input.move.coordinate);assert.deepEqual(input.inverse,input.intended);assert.equal(input.maximumUlpRadius,4);for(const key of ['xUlpSteps','yUlpSteps'])assert.ok(Number.isInteger(input[key])&&Math.abs(input[key])<=4);}
   // Current bounded supplement has no reached native commit. Any newly reachable
   // controller commit requires extending observation coverage, never silent promotion.
   assert.ok(!['confirm','undo','redo'].includes(name),'new native commit path needs explicit full canonical comparison');
   validateCanonicalInitial(n,web.input);validateEditor(n,name,web);assert.deepEqual(n.state.nativeCanonicalDocument,native.stages.before.state.nativeCanonicalDocument);assert.equal(n.state.canonicalBytesBase64,native.stages.before.state.canonicalBytesBase64);assert.equal(n.state.documentSha256,native.stages.before.state.documentSha256);assert.equal(n.edit.previewReady,false,'rejected/no-op move created a preview');
   compared.controllerStages++;
  }
  if(!native.stages.drag.observed)assert.deepEqual(native.inputObservations,[]);assert.deepEqual(native.replayGestures,[]);
  if(workflow.gesture){assert.equal(native.gesture?.ok,workflow.gesture.ok);assert.equal(native.gesture.picked,true);}else assert.equal(native.gesture,undefined);
 });
 return {case:web.case,passed:differences.length===0,rawParity:false,compared,differences,unobserved};
}
export function compareSupplementalObservations(webCases,native){
 const differences=[],expected=ids();let rows=[];
 try{assert.equal(expected.length,12);assert.equal(native?.schema,'pando-m974-native-boundary-workflows');assert.equal(native.version,1);rows=list(native.rows,'native rows');assert.deepEqual(list(webCases,'browser rows').map(row=>row.case),expected);assert.deepEqual(rows.map(row=>row.case),expected);assert.equal(new Set(rows.map(row=>row.case)).size,12);}catch(error){differences.push({field:'corpus',error:error.message});}
 const results=Array.isArray(webCases)?webCases.map(row=>compareSupplementalCase(row,rows.find(observation=>observation.case===row.case))):[];
 return {schema:'pando-m974-native-supplemental-comparison',version:1,passed:differences.length===0&&results.length===12&&results.every(row=>row.passed),rawParity:false,allControllerStagesProven:false,coverage:{expectedCases:12,browserCases:webCases?.length??0,nativeCases:rows.length,numericKeyCases:results.filter(row=>row.compared.numericKey).length,preparationCases:results.filter(row=>row.compared.preparation).length,moveCases:results.filter(row=>row.compared.move).length,previewReceiptCases:results.filter(row=>row.compared.preview).length,controllerStages:results.reduce((sum,row)=>sum+row.compared.controllerStages,0)},differences,results,unobserved:results.flatMap(row=>row.unobserved),limits:['Ordered prepared handles, node-owner vectors, refs, virtual refs, segment endpoints and moved coordinates use exact IEEE-754 equality without rounding. Prepared segment owner membership is compared; owner ordering is not in the web preparation packet, and full-topology insertion ordering is a distinct observation.','Complete raw topology outside prepared native handles is not exported; numeric inspect keys and preparation rejection remain separately compared.','Six bounded public projection input omissions remain unproven controller lifecycles. Helper receipts never substitute for them.','The 4e-10 direct-worker movement has no native Session counterpart; the actual native UI no-op is compared separately.','Canonical preview geometry alone uses the existing exact boundary representation mapping for ring start/winding/collinear vertices. Pixel rendering and complete raw parity are not certified.']};
}
export function readCapture({browserDirectory,expectedCommit,expectedRunId}){
 assert.match(expectedCommit??'',/^[a-f0-9]{40}$/,'explicit expected commit required');assert.match(expectedRunId??'',/^\d+$/,'explicit expected run ID required');const bytes={},read=name=>(bytes[name]=fs.readFileSync(path.join(browserDirectory,name)));
 const compressed=read('suite.json.gz'),suiteBytes=gunzipSync(compressed,{maxOutputLength:32*1024*1024}),pin=JSON.parse(read('suite-pin.json'));assert.equal(digest(suiteBytes),pin.decompressedSha256);assert.equal(suiteBytes.length,pin.decompressedBytes);const suite=verifySupplementalSuite(JSON.parse(suiteBytes));assert.deepEqual(pin.identity,suite.identity);
 const reportBytes=read('browser-report.json'),transfer=JSON.parse(read('report-transfer.json'));assert.equal(reportBytes.length,transfer.bytes);assert.equal(reportBytes.toString('utf8').length,transfer.characters);assert.equal(digest(reportBytes),transfer.sha256);assert.ok(Number.isInteger(transfer.chunks)&&transfer.chunks>0);assert.ok(Number.isInteger(transfer.chunkCharacters)&&transfer.chunkCharacters>=2&&transfer.chunkCharacters<=1024*1024);assert.ok(gunzipSync(read('browser-report.json.gz'),{maxOutputLength:32*1024*1024}).equals(reportBytes),'compressed browser report differs from transferred bytes');
 const report=verifySupplementalReport(suite,JSON.parse(reportBytes));for(const identity of [suite.identity,suite.base,suite.base.identity,report.identity]){assert.equal(identity.commit,expectedCommit,'browser commit differs from caller expectation');assert.equal(String(identity.runId),expectedRunId,'browser run differs from caller expectation');}
 const exported=read('supplemental-observations.json');assert.equal(digest(exported),digest(JSON.stringify(report.cases)),'frozen supplemental export bytes');assert.ok(equal(JSON.parse(exported),report.cases),'standalone supplemental export mismatch');const gate=JSON.parse(read('capture-verification.json'));assert.equal(gate.passed,true);assert.deepEqual(gate.identity,suite.identity);assert.equal(gate.reportDecompressedSha256,transfer.sha256);assert.equal(gate.cases,12);assert.equal(gate.rawParity,false);
 return {suite,report,artifacts:Object.fromEntries(Object.entries(bytes).map(([name,value])=>[name,{bytes:value.length,sha256:digest(value)}]))};
}
export function parseArguments(args){const names={'--browser-directory':'browserDirectory','--native-probe':'nativeProbe','--output':'outputDirectory','--expected-commit':'expectedCommit','--expected-run-id':'expectedRunId'},options={};for(let index=0;index<args.length;index+=2){const key=names[args[index]],value=args[index+1];assert.ok(key,'unknown argument '+args[index]);assert.ok(!Object.hasOwn(options,key),'duplicate argument');assert.ok(value&&!value.startsWith('--'),'missing argument value');options[key]=value;}for(const key of Object.values(names))assert.ok(options[key],'required argument '+key);return options;}
export function comparatorFingerprints(){return Object.fromEntries(['m974-native-supplemental.mjs','m97/split-differential.mjs'].map(file=>[file,digest(fs.readFileSync(new URL(file,import.meta.url)))]));}
export function runGate(options){
 fs.mkdirSync(options.outputDirectory,{recursive:true});let receipt={schema:'pando-m974-native-supplemental-gate',version:1,state:'running',passed:false,rawParity:false,allControllerStagesProven:false,expectedCommit:options.expectedCommit,expectedRunId:options.expectedRunId};const save=()=>fs.writeFileSync(path.join(options.outputDirectory,'receipt.json'),JSON.stringify(receipt,null,2)+'\n');save();
 try{const capture=readCapture(options),comparatorFiles=comparatorFingerprints(),binaryBytes=fs.readFileSync(options.nativeProbe),binarySha256=digest(binaryBytes),ciIdentityVerified=process.env.GITHUB_ACTIONS==='true';if(ciIdentityVerified){assert.equal(process.env.GITHUB_SHA,options.expectedCommit,'CI/native checkout commit mismatch');assert.equal(process.env.GITHUB_RUN_ID,options.expectedRunId,'CI/native workflow run mismatch');}
  const input=JSON.stringify({cases:capture.suite.cases}),run=spawnSync(options.nativeProbe,[],{input,encoding:'utf8',timeout:180000,maxBuffer:32*1024*1024});fs.writeFileSync(path.join(options.outputDirectory,'native-stderr.txt'),run.stderr||'');assert.equal(run.error,undefined,String(run.error));assert.equal(run.status,0,'native probe failed: '+String(run.stderr).slice(-2048));const native=JSON.parse(run.stdout);fs.writeFileSync(path.join(options.outputDirectory,'native-observations.json'),run.stdout);
  assert.equal(digest(fs.readFileSync(options.nativeProbe)),binarySha256,'native executable changed during capture');for(const [file,sha]of Object.entries(comparatorFiles))assert.equal(digest(fs.readFileSync(new URL(file,import.meta.url))),sha,'comparator changed during capture');
  const comparison=compareSupplementalObservations(capture.report.cases,native);fs.writeFileSync(path.join(options.outputDirectory,'comparison.json'),JSON.stringify(comparison,null,2)+'\n');receipt={...receipt,state:'complete',passed:comparison.passed,ciIdentityVerified,identity:capture.suite.identity,runtime:capture.report.runtime,artifacts:capture.artifacts,comparatorFiles,native:{file:path.basename(options.nativeProbe),sha256:binarySha256,inputSha256:digest(input),observationsSha256:digest(run.stdout)},comparison};
 }catch(error){receipt={...receipt,state:'failed',passed:false,error:String(error.stack||error)};}save();return receipt;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){try{const result=runGate(parseArguments(process.argv.slice(2)));console.log(JSON.stringify({passed:result.passed,state:result.state,rawParity:false,coverage:result.comparison?.coverage,error:result.error}));if(!result.passed)process.exitCode=1;}catch(error){console.error(error.stack);process.exitCode=1;}}
