import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {createHash,webcrypto} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';
import {createPlaceWorkerStore} from '../../tests/fixtures/web-place-runtime-source/assets/js/modules/place-worker-store.js';
import {createPlaceRuntime} from '../../tests/fixtures/web-place-runtime-source/assets/js/modules/place-runtime.js';
import {decodePlaceTile} from '../../tests/fixtures/web-place-runtime-source/assets/js/modules/place-codec.js';
import {isBuiltinPlaceId,normalizePlaceQuery,PLACE_LIMITS} from '../../tests/fixtures/web-place-runtime-source/assets/js/modules/place-contract.js';
import {automaticLabelSettings,placeLabelDimensions,layoutLabels,labelKey} from '../../tests/fixtures/web-place-runtime-source/assets/js/modules/label-layout.js';
import {createFrameProjectors} from '../../tests/fixtures/web-place-runtime-source/assets/js/modules/map-visual-frame.js';
import {placeView} from '../../tests/fixtures/web-place-runtime-source/tests/helpers/place-view.mjs';
globalThis.crypto ||= webcrypto;
const commit='ebcfae4d27b29cbbea6416a7045a4806930204be',root=new URL('../../tests/fixtures/web-place-runtime-source/',import.meta.url),sha=b=>createHash('sha256').update(b).digest('hex');
const args=process.argv.slice(2),option=name=>{const i=args.indexOf(name);return i<0?null:args[i+1];};
const webRepo=option('--web-repo')||'D:/dev/Pandoeditor(Web)';
const provenance=JSON.parse(readFileSync(new URL('source-manifest.json',root)));
assert.equal(provenance.webCommit,commit);
for(const spec of [...provenance.sources,...provenance.artifacts])assert.equal(sha(readFileSync(new URL(spec.path,root))),spec.sha256,spec.path);
// Read exact additional storage/candidate owners without modifying Web or the fixtures.
const owners={};for(const path of ['assets/js/modules/app-generic-commands.js','assets/js/modules/app-territorial-labels.js'])owners[path]=execFileSync('git',['-C',webRepo,'show',`${commit}:${path}`]);
const copyText=owners['assets/js/modules/app-generic-commands.js'].toString('utf8').match(/  function copySelectedPlaceForEditing\(\) \{[\s\S]*?\n  \}/u)?.[0];
const suppressText=owners['assets/js/modules/app-territorial-labels.js'].toString('utf8').match(/const copiedIds = new Set\(dependencies\.projectState\.state\.labels\.map\(label => label\.sourcePlaceId\)\.filter\(Boolean\)\);/u)?.[0];
assert.ok(copyText&&suppressText,'exact pinned copy/suppression source expressions');
const suppressed=labels=>vm.runInNewContext(`${suppressText}\nArray.from(copiedIds)`,{dependencies:{projectState:{state:{labels}}}});
const qualityText=owners['assets/js/modules/app-territorial-labels.js'].toString('utf8')
 .match(/    const labelDensity = [\s\S]*?(?=    const safe = frameContext.safeInset;)/u)?.[0];
assert.ok(qualityText,'original fixed-Web application quality prefilter is present');
function store(name){const folder=new URL(name==='production'?'assets/data/places/':`synthetic/${name}/`,root),manifest=JSON.parse(readFileSync(new URL('manifest.json',folder)));return {manifest,worker:createPlaceWorkerStore({manifest,fetchBytes:async spec=>new Uint8Array(readFileSync(new URL(spec.url,folder)))})};}
const records=rows=>rows.map(r=>({id:r.id,source:r.source,sourceId:r.sourceId,name:r.name,kind:r.kind,coordinates:[...r.coordinates],countryCode:r.countryCode,featureCode:r.featureCode,population:r.population,priority:r.priority,minZoom:r.minZoom,notes:r.notes}));
const ids=rows=>rows.map(r=>r.id),view=(threshold=3,width=400,dpr=1,bottom=0,extra={})=>placeView({width,height:300,scale:100,threshold,devicePixelRatio:dpr,safeInset:{left:0,top:0,right:0,bottom},...extra});
const cases={},basic=decodePlaceTile(new Uint8Array(readFileSync(new URL('synthetic/basic/0.bin',root))));cases['decode-basic']=records(basic);
const cameras=[['flat-basic','basic',view()],['flat-zoom1','basic',view(1)],['flat-pan','basic',view()],['flat-dateline','basic',view(3,400,1,0,{flatCenter:[180,0]})],['globe-front','basic',view(3,400,1,0,{projection:'globe'})],['globe-back','basic',view(3,400,1,0,{projection:'globe',rotation:[-180,0,0]})],['dpr1','basic',view(3,400,1)],['dpr3','basic',view(3,400,3)],['mobile-layout','basic',view(3,400,3,96)],['desktop-layout','basic',view(3,800,3,32)],['safe-bottom','basic',view(3,400,1,160)],['dense-limit','dense',view()],['overscan','overscan',view()]];
for(const [name,fixture,v] of cameras){const r=await store(fixture).worker.queryViewport(v);cases[`viewport-${name}`]={records:records(r.records),tileCount:r.tileCount};}
for(const [i,query] of ['', '서','  서울  ','서울 도시','\uFEFF서울\u00A0','missing'].entries()){const r=await store('basic').worker.search(query);cases[`search-${i}`]={normalized:normalizePlaceQuery(query),records:records(r.records),truncated:r.truncated};}
{const s=store('production');cases['production-empty']={revision:s.manifest.revision,ids:ids((await s.worker.queryViewport(view())).records),sourceRecordCountObserved:Object.hasOwn(s.manifest,'recordCount')};}
{const s=store('basic');let cancelled=false;try{await s.worker.queryViewport(view(),{throwIfCancelled(){throw Object.assign(new Error('cancelled'),{cancelled:true});}});}catch(e){cancelled=!!e.cancelled;}cases['store-cancel']={cancelled,cachedTiles:s.worker.stats().cachedTiles};}
function runtime(){const s=store('basic');let protectedIds=[];const p=createPlaceRuntime({manifestUrl:'http://fixed.test/manifest.json',getProtectedIds:()=>protectedIds,rpc:{stop(){},async request(type,payload,{signal}){await Promise.resolve();const context={throwIfCancelled(){if(signal.aborted)throw Object.assign(new Error('cancelled'),{name:'AbortError',cancelled:true});}};const result=await (type==='place.viewport'?s.worker.queryViewport(payload.view,context):s.worker.search(payload.query,context));context.throwIfCancelled();return {result};}}});return {p,protect(ids){protectedIds=ids;ids.forEach(id=>p.retain(p.resolve(id)));}};}
{
 let owner=runtime(),p=owner.p;await p.prepare(view());cases['runtime-ready']=ids(p.snapshot().records);
 let previous=p.snapshot();p.beginInteraction();const deferred=!await p.prepare(view(1));cases['runtime-moving']={deferred,held:p.snapshot()===previous,moving:p.stats().moving};
 await p.settle();cases['runtime-settled']=ids(p.snapshot().records);
 previous=p.snapshot();const pending=p.prepare(view());p.cancelViewport();const published=await pending;cases['runtime-cancel']={held:p.snapshot()===previous,noPublication:!published};
 const first=p.prepare(view()),last=p.prepare(view(1));await Promise.all([first,last]);cases['runtime-latest']=ids(p.snapshot().records);
 previous=p.snapshot();const search=await p.search('서울'),selected='builtin:place:synthetic:city';owner.protect([selected]);cases['runtime-shared-selection']={viewportHeld:p.snapshot()===previous,selected:p.resolve(selected).id,searchIds:ids(search.records)};
 const abandoned=p.prepare(view());const old=p;old.dispose();owner=runtime();p=owner.p;await abandoned;cases['runtime-revision']={advanced:p!==old,snapshotEmpty:p.snapshot().records.length===0,selectionEmpty:p.resolve(selected)===null};
 const closing=p.prepare(view());p.dispose();await closing;cases['runtime-close']={closed:!p.isSearchOpen(),snapshotEmpty:p.snapshot().records.length===0,selectionEmpty:p.resolve(selected)===null};
}
for(let scenario=0;scenario<8;scenario++){
 const selected=new Set(scenario===1?['builtin:place:synthetic:city']:[]),copyLabels=scenario===4||scenario===6?[{sourcePlaceId:'builtin:place:synthetic:capital'}]:[],hidden=scenario===7;
 const suppressedIds=new Set(suppressed(copyLabels)),frame=view().projectionFrame,{projectVisibleCoordinate}=createFrameProjectors(frame);
 const candidates=hidden?[]:basic.filter(r=>!suppressedIds.has(r.id)).map(r=>{const s=automaticLabelSettings(r.kind),point=projectVisibleCoordinate(r.coordinates);return {key:r.id,point,...placeLabelDimensions(r.name),priority:s.priority,minZoom:Math.max(r.minZoom,s.minZoom),maxZoom:s.maxZoom,collisionGroup:s.collisionGroup,pinned:s.pinned,selected:selected.has(r.id)};}).filter(c=>c.point);
 const documentSourceCount=scenario===2||scenario===3?1:0;
 if(documentSourceCount)candidates.push({key:'document-label',point:projectVisibleCoordinate([0,0]),width:34,height:19,priority:100,minZoom:0,maxZoom:Infinity,collisionGroup:scenario===2?'country':'place'});
 cases[`labels-${scenario}`]={ids:layoutLabels(candidates,{zoom:3,bounds:{left:0,top:0,right:400,bottom:300}}).map(r=>r.key),documentSourceCount,documentSourceRevision:17};
}
{
 const state={selected:{domain:'label',id:basic[0].id},labels:[],labelSettings:{}},history=[];let autosaves=0;
 const dependencies={projectState:{state},surfaces:{uid:()=> '11111111-1111-4111-8111-111111111111'},labelPresentation:{labelById:id=>basic.find(r=>r.id===id),labelKey,automaticLabelSettings},domains:{projectDomain:{recordHistory(){history.push(structuredClone(state));},queueAutosave(){autosaves++;}},renderingDomain:{invalidateLabels(){}}},layers:{markLayerTreeDirty(){}},propertyEditingA:{applyLabelSelectionIntent(id){state.selected={domain:'label',id};}},feedback:{setActionStatus(){}}};
 const separated=state.labels.length===0&&history.length===0,copy=vm.runInNewContext(`(${copyText})`,{dependencies,isBuiltinPlaceId})();
 assert.equal(history.length,1,'actual fixed copy records one history entry');assert.equal(autosaves,1,'actual fixed copy queues one autosave');assert.notEqual(copy.coordinates,basic[0].coordinates,'actual fixed copy owns its coordinates');
 const after=structuredClone(state),copySuppress=suppressed(state.labels).includes(basic[0].id),pinned=state.labelSettings[labelKey('label',copy.id)].pinned;
 Object.assign(state,history[0]);const undone=state.labels.length===0&&suppressed(state.labels).length===0;Object.assign(state,after);const redone=suppressed(state.labels).includes(basic[0].id);
 cases['storage-copy']={transientSeparated:separated,copySourceId:copy.sourcePlaceId,copySuppresses:copySuppress,pinned,oneUndoRestores:undone,redoSuppresses:redone,sourceUnchanged:basic[0].coordinates[0]===0&&basic[0].name==='서울'};
}
for(let scenario=0;scenario<3;scenario++){
 const count=scenario===1?2048:60,candidates=Array.from({length:count},(_,i)=>{
  const id=`row${String(i).padStart(4,'0')}`;
  return {key:`label:${id}`,point:[400,300],width:scenario===1?2000:20,height:19,
   priority:scenario===0?i:90,collisionGroup:id,pinned:scenario===0&&i===0,selected:scenario===0&&i===1};
 });
 if(scenario===1)candidates.push({key:'label:lower',point:[400,300],width:20,height:19,priority:1,collisionGroup:'lower'});
 if(scenario===2)candidates.reverse();
 const qualityCandidates=vm.runInNewContext(`${qualityText}\nqualityCandidates`,{
  candidates,dependencies:{renderScene:{currentRenderQuality:{labelDensity:scenario===1?1:.52,tier:'diagnostic'}}},
  frameContext:{cssViewport:[800,600]},PLACE_LIMITS
 });
 cases[`quality-${scenario}`]=JSON.parse(JSON.stringify(layoutLabels(qualityCandidates,{zoom:3,bounds:{left:0,top:0,right:800,bottom:600}}).map(r=>r.key.slice('label:'.length))));
}
assert.equal(cases['quality-0'].length,44);
assert.deepEqual(cases['quality-1'],[]);
assert.equal(cases['quality-2'].length,42);assert.equal(cases['quality-2'][0],'row0018');
// Independent fixed literals detect a shared accidental operand/schema change.
assert.deepEqual(ids(cases['viewport-flat-basic'].records),['builtin:place:synthetic:capital','builtin:place:synthetic:city']);assert.equal(cases['viewport-dense-limit'].records.length,1500);assert.equal(cases['viewport-dense-limit'].records[0].sourceId,'02047');assert.equal(cases['production-empty'].revision,'empty-v1');assert.equal(cases['store-cancel'].cachedTiles,0);assert.equal(Object.keys(cases).length,42);
const result={version:1,webCommit:commit,scope:'synthetic mechanism exchange; production empty-v1 BLOCKED',cases,sourceOwnerHashes:Object.fromEntries(Object.entries(owners).map(([path,bytes])=>[path,sha(bytes)]))};
const report={mechanismCases:Object.keys(cases).length,comparedCases:0,mismatchCount:0,mismatches:[],fontGeometryDifferences:[],productionData:'BLOCKED: immutable production empty-v1 has zero source tiles; synthetic fixtures cannot prove real data acceptance',web:result};
const nativePath=option('--native');if(nativePath){const native=JSON.parse(readFileSync(nativePath));assert.equal(native.webCommit,commit);const names=new Set([...Object.keys(cases),...Object.keys(native.cases||{})]);for(const name of names){report.comparedCases++;try{assert.deepEqual(native.cases[name],cases[name]);}catch(e){report.mismatches.push({case:name,native:native.cases[name],web:cases[name]});}}report.mismatchCount=report.mismatches.length;for(const geometry of native.fontGeometryReport||[]){if(geometry.qtWidth!==geometry.webWidth||geometry.qtHeight!==geometry.webHeight)report.fontGeometryDifferences.push(geometry);}report.fontGeometryDisposition='REPORT: Qt actual font metrics differ from fixed Web Unicode-count boxes; no tolerance and no pixel/layout parity claim';}
const reportPath=option('--report');if(reportPath)writeFileSync(reportPath,JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify({webCommit:commit,sourceHashesVerified:provenance.sources.length+provenance.artifacts.length,mechanismCases:report.mechanismCases,comparedCases:report.comparedCases,mismatchCount:report.mismatchCount,fontGeometryDifferenceCount:report.fontGeometryDifferences.length,productionData:'BLOCKED',mode:nativePath?'actual-native-exchange':'web-only; native unexecuted'}));
if(report.mismatchCount)process.exitCode=1;
