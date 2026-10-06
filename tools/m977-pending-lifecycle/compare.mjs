import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {readLifecycleSources,sha256,projectCurrentLifecycleInput} from './sources.mjs';
import {verifyWebLifecycleCase,rawChanges} from './contract.mjs';
import {accountWebNativeExchange} from '../m975-model-exchange/ownership-accounting.mjs';
import {exactGeometryBoundary} from '../m97/split-differential.mjs';
import {verifyEditEffect} from '../m975-model-exchange/edit-effects.mjs';
import {compareFullWebProject,compareNativeStorage} from '../web-v10-exchange-oracle.mjs';
import {isDeepStrictEqual} from 'node:util';
import {parseTemporal} from '../../tests/fixtures/web-v10-exchange/source/assets/js/modules/temporal.js';
const root=fileURLToPath(new URL('../../',import.meta.url));
function currentWeb(raw){
 const p=JSON.parse(raw);assert.equal(p.format,'pandolab-project-state');assert.equal(p.schemaVersion,10);assert.equal(p.territorialModel.schemaVersion,6);
 for(const row of p.territorialEntities){assert.equal(row.properties.schemaVersion,6);assert.ok(Object.hasOwn(row.properties,'sourceEntityId'));assert.equal(Object.hasOwn(row.properties,'sourceLibraryId'),false);}
 return p;
}
export function verifyCurrentLifecycleWeb(raw,expected){const actual=currentWeb(raw);compareFullWebProject(actual,expected,'literal current lifecycle interchange before any reader');return actual;}
// Independent wire mappings from the explicit Web input. Accounting below
// authenticates geometry ownership and opaque tokens; it does not authenticate
// these typed fields. Compare them before excluding inline domains from the
// shared territorial checker. No expected value comes from the native receipt.
export function compareCurrentInlineStorage(content,fixed){
 const clone=value=>structuredClone(value),territory=id=>id?{domain:'territorial',id}:null;
 const endpoint=value=>{const t=parseTemporal(value);if(!t)return null;assert.equal(t.canonical,value,'explicit canonical temporal input');return {text:value,precision:t.precision};};
 const validity=row=>({from:endpoint(row.validFrom),to:endpoint(row.validTo)});
 const source=p=>{assert.equal(p.schemaVersion,1);const {schemaVersion,...fields}=p;return clone(fields);};
 const original=[...fixed.geometries].sort((a,b)=>Buffer.compare(Buffer.from(a.id),Buffer.from(b.id))||a.version-b.version);
 const geometryRef=(domain,id,shape)=>{if(shape===null)return null;const preferred={id:'web-'+domain+':'+id,version:1},exact=original.find(g=>g.id===preferred.id&&g.version===preferred.version),matching=exact||original.find(g=>isDeepStrictEqual(g.geojson,shape));return matching?{id:matching.id,version:matching.version}:preferred;};
 const common=(row,p,domain,shape)=>({id:row.id,name:p.name??'',notes:p.notes??'',geometryRef:geometryRef(domain,row.id,shape),source:source(p.source)});
 const expected={
  labels:fixed.labels.map(row=>({...common(row,row,'label',{type:'Point',coordinates:row.coordinates}),kind:row.kind,territory:territory(row.territorialUnitId)})),
  hydro:fixed.hydroEdits.map(row=>{const p=row.properties;return {...common(row,p,'hydroEdits',row.geometry),kind:p.category,color:p.editorColor.toLowerCase(),locked:p.locked,sourceFeatureId:p.sourceFeatureId||null};}),
  genericFeatures:fixed.genericFeatures.map(row=>{const p=row.properties;return {...common(row,p,'genericFeatures',row.geometry),color:p.color.toLowerCase(),locked:p.locked,fallbackOnly:true};}),
  distributionLayers:fixed.distributionLayers.map(row=>{assert.ok(['auto','manual'].includes(row.valueScale.mode));return {id:row.id,name:row.name,unit:row.unit,valueScale:row.valueScale.mode==='manual'?{mode:'manual',min:row.valueScale.min,max:row.valueScale.max}:{mode:'auto'},color:row.color.toLowerCase(),locked:row.locked,parentId:row.parentId||null,groups:clone(row.groups),validity:validity(row),metadata:clone(row.metadata)};}),
  distributionEntries:fixed.distributionEntries.map(row=>{assert.ok(['territorial','geometry'].includes(row.mode));return {id:row.id,layerId:row.layerId,territory:row.mode==='territorial'?territory(row.territorialUnitId):null,geometryRef:geometryRef('distributionEntry',row.id,row.mode==='territorial'?null:row.geometry),value:row.value,certainty:row.certainty,validity:validity(row),metadata:clone(row.metadata)};}),
 };
 for(const [key,rows]of Object.entries(expected))assert.deepEqual(content[key],rows,'literal complete native typed inline storage '+key);
 return expected;
}
export function verifyCurrentNativeInput(nativeRaw,webRaw,inputRaw){
 const fixed=currentWeb(inputRaw),web=verifyCurrentLifecycleWeb(webRaw,fixed),native=JSON.parse(nativeRaw);
 compareCurrentInlineStorage(native.content,fixed);
 const accounting=accountWebNativeExchange({sourceWebRaw:inputRaw,nativeRaw,webOutputRaw:webRaw,expectedWebSchemaVersion:10});
 // The shared native checker supports territorial/header storage. The existing
 // complete typed mapping above authenticates every excluded inline field;
 // accounting checks full archive, ledger, ownership and raw opaque owner slots.
 const n=structuredClone(native),w=structuredClone(web),excluded=['labels','hydro','genericFeatures','distributionLayers','distributionEntries'];
 assert.deepEqual(Object.keys(n.content).sort(),[...excluded,'countryDetails','symbols','physicalData'].sort());
 for(const key of excluded)n.content[key]=[];
 for(const key of ['labels','hydroEdits','genericFeatures','distributionLayers','distributionEntries'])w[key]=[];
 const derived=new Set(native.geometryProvenance.inlineAllocations.filter(a=>!a.promoted).map(a=>JSON.stringify([a.ref.id,a.ref.version])));
 n.geometries=n.geometries.filter(g=>!derived.has(JSON.stringify([g.id,g.version])));
 for(const u of n.units)assert.deepEqual(Object.keys(u).sort(),['id','kind','name','baseName','nameExplicit','locked','libraryOrigin','metadata','notes','sourceFolderId','sourceEntityId','sourceGeometryVersion'].sort());
 assert.deepEqual(n.content.physicalData,{dataset:'',version:'',source:'',hiddenHydroIds:[]});assert.deepEqual(n.presentation.userLayers,[]);assert.deepEqual(n.presentation.membership,[]);
 compareNativeStorage(n,w,'current lifecycle territorial/header fields with complete accounting');return accounting;
}
export function decodeBlob(value){
 assert.equal(typeof value?.base64,'string');const bytes=Buffer.from(value.base64,'base64');assert.equal(bytes.toString('base64'),value.base64);assert.equal(bytes.length,value.bytes);assert.equal(sha256(bytes),value.sha256);return {bytes,document:JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(bytes))};
}
export function collectNativeCapture(binary,bundle=readLifecycleSources()){
 const before=sha256(fs.readFileSync(binary));const run=spawnSync(binary,[path.join(root,bundle.currentInput.corpusPath),path.join(root,bundle.currentInput.corpus.fixture.path)],{encoding:'utf8',timeout:120000,maxBuffer:32*1024*1024,env:{...process.env,QT_QPA_PLATFORM:'offscreen',QT_QUICK_BACKEND:'software'}});assert.ifError(run.error);assert.equal(run.status,0,run.stderr);assert.equal(sha256(fs.readFileSync(binary)),before,'native binary stable during collection');return {report:JSON.parse(run.stdout),raw:run.stdout,stderr:run.stderr,binarySha256:before,rawSha256:sha256(run.stdout),rawBytes:Buffer.byteLength(run.stdout)};
}
export function collectNative(binary,bundle=readLifecycleSources()){return collectNativeCapture(binary,bundle).report;}
export function verifyNativeLifecycleReport(bundle,report){
 assert.equal(report.schema,'pando-m977-native-pending-lifecycle');assert.equal(report.version,1);assert.deepEqual(report.runtime,{compiledQt:'6.8.3',qt:'6.8.3'});assert.equal(report.corpusSha256,bundle.currentInput.corpusSha256);assert.equal(report.fixtureSha256,bundle.currentInput.corpus.fixture.sha256);assert.deepEqual(report.cases.map(r=>r.case),bundle.corpus.cases.map(d=>d.id));
 for(const [index,row]of report.cases.entries()){
  const def=bundle.corpus.cases[index];assert.deepEqual(row.input,def);assert.deepEqual(row.stageOrder,def.stages);assert.deepEqual(Object.keys(row.stages).sort(),[...def.stages].sort());assert.deepEqual(row.errors,[]);
  assert.ok(row.events.length>0,'actual public signals required');row.events.forEach((e,i)=>{assert.equal(e.sequence,i);assert.ok(def.stages.includes(e.phase));assert.equal(typeof e.state.active,'boolean');});let eventCount=0;
  for(const name of def.stages){const s=row.stages[name];assert.ok(Number.isSafeInteger(s.eventCount)&&s.eventCount>=eventCount&&s.eventCount<=row.events.length);eventCount=s.eventCount;if(eventCount)assert.deepEqual(s.state,row.events[eventCount-1].state,'public state belongs to latest ordered signal '+name);}
  const before=row.stages.before,applied=row.stages.applied;const base=decodeBlob(before.document),edited=decodeBlob(applied.document);assert.notDeepEqual(edited.bytes,base.bytes,'real native Apply changes document');assert.equal(base.document.version,10);verifyCurrentNativeInput(base.bytes.toString('utf8'),decodeBlob(before.web).bytes.toString('utf8'),bundle.currentInput.fixtureRaw);
  verifyEditEffect({operation:'annex',full:false},decodeBlob(before.web).document,decodeBlob(applied.web).document);
  for(const [name,s]of Object.entries(row.stages)){
   assert.equal(s.observed,true);const isEdited=['applied','redo'].includes(name);assert.deepEqual(decodeBlob(s.document).bytes,isEdited?edited.bytes:base.bytes,'exact full native snapshot '+name);assert.deepEqual(decodeBlob(s.web).bytes,decodeBlob(isEdited?applied.web:before.web).bytes,'full raw native export follows canonical state '+name);const nativeDocument=decodeBlob(s.document).document,webDocument=decodeBlob(s.web).document;assert.deepEqual(webDocument.timelineRecords,nativeDocument.timelineRecords,'native exported reference graph is actual native graph');assert.deepEqual(webDocument.territorialEntities.map(f=>f.id),nativeDocument.units.map(u=>u.id));for(const row of webDocument.geometries)assert.deepEqual(row,nativeDocument.geometries.find(g=>g.id===row.id&&g.version===row.version),'native export geometry identity/value');if(s.state.active)assert.deepEqual(s.selection.state,s.state,'owned public selection uses same state');
   assert.deepEqual(Object.keys(nativeDocument).sort(),Object.keys(base.document).sort(),'complete native field inventory '+name);
   for(const key of Object.keys(base.document))if(!['geometries','timelineRecords'].includes(key))assert.deepEqual(nativeDocument[key],base.document[key],'partial annex preserves every other native field, including full ledger '+name+'/'+key);
   for(const key of ['lifetimes','parentRelations'])assert.deepEqual(nativeDocument.timelineRecords[key],base.document.timelineRecords[key],'partial annex retains native '+key+' '+name);
   assert.deepEqual(s.history,isEdited?{canUndo:true,canRedo:false}:name==='undo'?{canUndo:false,canRedo:true}:{canUndo:false,canRedo:false});assert.equal(s.revision,before.revision+({applied:1,undo:2,redo:3}[name]||0));
   assert.equal(s.state.error||'','');assert.equal(s.state.selectionPending===true,s.state.selectionPending||false);
   assert.deepEqual(s.projectedVertices,s.draftPaths.flatMap(p=>p.vertices.map(v=>[v.x,v.y])));const {cosLatitude,minX,maxLatitude}=row.projection;
   assert.ok([cosLatitude,minX,maxLatitude].every(Number.isFinite)&&cosLatitude>0&&cosLatitude<=1);assert.deepEqual(s.coordinates,s.projectedVertices.map(p=>[(p[0]+minX)/cosLatitude,maxLatitude-p[1]]));
   for(const [k,v]of Object.entries({projection:'flat',scale:def.view.scale,translateX:def.view.translate[0],translateY:def.view.translate[1],viewportWidth:def.profile.width,viewportHeight:def.profile.height,rotationLongitude:def.view.rotate[0],rotationLatitude:def.view.rotate[1],rotationRoll:def.view.rotate[2],centerLongitude:def.view.center[0],centerLatitude:def.view.center[1]}))assert.equal(s.mapViewState[k],v,'actual native view '+k);
  }
  const expected=[];const {cosLatitude,minX,maxLatitude}=row.projection;
  for(const prefix of ['', 'retry']){
   const n=s=>prefix?prefix+s[0].toUpperCase()+s.slice(1):s;
   for(const key of ['activation','firstPending','secondPending','held']){const s=row.stages[n(key)];assert.equal(s.state.selectionPending,true);assert.deepEqual(s.coordinates,[]);assert.equal(s.state.canUndoDraft,false);}
   assert.deepEqual(row.stages[n('held')].outcome,{timerTailSignals:1,workerComplete:true,ownerCompletionEventsProcessed:false});
   const ready=row.stages[n('ready')];assert.equal(ready.state.selectionPending,false);assert.deepEqual(ready.coordinates,[]);assert.equal(ready.state.canUndoDraft,false);assert.equal(ready.state.canRedo,false);
   const points=[];for(const [i,p]of [...bundle.corpus.pendingPoints,...bundle.corpus.readyPoints].entries()){
    const name=n(i<2?['firstPending','secondPending'][i]:['firstReady','secondReady','thirdReady','fourthReady'][i-2]);expected.push(name);const t=row.inputs.find(t=>t.stage===name);assert.ok(t);assert.deepEqual(t.coordinate,p);assert.deepEqual(t.mapCoordinate,[p[0]*cosLatitude-minX,maxLatitude-p[1]]);assert.deepEqual(t.roundTrip,[(t.mapCoordinate[0]+minX)/cosLatitude,maxLatitude-t.mapCoordinate[1]]);assert.deepEqual(t.roundTrip,t.coordinate,'exact native intended geographic input');const v=row.stages[t.stage].mapViewState;assert.deepEqual(t.screen,[t.mapCoordinate[0]*v.mapScale+v.originX,t.mapCoordinate[1]*v.mapScale+v.originY],'native public screen projection consistency');assert.ok(t.screen[0]>=0&&t.screen[0]<def.profile.width&&t.screen[1]>=0&&t.screen[1]<def.profile.height);assert.equal(t.pointerType,def.profile.pointerType);assert.equal(t.prePending,i<2);assert.equal(t.accepted,i>=2);assert.equal(row.stages[name].outcome,t.accepted);
    if(i>=2){points.push(t.mapCoordinate);assert.deepEqual(row.stages[name].projectedVertices,points);assert.deepEqual(row.stages[name].snapState.indicator,{});}
   }
   assert.equal(row.stages[n('finished')].outcome,true);assert.equal(row.stages[n('finished')].state.selectionPhase,'candidates');for(const key of ['preview','review']){const s=row.stages[n(key)];assert.equal(s.state.previewReady,true);assert.equal(s.state.parts.length,1);assert.equal(s.outcome,true);}assert.equal(row.stages[n('review')].state.stage,'review');const finished=row.stages[n('finished')],preview=row.stages[n('preview')],review=row.stages[n('review')];assert.equal(finished.selection.candidates.length,1);assert.deepEqual(finished.state.selectedCandidateIds,finished.selection.candidates.map(c=>c.id));assert.deepEqual(preview.selection.parts[0].geometry,finished.selection.candidates[0].geometry);assert.deepEqual(review.selection.parts,preview.selection.parts);assert.deepEqual(review.selection.transferredGeometry,preview.selection.transferredGeometry);assert.ok(review.selection.transferredGeometry.coordinates.length);for(const stage of [preview,review])assert.deepEqual(stage.state.parts.map(p=>({id:p.id,method:p.method})),stage.selection.parts.map(p=>({id:p.id,method:p.method})));
  }
  assert.deepEqual(row.inputs.map(t=>t.stage),expected);for(const n of ['before','cancel','applied','undo','redo'])assert.deepEqual(row.stages[n].state,{active:false});
 }
 return report;
}
export function compareLifecycle(bundle,web,native){
 verifyNativeLifecycleReport(bundle,native);assert.deepEqual(web.cases.map(c=>c.case),bundle.corpus.cases.map(c=>c.id));const cases=[];
 for(const [i,def]of bundle.corpus.cases.entries()){
  const w=verifyWebLifecycleCase(bundle,def,web.cases[i]),n=native.cases[i];cases.push({case:def.id,paired:verifyPairedLifecycle(bundle,w,n),stages:def.stages.length,webRawHistory:{undoEqual:w.stages.undo.canonical===w.stages.before.canonical,redoEqual:w.stages.redo.canonical===w.stages.applied.canonical,undoDifferences:rawChanges(JSON.parse(w.stages.before.canonical),JSON.parse(w.stages.undo.canonical)),redoDifferences:rawChanges(JSON.parse(w.stages.applied.canonical),JSON.parse(w.stages.redo.canonical))},crossEngineRaw:Object.fromEntries(def.stages.map(name=>[name,rawChanges(JSON.parse(w.stages[name].canonical),decodeBlob(n.stages[name].web).document)]))});
 }
 return {schema:'pando-m977-pending-lifecycle-comparison',version:1,functionalLifecycleContractsPassed:true,pairedSemanticContractsPassed:true,rawParity:false,actualBrowser:false,currentInput:bundle.currentInput.provenance,currentCorpusSha256:bundle.currentInput.corpusSha256,webLifecycleContract:'historical Web9 observations projected across four explicit input boundaries; not current Web10 production lifecycle evidence',cases};
}

// Reuse the accepted exact dyadic boundary comparison. This ignores only polygon
// wrapper, cyclic ring start/winding and proved-collinear representation; it never
// rounds a coordinate, clips a polygon or permits numerical tolerance.
export function verifyPairedLifecycle(bundle,web,native){
 assert.equal(web.case,native.case);assert.deepEqual(web.input,native.input);const def=bundle.corpus.cases.find(d=>d.id===web.case);assert.ok(def);
 const emptyGroups=['countries','regions','distributions','hydro','labels','countryLabels'];let archiveGeometries=0,candidateGeometries=0,partGeometries=0;
 for(const name of def.stages){
  const w=web.stages[name],n=native.stages[name];assert.equal(w.observed,true);assert.equal(n.observed,true);
  assert.equal(!!w.session,n.state.active,'same active lifecycle '+name);assert.equal(w.stage,n.state.stage??null,'same lifecycle stage '+name);assert.equal(w.pending,n.state.selectionPending===true,'same pending policy '+name);assert.equal(w.method,n.state.activeMethod||null,'same active drawing method '+name);assert.equal(w.previewReady,n.state.previewReady===true,'same preview availability '+name);
  assert.equal(w.draftUndo>0,n.state.canUndoDraft===true,'same draft Undo '+name);assert.equal(w.draftRedo>0,n.state.canRedo===true,'same draft Redo '+name);assert.deepEqual(w.coordinates,n.coordinates,'same exact public draft '+name);
  assert.deepEqual({canUndo:w.history.undo>0,canRedo:w.history.redo>0},n.history,'same project history '+name);assert.equal(w.revision-web.stages.before.revision,n.revision-native.stages.before.revision,'same revision transitions '+name);
  const phase=w.pending?'preparing':w.phase,otherPhase=n.state.selectionPending?'preparing':n.state.selectionPhase==='candidates'?'candidate':n.state.selectionPhase||null;assert.equal(phase,otherPhase,'documented public phase spelling '+name);
  const a=projectCurrentLifecycleInput(JSON.parse(w.canonical)),b=currentWeb(decodeBlob(n.web).bytes.toString('utf8'));assert.deepEqual(Object.keys(a).sort(),Object.keys(b).sort(),'same whole interchange field inventory after declared input projection '+name);
  for(const key of Object.keys(a)){
   if(key==='geometries')continue;
   if(key==='itemVisibility'&&['undo','redo'].includes(name)){
    assert.deepEqual(Object.keys(a[key]).sort(),[...Object.keys(b[key]),...emptyGroups].sort(),'only six recorded web empty visibility groups');for(const group of emptyGroups)assert.deepEqual(a[key][group],{});for(const group of Object.keys(b[key]))assert.deepEqual(a[key][group],b[key][group]);
   }else assert.deepEqual(a[key],b[key],'exact shared interchange '+name+'/'+key);
  }
  assert.deepEqual(a.geometries.map(g=>({id:g.id,version:g.version})),b.geometries.map(g=>({id:g.id,version:g.version})),'same ordered geometry archive identity/version '+name);
  for(const [i,g]of a.geometries.entries()){assert.deepEqual(Object.keys(g).sort(),Object.keys(b.geometries[i]).sort());assert.deepEqual(exactGeometryBoundary(g.geojson),exactGeometryBoundary(b.geometries[i].geojson),'exact shared geometry boundary '+name+'/'+g.id+'/'+g.version);archiveGeometries++;}
  const wc=w.session?.candidates||[],nc=n.selection.candidates||[],wp=w.session?.parts||[],np=n.selection.parts||[];
  assert.equal(wc.length,nc.length,'same candidate count '+name);assert.equal(wp.length,np.length,'same archived part count '+name);
  assert.deepEqual((w.session?.selectedCandidateIds||[]).map(id=>wc.findIndex(c=>c.id===id)),(n.state.selectedCandidateIds||[]).map(id=>nc.findIndex(c=>c.id===id)),'same selected candidate positions '+name);
  for(const [i,c]of wc.entries()){assert.deepEqual(exactGeometryBoundary(c.geometry),exactGeometryBoundary(nc[i].geometry),'exact selected candidate geometry '+name);candidateGeometries++;}
  for(const [i,p]of wp.entries()){assert.equal(p.method,np[i].method);assert.deepEqual(exactGeometryBoundary(p.geometry),exactGeometryBoundary(np[i].geometry),'exact archived part geometry '+name);partGeometries++;}
 }
 assert.deepEqual(web.inputs.map(t=>t.coordinate),native.inputs.map(t=>t.coordinate),'shared ordered stimuli');
 for(const [i,w]of web.inputs.entries()){const n=native.inputs[i];assert.equal(w.stage,n.stage);assert.deepEqual(w.roundTrip,w.coordinate,'actual web inverse equals common intended input');assert.deepEqual(n.roundTrip,n.coordinate,'actual native inverse equals common intended input');assert.deepEqual(w.roundTrip,n.roundTrip);for(const t of [w,n]){assert.ok(t.screen[0]>=0&&t.screen[0]<def.profile.width&&t.screen[1]>=0&&t.screen[1]<def.profile.height,'actual observed screen point is in viewport');}}
 return {stages:def.stages.length,archiveGeometries,candidateGeometries,partGeometries,exactSharedInput:true,exactSharedBoundary:true,exactReferencesAndHistory:true,rawParity:false,representationRules:['historical Web9 lifecycle to explicit Web10 input: project9->10, model5->6, identity5->6, sourceLibraryId->sourceEntityId','existing exactGeometryBoundary','JSON object key insertion order','six enumerated empty web itemVisibility groups after history','preparing versus drawing+selectionPending','candidate versus candidates','ephemeral candidate/part IDs paired by ordered exact geometry']};
}
