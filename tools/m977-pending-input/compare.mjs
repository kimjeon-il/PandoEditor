// Integrity validation is independent from equivalence. Differences are retained,
// never accepted through an allowlist or converted to a regenerated golden.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {isDeepStrictEqual} from 'node:util';
import {spawnSync,execFileSync} from 'node:child_process';
import {gunzipSync} from 'node:zlib';
import {readPendingSources,pendingProjection,sha256} from './sources.mjs';
import {verifyWebCase} from './runtime.mjs';
const same=(a,b,message)=>assert.deepEqual(a,b,message);
const keys=(value,expected,message)=>same(Object.keys(value).sort(),[...expected].sort(),message);
const taps=['firstPending','secondPending','firstReady','secondReady','tapAfterBack'];
const decisions=['cancelSwitch','switchAgain','confirmSwitch'];
function canonical(stage){
 assert.equal(typeof stage.canonicalBytesBase64,'string');
 const bytes=Buffer.from(stage.canonicalBytesBase64,'base64');
 assert.ok(bytes.length>0&&bytes.length<8*1024*1024);assert.equal(bytes.toString('base64'),stage.canonicalBytesBase64);
 assert.equal(sha256(bytes),stage.canonicalSha256,'native canonical bytes/hash');
 const document=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(bytes));
 assert.equal(document.format,'pandoeditor-project');assert.equal(document.version,10);return {bytes,document};
}
function finitePoint(point){assert.ok(Array.isArray(point)&&point.length===2&&point.every(Number.isFinite),'finite coordinate');}
function verifyNativeCase(corpus,definition,row){
 assert.equal(row.case,definition.id);same(row.input,definition,'exact native corpus case');assert.equal(row.error,undefined,'native case must finish');
 same(row.corpusInput,{features:corpus.features,points:corpus.points});keys(row.stages,definition.stages,'complete native stage coverage');
 assert.equal(definition.view.kind,'flat','bounded public affine observation requires flat mode');
 assert.ok(row.projectionParameters&&typeof row.projectionParameters==='object','public native projection parameters required');
 keys(row.projectionParameters,['cosLatitude','minX','maxLatitude'],'exact public projection parameter fields');
 const {cosLatitude,minX,maxLatitude}=row.projectionParameters;
 assert.ok([cosLatitude,minX,maxLatitude].every(Number.isFinite)&&cosLatitude>0&&cosLatitude<=1,'finite public affine parameters');
 assert.equal(minX,Math.min(...corpus.features.map(f=>f.bounds[0]))*cosLatitude,'public projection source west bound');
 assert.equal(maxLatitude,Math.max(...corpus.features.map(f=>f.bounds[3])),'public projection source north bound');
 // These are consistency equations for the captured public affine parameters,
 // not a replacement camera oracle or a cross-runtime projection comparison.
 const project=point=>[point[0]*cosLatitude-minX,maxLatitude-point[1]];
 const unproject=point=>[(point[0]+minX)/cosLatitude,maxLatitude-point[1]];
 const before=canonical(row.baseline);assert.ok(Number.isSafeInteger(row.baseline.revision)&&row.baseline.revision>=0);
 assert.equal(row.baseline.projectCanUndo,false);assert.equal(row.baseline.projectCanRedo,false);
 same(before.document.units.map(u=>u.id),corpus.features.map(f=>f.id),'actual native territorial fixture membership');
 assert.equal(before.document.geometries.length,corpus.features.length);
 for(const feature of corpus.features){
  const [x0,y0,x1,y1]=feature.bounds,geometry=before.document.geometries.find(g=>g.id===feature.id);
  same(geometry,{id:feature.id,version:1,geojson:{type:'Polygon',coordinates:[[[x0,y0],[x1,y0],[x1,y1],[x0,y1],[x0,y0]]]}},'exact native source geometry');
  assert.ok(before.document.timelineRecords.geometryBindings.some(b=>b.entityId===feature.id&&b.geometryRef.id===feature.id&&b.geometryRef.version===1&&b.validFrom===null&&b.validTo===null));
 }
 for(const name of ['rawPolygonDraftCoordinates','historyDepth','requestAndEpochIds','privateWorkerPayload','browserOrNativePointerDispatch','projectHistoryReplay']){
  assert.equal(row.limits[name]?.observed,false,'unsupported native evidence remains unavailable');assert.equal(typeof row.limits[name].reason,'string');
 }
 const missingDecision=definition.scenario==='two-pending-empty-switch'&&row.stages.switch.confirmation===null;
 for(const name of definition.stages){
  const s=row.stages[name];assert.equal(typeof s.observed,'boolean');
  if(!s.observed){assert.ok(missingDecision&&decisions.includes(name),'only causally unavailable decisions may be unobserved');keys(s,['observed','reason']);assert.ok(s.reason.length>0);continue;}
  assert.ok(!(missingDecision&&decisions.includes(name)),'absent confirmation cannot produce observed dependent actions');
  const bytes=canonical(s).bytes;same(bytes,before.bytes,'exact native document immutability '+name);assert.equal(s.unchangedFromBefore,true);
  assert.equal(s.revision,row.baseline.revision);assert.equal(s.projectCanUndo,false);assert.equal(s.projectCanRedo,false);
  for(const flag of ['pending','draftUndoAvailable','draftRedoAvailable'])assert.equal(typeof s[flag],'boolean');
  assert.ok(Array.isArray(s.coordinates));s.coordinates.forEach(finitePoint);
  if(s.state.active){assert.equal(s.state.previewReady,false);assert.equal(s.state.previewPending,false);assert.equal(s.state.applying,false);assert.equal(s.state.error,'');}else same(s.state,{active:false});
  assert.equal(s.pending,s.state.selectionPending===true);assert.equal(s.draftUndoAvailable,s.state.canUndoDraft===true);assert.equal(s.draftRedoAvailable,s.state.canRedo===true);
  if(s.state.active){assert.equal(s.method,s.state.activeMethod);assert.equal(s.stage,s.state.stage);assert.equal(s.phase,s.state.selectionPhase);}else same([s.method,s.stage,s.phase],[null,null,null]);
  same(s.confirmation,s.state.confirmationKind?{kind:s.state.confirmationKind,requestedMethod:s.state.requestedMethod}:null,'native confirmation comes from public state');
  const view=definition.view;
  for(const [field,value]of Object.entries({scale:view.scale,translateX:view.translate[0],translateY:view.translate[1],viewportWidth:view.size.width,viewportHeight:view.size.height,rotationLongitude:view.rotate[0],rotationLatitude:view.rotate[1],rotationRoll:view.rotate[2],centerLongitude:view.center[0],centerLatitude:view.center[1]}))assert.equal(s.mapViewState[field],value,'actual native map-view '+field);
  assert.equal(s.mapViewState.projection,view.kind);
  const raw=s.state.active===true&&s.method==='line';
  assert.equal(s.coordinateObservation.raw,raw,'coordinate mechanism follows public method');
  assert.equal(s.coordinateObservation.mechanism,raw?'riverSelectionObservation.inputLine':'geometryDraftPaths.vertices + MapProjection.unproject','exact public coordinate mechanism');
  assert.ok(Array.isArray(s.draftPaths));
  const projectedVertices=s.draftPaths.flatMap(draftPath=>{
   assert.ok(Array.isArray(draftPath.vertices));return draftPath.vertices.map(vertex=>{const point=[vertex.x,vertex.y];finitePoint(point);return point;});
  });
  same(s.coordinateObservation.projectedVertices,projectedVertices,'projected coordinate evidence belongs to public paths');
  if(raw){same(s.coordinates,s.selectionObservation.inputLine,'raw line input');assert.equal(s.coordinateObservation.roundTripCanLosePrecision,false);}
  else{assert.equal(s.coordinateObservation.roundTripCanLosePrecision,true);same(s.coordinates,projectedVertices.map(unproject),'exact inverse of observed public vertices');}
 }
 const expectedTaps=definition.stages.filter(name=>taps.includes(name));same(row.inputObservations.map(t=>t.stage),expectedTaps,'complete ordered native taps');
 const acceptedInputs=[];
 for(const tap of row.inputObservations){
  same(tap.intended,corpus.points[corpus.tapPointIndexes[tap.stage]],'native uses declared shared tap coordinate');finitePoint(tap.mapCoordinate);finitePoint(tap.inverseBeforeInput);
  same(tap.mapCoordinate,project(tap.intended),'exact affine intended-to-map input');same(tap.inverseBeforeInput,unproject(tap.mapCoordinate),'exact affine pre-input inverse');
  assert.equal(tap.pointerType,definition.profile.pointerType);assert.equal(tap.tolerance,0);assert.equal(typeof tap.accepted,'boolean');
  assert.equal(tap.exactInputRoundTrip,JSON.stringify(tap.intended)===JSON.stringify(tap.inverseBeforeInput));same(row.stages[tap.stage].outcome,{action:'geometryAddPoint',accepted:tap.accepted});
  if(tap.stage.endsWith('Pending')||tap.stage==='tapAfterBack'){assert.equal(tap.accepted,false);same(row.stages[tap.stage].coordinates,[]);}
  else{
   assert.equal(tap.accepted,true);acceptedInputs.push(tap.inverseBeforeInput);
   const stage=row.stages[tap.stage];same(stage.snapState.indicator,{},'bounded accepted input has no snap result');
   same(stage.coordinateObservation.projectedVertices,acceptedInputs.map(project),'accepted input belongs to corresponding public draft vertex');
  }
 }
 for(const name of ['activation','firstPending','secondPending','held'])if(row.stages[name]){
  const s=row.stages[name];assert.equal(s.pending,true);same(s.coordinates,[]);assert.equal(s.draftUndoAvailable,false);assert.equal(s.draftRedoAvailable,false);
 }
 if(row.stages.held){same(row.deliveryBarrier,{mechanism:'controller-zero-timer-dispatched-worker-completed-owner-delivery-withheld',targetedControllerMetaCalls:true,timerTailSignals:1,globalPoolCompleted:true,ownerCompletionEventsProcessed:false,selectionPending:true,timeoutMs:30000});same(row.stages.held.outcome,row.deliveryBarrier);}
 else assert.equal(row.deliveryBarrier,undefined);
 if(row.stages.ready){assert.equal(row.stages.ready.pending,false);same(row.stages.ready.coordinates,[]);}
 if(row.stages.firstReady)assert.equal(row.stages.firstReady.coordinates.length,1);
 if(row.stages.secondReady)assert.equal(row.stages.secondReady.coordinates.length,2);
 const sameDraft=(stage,previous,message)=>{same(stage.coordinates,previous.coordinates,message+' coordinates');same(stage.draftPaths,previous.draftPaths,message+' public paths');};
 if(row.stages.undoDraft){sameDraft(row.stages.undoDraft,row.stages.firstReady,'Undo restores first ready input');assert.equal(row.stages.undoDraft.draftRedoAvailable,true);sameDraft(row.stages.redoDraft,row.stages.secondReady,'Redo restores second ready input');}
 if(row.stages.sameMethod){sameDraft(row.stages.sameMethod,row.stages.secondReady,'same method preserves draft');assert.equal(row.stages.sameMethod.confirmation,null);}
 if(row.stages.switch?.confirmation){const previous=row.stages.redoDraft||row.stages.ready;sameDraft(row.stages.switch,previous,'switch decision preserves draft');}
 if(row.stages.cancelSwitch?.observed){sameDraft(row.stages.cancelSwitch,row.stages.switch,'cancel preserves draft');sameDraft(row.stages.switchAgain,row.stages.switch,'re-request preserves draft');assert.equal(row.stages.cancelSwitch.confirmation,null);assert.equal(row.stages.switchAgain.confirmation?.requestedMethod,'line');}
 if(row.stages.confirmSwitch?.observed){assert.equal(row.stages.confirmSwitch.method,'line');same(row.stages.confirmSwitch.coordinates,[]);assert.equal(row.stages.confirmSwitch.confirmation,null);assert.equal(row.stages.confirmSwitch.draftUndoAvailable,false);assert.equal(row.stages.confirmSwitch.draftRedoAvailable,false);}
 if(row.stages.afterLateCompletion){const s=row.stages.afterLateCompletion;same(s.coordinates,[]);assert.equal(s.pending,false);
  if(definition.scenario==='held-clear')assert.equal(s.method,null);
  else if(definition.scenario==='held-back')assert.equal(s.stage,'setup');
  else{assert.equal(s.method,'line');assert.equal(s.stage,'selection');}
 }
 assert.ok(Array.isArray(row.events)&&row.events.length>0);row.events.forEach((e,index)=>{assert.equal(e.sequence,index);assert.equal(typeof e.state.active,'boolean');});
 let signalIndex=0;
 for(const name of definition.stages){
  const stage=row.stages[name];if(!stage.observed)continue;
  const index=row.events.findIndex((event,index)=>index>=signalIndex&&isDeepStrictEqual(event.state,stage.state));
  assert.ok(index>=0,'observed public state appears in signal order '+name);
  // Rejected taps and no-op method requests may reuse the preceding signal.
  signalIndex=index;
 }
 return row;
}
export function verifyNativeReport(bundle,report){
 keys(report,['schema','version','runtime','corpusSha256','cases']);assert.equal(report.schema,'pando-m977-native-pending-input');assert.equal(report.version,1);
 same(report.runtime,{qt:'6.8.3'});assert.equal(report.corpusSha256,bundle.addendum.corpusSha256);
 same(report.cases.map(c=>c.case),bundle.corpus.cases.map(c=>c.id),'complete exact native cases');
 report.cases.forEach((row,index)=>verifyNativeCase(bundle.corpus,bundle.corpus.cases[index],row));return report;
}
function webShared(s){return {stage:s.stage,phase:s.phase,method:s.method,pending:s.pending,confirmationMethod:s.confirmation?.method??null,coordinates:s.coordinates,draftUndoAvailable:s.draftUndo>0,draftRedoAvailable:s.draftRedo>0,projectCanUndo:s.projectUndo>0,projectCanRedo:s.projectRedo>0};}
function nativeShared(s){return {stage:s.stage,phase:s.phase,method:s.method,pending:s.pending,confirmationMethod:s.confirmation?.requestedMethod??null,coordinates:s.coordinates,draftUndoAvailable:s.draftUndoAvailable,draftRedoAvailable:s.draftRedoAvailable,projectCanUndo:s.projectCanUndo,projectCanRedo:s.projectCanRedo};}
const verifiedCaptures=new WeakSet();
export function comparePendingCollections(bundle,web,native,{mode}={}){
 assert.ok(mode==='node-diagnostic'||(mode==='verified-chromium-capture'&&verifiedCaptures.has(web)),'only an actually verified capture can be labelled Chromium');
 verifyNativeReport(bundle,native);same(web.cases.map(c=>c.case),bundle.corpus.cases.map(c=>c.id),'complete exact web cases');
 web.cases.forEach((row,index)=>{const definition=bundle.corpus.cases[index];verifyWebCase(definition,row,bundle.corpus,pendingProjection(bundle,definition));});
 const cases=web.cases.map((row,index)=>{
  const n=native.cases[index],differences=[],unsupportedStages=[],pairedStages=[];
  for(const stage of row.input.stages){
   const w=row.stages[stage],ns=n.stages[stage];if(!ns.observed){unsupportedStages.push({case:row.case,stage,webObserved:true,nativeObserved:false,reason:ns.reason});continue;}
   const ws=webShared(w),shared=nativeShared(ns);pairedStages.push(stage);
   for(const field of Object.keys(ws))if(JSON.stringify(ws[field])!==JSON.stringify(shared[field])){
    const diff={stage,field,web:ws[field],native:shared[field]};
    if(field==='coordinates'&&ws[field].length===shared[field].length){diff.maxAbsoluteDelta=Math.max(0,...ws[field].flatMap((p,i)=>p.map((v,a)=>Math.abs(v-shared[field][i][a]))));diff.nativeObservationMechanism=ns.coordinateObservation.mechanism;diff.epsilonApplied=false;}
    differences.push(diff);
   }
  }
  return {case:row.case,profile:row.input.profile,pairedStages,differences,unsupportedStages,pendingTaps:row.inputs.filter(t=>t.stage.endsWith('Pending')).map(t=>({stage:t.stage,intended:t.coordinate,webPointCount:row.stages[t.stage].coordinates.length,nativePointCount:n.stages[t.stage].coordinates.length,nativeAccepted:n.stages[t.stage].outcome.accepted})),canonicalWithinRuntimeUnchanged:true,rawParity:false};
 });
 return {schema:'pando-m977-pending-input-comparison',version:1,integrityPassed:true,mode,actualChromium:mode==='verified-chromium-capture',gateAcceptance:false,rawParity:false,
  pairedCases:cases.length,requestedStages:bundle.corpus.cases.reduce((n,c)=>n+c.stages.length,0),pairedStages:cases.reduce((n,c)=>n+c.pairedStages.length,0),
  differenceCount:cases.reduce((n,c)=>n+c.differences.length,0),unsupportedStages:cases.flatMap(c=>c.unsupportedStages),cases,
  limits:{fullDOM:false,actualTouchDispatch:false,pixelParity:false,gpu:false,fullWebProjectSerialization:false,projectApplyUndoRedo:false,rawNativePolygonCoordinates:false,nativeNumericHistoryDepth:false,nativeRequestGenerationIds:false,workerCPUExecutionPhase:false,sameScreenCoordinates:false,
   preparationPhase:'Raw web preparing and native drawing+selectionPending retained; no phase normalization hides differences.',
   pendingSwitch:'Immediate supersession before old Worker execute/native timer dispatch; not an old Worker-result cancellation.',
   heldResults:'Worker completed with owner delivery withheld; no simultaneous CPU execution claim.',
   switchBoundary:'Web awaited selectMethod promise; native immediate public return. Pending differences at confirm are scheduling observations, not equivalent completion checkpoints.',
   nativePolygonCoordinates:'Exact affine consistency uses captured public parameters for attempted input and observed vertices. Accepted no-snap input is bound through its full projection round trip; no epsilon, rounding or cross-runtime projection equivalence is applied.',
   canonical:'Exact bytes checked within each runtime; cross-format canonical equivalence is not claimed.'}};
}
export function parseArguments(args){
 const mode=args.shift();assert.ok(['--diagnostic','--capture'].includes(mode),'choose diagnostic or capture');
 const count=mode==='--capture'?5:3,names=mode==='--capture'?['suite','web','binding','native','output']:['web','native','output'];assert.ok(args.length>=count);
 const result={mode};for(const name of names){const value=args.shift();assert.ok(value&&!value.startsWith('--'),'missing '+name);result[name]=value;}
 while(args.length){const key=args.shift(),value=args.shift();assert.ok(['--native-binary','--native-commit'].includes(key)&&value&&!value.startsWith('--'),'unknown or missing argument');const field=key.slice(2);assert.equal(result[field],undefined,'duplicate argument');result[field]=value;}
 assert.ok(result['native-binary'],'actual native binary required');if(mode==='--capture')assert.match(result['native-commit']||'',/^[a-f0-9]{40}$/);else assert.equal(result['native-commit'],undefined);return result;
}
function readBytes(file){const bytes=fs.readFileSync(file);return file.endsWith('.gz')?gunzipSync(bytes,{maxOutputLength:32*1024*1024}):bytes;}
export async function runComparison(options){
 const bundle=readPendingSources(),webBytes=readBytes(options.web);let web=JSON.parse(webBytes),identity,mode='node-diagnostic',captureProvenance={};
 const head=execFileSync('git',['rev-parse','HEAD'],{encoding:'utf8'}).trim();
 if(options.mode==='--capture'){
  const {verifyPendingCapture}=await import('./suite.mjs'),suiteBytes=readBytes(options.suite),bindingBytes=readBytes(options.binding),binding=JSON.parse(bindingBytes),suite=JSON.parse(suiteBytes);
  web=verifyPendingCapture(suiteBytes.toString('utf8'),webBytes.toString('utf8'),binding);identity=suite.identity;
  assert.equal(process.env.GITHUB_ACTIONS,'true','actual capture comparison requires exact-commit CI');assert.equal(process.env.GITHUB_SHA,head);assert.equal(process.env.GITHUB_RUN_ID,identity.runId);assert.equal(identity.commit,head);assert.equal(options['native-commit'],head);
  verifiedCaptures.add(web);mode='verified-chromium-capture';captureProvenance={suiteSha256:sha256(suiteBytes),captureBindingSha256:sha256(bindingBytes)};
 }else{
  assert.equal(web.schema,'pando-m977-node-pending-input');assert.equal(web.gateAcceptance,false);assert.equal(web.applicationCommit,head);assert.equal(web.sourceManifestSha256,sha256(JSON.stringify(bundle.manifest)));assert.equal(web.addendumSha256,sha256(JSON.stringify(bundle.addendum)));assert.equal(web.corpusSha256,bundle.addendum.corpusSha256);
  identity={commit:head,runId:null};
 }
 const binary=path.resolve(options['native-binary']),binaryBytes=fs.readFileSync(binary),binaryHash=sha256(binaryBytes);
 assert.ok(!fs.existsSync(options.native),'fresh native output required; refuse detached or stale report');assert.ok(!fs.existsSync(options.output),'fresh comparison output required');
 fs.mkdirSync(path.dirname(path.resolve(options.native)),{recursive:true});
 const run=spawnSync(binary,[new URL('../../tests/fixtures/web-m977-pending-input/corpus.json',import.meta.url).pathname],{encoding:'utf8',timeout:120000,maxBuffer:8*1024*1024,env:{...process.env,QT_QPA_PLATFORM:'offscreen',QT_QUICK_BACKEND:'software'}});
 fs.writeFileSync(options.native+'.stderr',run.stderr||'');assert.ifError(run.error);assert.equal(run.status,0,'actual native probe completion: '+run.stderr);assert.equal(sha256(fs.readFileSync(binary)),binaryHash,'native binary stable during capture');
 fs.writeFileSync(options.native,run.stdout,{flag:'wx'});const native=JSON.parse(run.stdout),result=comparePendingCollections(bundle,web,native,{mode});
 result.identity=identity;result.provenance={...captureProvenance,webReportSha256:sha256(webBytes),nativeReportSha256:sha256(run.stdout),nativeReportBytes:Buffer.byteLength(run.stdout),nativeBinarySha256:binaryHash,nativeProbeSourceSha256:sha256(fs.readFileSync(new URL('../../tests/m977_pending_input_probe.cpp',import.meta.url))),comparatorSha256:sha256(fs.readFileSync(fileURLToPath(import.meta.url))),ciIdentityVerified:mode==='verified-chromium-capture'};
 fs.mkdirSync(path.dirname(path.resolve(options.output)),{recursive:true});fs.writeFileSync(options.output,JSON.stringify(result,null,2)+'\n',{flag:'wx'});return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))runComparison(parseArguments(process.argv.slice(2))).then(r=>console.log(JSON.stringify({integrityPassed:r.integrityPassed,actualChromium:r.actualChromium,pairedCases:r.pairedCases,pairedStages:r.pairedStages,differences:r.differenceCount,unsupportedStages:r.unsupportedStages.length,rawParity:false,gateAcceptance:false}))).catch(e=>{console.error(e);process.exitCode=1;});
