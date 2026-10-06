import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {isDeepStrictEqual as equal} from 'node:util';
import {gunzipSync} from 'node:zlib';
import {fileURLToPath} from 'node:url';
import {exactGeometryBoundary} from './m97/split-differential.mjs';
import {verifyCaptureSuite,verifyBrowserReport} from './m974-snap-boundary/protocol.mjs';

// These are explicit coverage limits, never substitutes for native observations.
export const REPRESENTATION_MAPPINGS = [
 'Canonical polygon boundaries alone permit cyclic ring start, winding, polygon/hole ordering, and exactly dyadic-collinear segmentation. No rounding, epsilon, area tolerance or polygon XOR is used.',
 'Helper handles, owner vectors, refs, virtual refs, segments, moved owner vectors and moved coordinates retain their actual returned order and raw IEEE-754 values.',
 'Feature/committed-entity collections are keyed by their actual stable IDs. No browser property, parent, reference, geometry or entity ID is copied into native results.',
 'Web label-settings territorial:ID keys map to native ref domain territorial plus ID. Native pinned=false and collisionGroup=map are explicit native defaults when absent from web. Web visible is unsupported, recorded as a presentation limit rather than a compared value.',
 'Active geometry references are checked against the actual entity ID, resolved local version, and exact boundary. Web territorial-geometry:ID and native ID are storage keys; no cross-engine immutable archive/version-allocation parity is claimed.',
 'Web history undo/redo counts map to native public canUndo/canRedo booleans only. Both sides must independently match exact canonical Undo/Redo results. Only root/child-descendant-effects Undo preserves the actual browser live-label omission of exactly removed and removed-deep; the entire remaining document must equal its own initial snapshot.',
 'Web distribution territorialUnitId maps to native territory.id, and validFrom/validTo map to native validity.from/to. Browser schemaVersion and mode are representation tags; all native-supported payload fields are compared.',
];
const STAGES=['before','cold','pending','prepared','drag','preview','cancel','impactCancel','confirm','undo','redo','settled'];
const INPUT_GAPS=new Set(['root-coast-union-change','root-uneven-multipolygon','root-negative-half-rounding']);
const INPUT_REASON='No exact public native projection preimage within four ULPs for the source fixture coordinate.';
const DELAYED_GAPS=new Set(['root-pending-cancel','root-stale-preparation','root-stale-move','root-stale-preview','root-pending-preview-cancel']);
const NO_NATIVE_STAGE='The actual native workflow did not reach this stage.';
const NATIVE_ABSENCE_REASONS=new Set([NO_NATIVE_STAGE,'The actual controller created no canonical preview for this move.']);
const WEB_ABSENCE_REASONS=new Set(['The production workflow has not reached this stage.','The production entrypoint rejected this selection.','The exact production beginBoundaryGesture callback rejected this handle.','The production callback rejected a no-op, invalid move, or territorial plan.','No canonical preview was created.','A real worker result was rejected after preparation cancellation or project generation change.','A real worker reply was rejected after cancellation or a stale session/revision.','Production boundary preparation rejected the selected geometry or hierarchy.']);
export const BOUNDARY_CASE_IDS=['root-triple','root-two-fixed','root-no-op','root-coast-union-change','root-invalid-latitude','root-uneven-multipolygon','root-selected-three-move-two','root-negative-half-rounding','child-triple','child-parent-fixed','child-two-fixed','root-disjoint-pairs','child-disjoint-pairs','child-auto-seed','root-isolated-owner','root-selected-locked','child-locked-parent','child-locked-ancestor','mixed-root-child','mixed-parents','regional-selection','missing-selected-owner','mixed-domain-selection','root-descendant-effects','child-descendant-effects','root-locked-touching','root-locked-unrelated','root-locked-cut-child','root-pending-cancel','root-stale-preparation','root-stale-move','root-stale-preview','root-pending-preview-cancel'];
const own=(o,k)=>o!=null&&Object.hasOwn(o,k);
function list(v,name){assert.ok(Array.isArray(v),`missing ${name}`);return v;}
function object(v,name){assert.ok(v&&typeof v==='object'&&!Array.isArray(v),`missing ${name}`);return v;}
function bool(v,name){assert.equal(typeof v,'boolean',`missing ${name}`);return v;}
function required(o,k,name=k){assert.ok(own(o,k),`missing ${name}`);return o[k];}
function pick(o,keys){return Object.fromEntries(keys.map(k=>[k,required(o,k)]));}
function uniqueIds(rows,name){const ids=list(rows,name).map(r=>r.id);assert.ok(ids.every(id=>typeof id==='string'&&id.length),`invalid ${name} ID`);assert.equal(new Set(ids).size,ids.length,`duplicate ${name} ID`);return ids;}
const sorted=(rows,key='id')=>[...rows].sort((a,b)=>String(a[key]).localeCompare(String(b[key])));
const digest=v=>createHash('sha256').update(v).digest('hex');
function features(rows){uniqueIds(rows,'entities');return sorted(rows).map(f=>{const p=object(f.properties,'entity properties');const parentId=required(f,'parentId'),coverageMode=required(f,'coverageMode'),entityKind=required(f,'entityKind');assert.equal(parentId,p.parentId,'conflicting entity parent');assert.equal(coverageMode,p.coverageMode,'conflicting entity coverage');assert.equal(entityKind,p.entityKind,'conflicting entity kind');return {...f,geometry:exactGeometryBoundary(f.geometry)};});}
function rawFeatures(rows){uniqueIds(rows,'move features');return rows.map(f=>({id:f.id,geometry:required(f,'geometry')}));}
function previewFeatures(rows){return rows.map(f=>({id:f.id,parentId:f.properties.parentId,coverageMode:f.properties.coverageMode,entityKind:f.properties.entityKind,properties:f.properties,geometry:f.geometry}));}
function webPresentation(state){const p=object(state.presentation,'web presentation'),visibility=object(p.itemVisibility,'itemVisibility'),labels=object(p.labelSettings,'labelSettings'),layers=object(p.layerPresentation,'layerPresentation');return {
 hiddenItems:Object.keys(visibility).sort().flatMap(group=>Object.keys(visibility[group]).sort().filter(id=>visibility[group][id]===false).map(id=>({group,id}))),
 objectStyles:Object.keys(object(layers.objectStyles,'object styles')).sort().map(key=>({key,...layers.objectStyles[key]})),
 labelSettings:Object.keys(labels).sort().map(key=>{assert.ok(key.startsWith('territorial:'),'unsupported label domain');const {visible,...payload}=labels[key];return {id:key.slice('territorial:'.length),pinned:false,collisionGroup:'map',...payload};})
};}
function webReferences(state){return [...list(state.document.distributionEntries,'distribution entries').filter(e=>e.territorialUnitId).map(e=>({kind:'distribution',id:e.id,target:e.territorialUnitId})),...Object.keys(state.presentation.labelSettings).sort().map(k=>({kind:'label-settings',id:k.slice('territorial:'.length),target:k.slice('territorial:'.length)}))];}
function nativePresentation(state){
 const p=object(state.nativeCanonicalDocument?.presentation?.webPresentation,'native canonical presentation'),hidden=object(p.hiddenItems,'native canonical hidden items');
 const hiddenItems=Object.keys(hidden).sort().flatMap(group=>list(hidden[group],'native hidden group').map(id=>({group,id})).sort((a,b)=>a.id.localeCompare(b.id)));
 const labelSettings=list(p.labelSettings,'native canonical label settings').map(setting=>{const {ref,...payload}=setting;assert.equal(ref?.domain,'territorial','wrong label reference domain');assert.equal(typeof ref.id,'string','missing label reference ID');return {id:ref.id,...payload};}).sort((a,b)=>a.id.localeCompare(b.id));
 const objectStyles=Object.keys(object(p.objectStyles,'native canonical object styles')).sort().map(key=>({key,...p.objectStyles[key]}));return {hiddenItems,labelSettings,objectStyles};
}
function history(state,native){const h=object(state.history,'history');if(native)return {canUndo:bool(h.canUndo,'canUndo'),canRedo:bool(h.canRedo,'canRedo')};for(const k of ['undo','redo'])assert.ok(Number.isInteger(h[k])&&h[k]>=0,`missing web ${k} depth`);return {canUndo:h.undo>0,canRedo:h.redo>0};}
function nativeCanonical(stage){return object(stage.state.nativeCanonicalDocument,'native canonical document');}
function exactWeb(stage){return {document:stage.state.document,presentation:stage.state.presentation};}
function outcome(stage){return object(stage.outcome,'outcome');}
function selectionReceipt(stage,native){
 const ref=row=>{object(row,'selection ref');assert.ok(typeof row.domain==='string'&&row.domain.length,'selection domain');assert.ok(typeof row.id==='string'&&row.id.length,'selection ID');return pick(row,['domain','id']);};
 const state=native?null:object(stage.selection,'web selection');const raw=list(native?stage.selectionItems:state.items,'selection items'),items=raw.map(ref);
 assert.equal(new Set(items.map(JSON.stringify)).size,items.length,'duplicate selection refs');let primary;
 if(native){const value=object(stage.selection,'native primary selection');primary=Object.keys(value).length?ref(value):null;}
 else{assert.deepEqual(list(state.keys,'web selection keys'),raw.map(row=>row.key),'web selection keys');const key=required(state,'primaryKey');const value=key===null?null:raw.find(row=>row.key===key);assert.ok(key===null||value,'web primary must name a selected item');primary=value?ref(value):null;}
 assert.equal(primary===null,items.length===0,'selection primary presence');assert.ok(primary===null||items.some(row=>equal(row,primary)),'selection primary must name a selected item');return {items,primary};
}
function canonicalHashCheck(stage){
 const state=stage.state;assert.match(state.documentSha256,/^[a-f0-9]{64}$/,'missing native document SHA');bool(state.unchangedFromBefore,'native unchangedFromBefore');
 assert.equal(typeof state.canonicalBytesBase64,'string','missing canonical base64 bytes');assert.ok(state.canonicalBytesBase64.length>0&&state.canonicalBytesBase64.length<=24*1024*1024,'invalid canonical byte length');
 const bytes=Buffer.from(state.canonicalBytesBase64,'base64');assert.equal(bytes.toString('base64'),state.canonicalBytesBase64,'invalid canonical base64 encoding');
 assert.equal(digest(bytes),state.documentSha256,'canonical byte SHA mismatch');
 assert.deepEqual(JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(bytes)),state.nativeCanonicalDocument,'canonical document differs from authenticated bytes');
}
function distributionPayload(state,native){
 const canonical=state.nativeCanonicalDocument;
 if(native&&canonical?.content){return sorted(list(canonical.content.distributionEntries,'native distribution payload')).map(e=>{assert.equal(e.territory?.domain,'territorial','wrong distribution reference domain');return ({id:e.id,layerId:e.layerId,territorialUnitId:e.territory?.id??'',value:e.value,certainty:e.certainty,validFrom:e.validity?.from,validTo:e.validity?.to,metadata:e.metadata,geometry:e.geometryRef});});}
 return sorted(list(state.document.distributionEntries,'distribution payload')).map(e=>pick(e,own(e,'certainty')?['id','layerId','territorialUnitId','value','certainty','validFrom','validTo','metadata','geometry']:['id','layerId','territorialUnitId','value']));
}


// The two captured descendant workflows preserve live labelSettings through web
// history. Validate the exact demonstrated omission; never ignore presentation.
const LIVE_LABEL_HISTORY_CASES=new Set(['root-descendant-effects','child-descendant-effects']);
function expectedUndo(row,native){
 const before=row.stages.before,expected=structuredClone(native?nativeCanonical(before):exactWeb(before));
 if(!LIVE_LABEL_HISTORY_CASES.has(row.case))return expected;
 const removed=list(row.stages.preview?.state?.preview?.removedIds??row.topologyHelper?.preview?.removedIds,'actual confirmed removed IDs');
 assert.deepEqual([...removed].sort(),['removed','removed-deep'],'unexpected boundary history removal set');
 const inputIds=uniqueIds(row.input.features,'history input features'),initialIds=uniqueIds(before.state.document.entities,'history initial entities');assert.deepEqual([...initialIds].sort(),[...inputIds].sort(),'history input identities changed');
 for(const id of removed){assert.ok(inputIds.includes(id),'removed history label outside actual input');assert.ok(!row.stages.confirm.state.document.entities.some(e=>e.id===id),'removed history entity still exists at confirm');}
 if(native){const presentation=expected.presentation.webPresentation;for(const id of removed)assert.ok(presentation.labelSettings.some(s=>s.ref.domain==='territorial'&&s.ref.id===id),'missing original native label reference');presentation.labelSettings=presentation.labelSettings.filter(s=>s.ref.domain!=='territorial'||!removed.includes(s.ref.id));}
 else for(const id of removed){const key='territorial:'+id;assert.ok(own(expected.presentation.labelSettings,key),'missing original web label reference');delete expected.presentation.labelSettings[key];}
 return expected;
}
function timelineReferences(state,native){
 const document=native?state.nativeCanonicalDocument:state.document, timeline=object(document.timelineRecords,'canonical timeline references');
 const versions=list(native?document.geometries:document.geometryVersions,'canonical geometry versions'), entities=list(state.document.entities,'entity observations');
 const relations=sorted(list(timeline.parentRelations,'canonical parent relations')),lifetimes=sorted(list(timeline.lifetimes,'canonical lifetimes'));
 const bindings=sorted(list(timeline.geometryBindings,'canonical geometry bindings')).map(binding=>{
  const ref=object(binding.geometryRef,'canonical geometry reference'),expectedId=native?binding.entityId:'territorial-geometry:'+binding.entityId;
  assert.equal(ref.id,expectedId,'geometry reference points to wrong actual entity');assert.ok(Number.isInteger(ref.version)&&ref.version>0,'invalid geometry version reference');
  const matches=versions.filter(g=>g.id===ref.id&&g.version===ref.version);assert.equal(matches.length,1,'missing or duplicate referenced geometry version');
  const geometry=exactGeometryBoundary(matches[0].geojson), entity=entities.find(e=>e.id===binding.entityId);assert.ok(entity,'orphaned geometry binding');assert.deepEqual(geometry,exactGeometryBoundary(entity.geometry),'canonical geometry reference disagrees with entity receipt');
  const {geometryRef,...rest}=binding;return {...rest,geometryRef:{id:binding.entityId,geometry}};
 });
 for(const entity of entities){const relation=relations.find(r=>r.entityId===entity.id);assert.ok(relation,'missing canonical parent relation');assert.equal(relation.parentId,entity.parentId,'canonical parent disagrees with entity receipt');assert.equal(relation.coverageMode,entity.coverageMode,'canonical coverage disagrees with entity receipt');}
 return {schemaVersion:timeline.schemaVersion,lifetimes,relations,bindings};
}

export function compareBoundaryCase(web,native){
 const differences=[],unobserved=[];let helperObserved=false,controllerStages=0;
 const diff=(field,a,b)=>{if(!equal(a,b))differences.push({field,web:a,native:b});};
 const check=(field,fn)=>{try{fn();}catch(error){differences.push({field,error:error.message});}};
 check('case.schema',()=>{object(web,'browser case');object(native,'native case');assert.equal(native.case,web.case,'case identity');assert.equal(native.error,undefined,'native probe error');assert.equal(web.input?.id,web.case);diff('input',web.input,native.input);bool(web.entry?.ok,'web entry result');bool(native.entry?.ok,'native entry result');diff('entry.ok',web.entry.ok,native.entry.ok);});
 if(!web||!native)return {case:web?.case,passed:false,rawParity:false,differences,unobserved};
 const staleSelectionStimulus=['root-stale-preparation','root-stale-move','root-stale-preview'].includes(web.case);
 if(staleSelectionStimulus)unobserved.push({case:web.case,scope:'settled selection parity after different stale-response stimuli',reason:'Browser generation/stateRevision invalidation has no public native equivalent during active geometry editing; native uses real selectCountry(C). Side-specific selection transitions and unchanged canonical/history state are required. Actual settled outcomes remain raw differences: selection membership alone can preserve native preparation/preview while the web generation/revision stimulus rejects its operation. This is mechanism coverage, not equivalent stale-project input or web selection parity.'});
 const selectionInputGap=web.case==='missing-selected-owner'||web.case==='mixed-domain-selection';
 if(selectionInputGap)unobserved.push({case:web.case,scope:'selection arrays for absent-object input references',reason:'Native setSelection sanitizes absent refs; the web selection stores them. Both real entrypoints must reject and preserve canonical state/history. This is not equivalent selection-state input.'});
 const inputGap=INPUT_GAPS.has(web.case)&&native.stages?.drag?.observed===false&&native.stages.drag.reason===INPUT_REASON;
 if(inputGap){unobserved.push({scope:'controller geographic input and downstream lifecycle',case:web.case,reason:INPUT_REASON});check('input-gap.receipt',()=>{assert.deepEqual(native.inputObservations,[]);assert.equal(native.stages.settled?.outcome?.inputObserved,false);assert.equal(native.stages.settled?.outcome?.ok,false);assert.equal(native.gesture?.ok,true);});}
 check('observation-limits',()=>{for(const field of ['browserPointerProjection','historyDepth','labelSettingVisible','nativeRawPreviewGeometry','workerResultInterception'])assert.equal(native.observationLimits?.[field],false,`missing explicit native limit ${field}`);});
 check('exclusions',()=>{for(const row of [web,native])for(const key of ['exclusions','excludedStages','skipStages'])assert.ok(!own(row,key),'unexpected exclusion '+key);});
 for(const name of STAGES){
  check(name,()=>{
   const w=object(web.stages?.[name],`web ${name} stage`),n=object(native.stages?.[name],`native ${name} stage`);bool(w.observed,`web ${name} observation`);bool(n.observed,`native ${name} observation`);
   if(!w.observed){if(web.input.expected.confirm&&['drag','preview','cancel','confirm','undo','redo'].includes(name))assert.fail(`required successful browser ${name} stage is unobserved`);assert.ok(WEB_ABSENCE_REASONS.has(w.reason),`undeclared web ${name} absence: ${w.reason}`);assert.equal(n.observed,false,`unexpected native ${name} stage`);assert.ok(NATIVE_ABSENCE_REASONS.has(n.reason),`undeclared native ${name} absence: ${n.reason}`);return;}
   if(!n.observed){assert.ok(inputGap&&['drag','preview','cancel','impactCancel','confirm','undo','redo'].includes(name),`required native ${name} stage is unobserved`);assert.ok(name==='drag'?n.reason===INPUT_REASON:n.reason===NO_NATIVE_STAGE,`undeclared input-limited ${name} absence`);return;}
   controllerStages++;
   object(w.state,`web ${name} state`);object(n.state,`native ${name} state`);canonicalHashCheck(n);nativeCanonical(n);
   if(w.outcome&&own(w.outcome,'ok')&&!(inputGap&&name==='settled'))diff(`${name}.outcome.ok`,bool(w.outcome.ok,'web outcome'),bool(outcome(n).ok,'native outcome'));
   const ws=w.state,ns=n.state;
   // A known missing projection input does not excuse a mutated canonical state.
   if(inputGap&&name==='settled'){
    diff(`${name}.native.input-limited.canonical`,nativeCanonical(native.stages.before),nativeCanonical(n));diff(`${name}.native.input-limited.history`,history(native.stages.before.state,true),history(ns,true));
   }else{
    diff(`${name}.entities.geometry-and-properties`,features(ws.document.entities),features(ns.document.entities));
    diff(`${name}.timeline.references`,timelineReferences(ws,false),timelineReferences(ns,true));
    diff(`${name}.references`,webReferences(ws),list(ns.references,'native references'));
    diff(`${name}.distribution.supportedPayload`,distributionPayload(ws,false),distributionPayload(ns,true));
    const wp=webPresentation(ws),np=object(ns.presentation,'native presentation');diff(`${name}.presentation.native-canonical-receipt`,nativePresentation(ns),np);
    for(const k of ['hiddenItems','objectStyles','labelSettings'])diff(`${name}.presentation.${k}`,wp[k],list(np[k],`native ${k}`));
    diff(`${name}.history.public`,history(ws,false),history(ns,true));
   }
   if(!(inputGap&&name==='settled')){
    const webSelection=selectionReceipt(w,false),nativeSelection=selectionReceipt(n,true),webRefs=webSelection.items,nativeRefs=nativeSelection.items;
    if(selectionInputGap){
     assert.equal(web.entry.ok,false,'invalid selection must reject web entry');assert.equal(native.entry.ok,false,'invalid selection must reject native entry');
     const expectedInvalid=web.case==='missing-selected-owner'?{domain:'territorial',id:'missing'}:{domain:'generic',id:'B'};
     assert.deepEqual(webRefs,[{domain:'territorial',id:'A'},expectedInvalid],'undeclared invalid-reference input');
     assert.ok(!web.input.features.some(f=>expectedInvalid.domain==='territorial'&&f.id===expectedInvalid.id),'declared absent territorial ref actually exists');
     assert.equal(web.input.genericFeatures?.length??0,0,'valid generic selection cannot use missing-ref exception');
     diff(`${name}.selection.sanitizedAbsentRefs`,[{domain:'territorial',id:'A'}],nativeRefs);
    }else if(staleSelectionStimulus&&name==='settled'){
     const changedSelectionId=web.input.features.at(-1)?.id;assert.equal(typeof changedSelectionId,'string');assert.ok(web.input.selectedIds.includes(changedSelectionId),'stale selection target must be an existing selected owner');
     const baseline=selectionReceipt(web.stages.pending,false).items;diff('settled.selection.webUnchangedAfterGeneration',baseline,webRefs);
     diff('settled.selection.nativeRealSelectionAction',[{domain:'territorial',id:changedSelectionId}],nativeRefs);
     const trigger=web.case==='root-stale-preparation'?'selection revision change before owner delivery':'selection revision change before preview delivery';assert.equal(n.outcome.trigger,trigger,'undeclared native stale stimulus');
    }else diff(`${name}.selection.items`,webRefs,nativeRefs);
    diff(`${name}.selection.primary`,webSelection.primary,nativeSelection.primary);
   }else{
    selectionReceipt(w,false);
    diff('settled.selection.nativeInputGapPreserved',selectionReceipt(native.stages.prepared,true),selectionReceipt(n,true));
   }
   if(name==='settled'){
    bool(n.edit?.active,'settled session state');if(n.edit.active){assert.equal(n.edit.calculating,false,'settled worker must not be calculating');if(!staleSelectionStimulus)assert.equal(n.edit.previewReady,false,'settled failure must not reopen preview');}
    if(staleSelectionStimulus){
     assert.equal(n.edit.active,true,'public selection must retain the boundary session');bool(n.edit.previewReady,'actual native preview outcome');assert.ok(['ready','error'].includes(n.edit.boundaryStatus),'actual native preparation outcome');
     assert.equal(n.outcome.ok,web.input.scenario==='stale-preparation'?n.edit.boundaryStatus==='ready':n.edit.previewReady,'native outcome must reflect actual preparation or preview');
     diff('settled.nonEquivalentStimuli.boundaryStatus',w.preparation?.status??null,n.edit.boundaryStatus);
     diff('settled.nonEquivalentStimuli.previewReady',!!w.state.preview,n.edit.previewReady);
     diff('settled.native.selectionOnly.canonical',nativeCanonical(native.stages.before),nativeCanonical(n));diff('settled.native.selectionOnly.history',history(native.stages.before.state,true),history(ns,true));
     assert.equal(ns.canonicalBytesBase64,native.stages.before.state.canonicalBytesBase64,'selection-only settled exact canonical bytes');assert.equal(ns.documentSha256,native.stages.before.state.documentSha256,'selection-only settled canonical hash');assert.equal(ns.unchangedFromBefore,true,'selection-only settled unchanged canonical status');
    }
    if(w.outcome?.ok===true&&!inputGap)assert.equal(n.edit.active,false,'confirmed workflow must exit session');
    if(['pending-cancel','pending-preview-cancel'].includes(web.input.scenario))assert.equal(n.edit.active,false,'cancelled workflow reopened session');
   }
   if(name==='drag')for(const field of ['changed','coordinate'])diff(`drag.${field}`,required(outcome(w),field),required(outcome(n),field));
   if(name==='pending'&&native.entry.ok){assert.equal(n.edit?.active,true,'pending active state');assert.equal(n.edit?.calculating,true,'pending calculating state');assert.equal(n.edit?.boundaryStatus,'preparing','pending preparation state');}
   if(name==='prepared'){assert.equal(n.edit?.calculating,false,'preparation must settle');assert.equal(n.edit?.boundaryStatus,w.outcome.ok?'ready':'error','prepared status');}
   if(name==='preview'){assert.equal(outcome(n).ok,true,'native preview result');assert.equal(n.edit?.previewReady,true,'native preview ready');}
   if(['before','cold','pending','prepared','drag','preview','cancel','impactCancel'].includes(name)){
    diff(`${name}.web.canonical-before`,exactWeb(web.stages.before),exactWeb(w));diff(`${name}.native.canonical-before`,nativeCanonical(native.stages.before),nativeCanonical(n));assert.equal(ns.documentSha256,native.stages.before.state.documentSha256,`${name} canonical SHA changed`);assert.equal(ns.unchangedFromBefore,true,`${name} canonical bytes changed`);
   }
  });
 }
 check('gesture',()=>{if(web.gesture){bool(web.gesture.ok,'web gesture');bool(native.gesture?.ok,'native gesture');diff('gesture.ok',web.gesture.ok,native.gesture.ok);}else assert.equal(native.gesture,undefined,'unexpected gesture');});
 check('helper',()=>{
  if(!web.preparation){assert.equal(native.topologyHelper,undefined,'unexpected helper receipt');return;}
  const h=object(native.topologyHelper,'topology helper');assert.equal(h.observed,true,'missing helper observation');helperObserved=true;const p=object(h.preparation,'helper preparation');diff('helper.preparation.valid',bool(web.preparation.valid,'web valid'),bool(p.valid,'native valid'));
  if(!web.preparation.valid)return;
  const handleKeys=['nodeKey','coordinate','polygonIndex','ringIndex','index','refs','virtualRefs','ownerIds','fixed','segments'];
  diff('helper.handles.ordered',list(web.preparation.handles,'web handles').map(h=>pick(h,handleKeys)),list(p.handles,'native handles').map(h=>pick(h,handleKeys)));
  diff('helper.segments.ordered',list(web.preparation.segments,'web segments').map(h=>pick(h,['key','start','end'])),list(p.segments,'native segments').map(h=>pick(h,['key','start','end'])));
  diff('helper.selectedIds',web.preparation.selectedIds,list(h.selectedIdsInInputOrder,'helper selected IDs'));
  const sourceFeatures=new Map(web.input.features.map(feature=>[feature.id,feature]));
  const validateRawRef=(reference,virtual)=>{
   const feature=sourceFeatures.get(reference.featureId);assert.ok(feature,'raw reference names an absent input feature');
   const geometry=feature.geometry,polygons=geometry.type==='Polygon'?[geometry.coordinates]:geometry.coordinates;
   for(const key of ['polygonIndex','ringIndex',virtual?'segmentIndex':'vertexIndex'])assert.ok(Number.isInteger(reference[key])&&reference[key]>=0,'invalid raw reference index '+key);
   const ring=polygons[reference.polygonIndex]?.[reference.ringIndex];assert.ok(Array.isArray(ring),'raw reference polygon/ring index is out of range');
   const index=reference[virtual?'segmentIndex':'vertexIndex'];assert.ok(index<(virtual?ring.length-1:ring.length),'raw reference coordinate index is out of range');
   if(virtual)assert.ok(Number.isFinite(reference.t)&&reference.t>=0&&reference.t<=1,'invalid raw virtual interpolation');
  };
  for(const handle of p.handles){list(handle.rawRefs,'raw refs');list(handle.rawVirtualRefs,'raw virtual refs');handle.rawRefs.forEach(ref=>validateRawRef(ref,false));handle.rawVirtualRefs.forEach(ref=>validateRawRef(ref,true));diff('helper.refs.selected-owner-filter',handle.refs,handle.rawRefs.filter(r=>web.preparation.selectedIds.includes(r.featureId)));diff('helper.virtualRefs.selected-owner-filter',handle.virtualRefs,handle.rawVirtualRefs.filter(r=>web.preparation.selectedIds.includes(r.featureId)));}
  diff('helper.gesture.ok',web.gesture?.ok,bool(h.gesture?.ok,'helper gesture result'));
  if(web.gesture?.ok!==true)return;
  const m=object(h.move,'helper move result');diff('helper.move.changed',required(web.stages.drag.outcome,'changed'),bool(m.changed,'helper move changed'));diff('helper.move.coordinate',web.input.move.coordinate,required(m,'coordinate'));
  // Worker cancellation means no web returned move result; coordinates/visuals remain observable.
  if(own(web,'movedFeatures')){diff('helper.move.features.rawOrdered',rawFeatures(web.movedFeatures),rawFeatures(list(m.features,'helper move features')));diff('helper.move.movedOwnerIds.rawOrdered',web.movedOwnerIds,list(m.movedOwnerIds,'helper moved owners'));}
  const visual=list(web.visualEvents,'browser visual events').find(e=>e.kind==='move');if(visual)diff('helper.activeSegments.ordered',visual.value,list(h.activeSegments,'helper active segments'));
  if(web.stages.preview.observed){
   const preview=object(h.preview,'helper preview result');assert.equal(preview.ok,true,'helper preview rejected');assert.equal(preview.prepareOk,true,'helper prepared receipt rejected');
   const wp=object(web.stages.preview.state.preview,'browser canonical preview');
   const changes=list(preview.rows,'helper preview rows');uniqueIds(changes,'helper preview rows');
   diff('helper.preview.affectedIds',[...wp.affectedIds].sort(),changes.map(r=>r.id).sort());
   const browserAfter=new Map(wp.afterFeatures.map(f=>[f.id,f]));
   for(const row of changes){const before=web.stages.before.state.document.entities.find(f=>f.id===row.id);assert.ok(before,'helper preview unknown entity');diff(`helper.preview.before.${row.id}`,exactGeometryBoundary(before.geometry),exactGeometryBoundary(row.before));const after=browserAfter.get(row.id);if(after)diff(`helper.preview.after.${row.id}`,exactGeometryBoundary(after.geometry),exactGeometryBoundary(row.after));else assert.equal(row.after,null,'removed preview geometry must be null');}
   diff('helper.preview.removedIds',[...wp.removedIds].sort(),[...list(preview.removedIds,'helper removed IDs')].sort());
   const expected=web.stages.before.state.document.entities.filter(f=>!wp.removedIds.includes(f.id)).map(f=>browserAfter.has(f.id)?previewFeatures([browserAfter.get(f.id)])[0]:f);
   diff('helper.preview.entities.geometry-and-properties',features(expected),features(list(preview.entities,'helper prepared entities')));
  }else if(m.features.length&&!web.input.scenario){assert.equal(h.preview?.ok,false,'invalid moved candidate must have an observed rejected helper preview');}
 });
 check('controller.input',()=>{if(native.stages?.drag?.observed){const observations=list(native.inputObservations,'native input observations');assert.equal(observations.length,1,'one exact input required');const receipt=observations[0];assert.equal(receipt.exact,true,'inexact input');diff('controller.input.intended',web.input.move.coordinate,receipt.intended);diff('controller.input.inverse',receipt.intended,receipt.inverse);}});
 check('history.undo-redo',()=>{
  if(!native.stages?.confirm?.observed)return;
  for(const side of [web,native]){assert.equal(side.stages.confirm.outcome.ok,true,'confirm failed');for(const name of ['undo','redo']){assert.equal(side.stages[name]?.observed,true,`missing ${name}`);assert.equal(side.stages[name].outcome.ok,true,`${name} failed`);}}
  diff('undo.web.exactExpectedCanonical',expectedUndo(web,false),exactWeb(web.stages.undo));diff('redo.web.exactCanonical',exactWeb(web.stages.confirm),exactWeb(web.stages.redo));
  diff('undo.native.exactExpectedCanonical',expectedUndo(native,true),nativeCanonical(native.stages.undo));diff('redo.native.exactCanonical',nativeCanonical(native.stages.confirm),nativeCanonical(native.stages.redo));
  if(LIVE_LABEL_HISTORY_CASES.has(web.case)){assert.notEqual(native.stages.undo.state.documentSha256,native.stages.before.state.documentSha256,'live-label Undo must observe changed canonical SHA');assert.equal(native.stages.undo.state.unchangedFromBefore,false,'live-label Undo must observe actual changed bytes');}else assert.equal(native.stages.undo.state.documentSha256,native.stages.before.state.documentSha256,'undo native exact SHA');assert.equal(native.stages.redo.state.documentSha256,native.stages.confirm.state.documentSha256,'redo native exact SHA');
  assert.equal(web.stages.confirm.state.history.undo,web.stages.before.state.history.undo+1,'web one atomic history step');
 });
 check('replay',()=>{
  const expected=['cancel','impactCancel'].filter(name=>native.stages?.[name]?.observed);const replays=list(native.replayGestures,'native cancellation replays');diff('replay.count-and-order',expected,replays.map(r=>r.afterCancel));
  for(const r of replays){canonicalHashCheck(r.before);canonicalHashCheck(r.after);for(const stage of [r.before,r.after]){assert.equal(stage.state.unchangedFromBefore,true,'replay unchanged canonical status');assert.equal(stage.state.canonicalBytesBase64,native.stages.before.state.canonicalBytesBase64,'replay exact initial bytes');assert.equal(stage.state.documentSha256,native.stages.before.state.documentSha256,'replay exact initial SHA');}diff('replay.before.history',history(native.stages[r.afterCancel].state,true),history(r.before.state,true));diff('replay.after.history',history(native.stages.preview.state,true),history(r.after.state,true));diff('replay.before.selection',selectionReceipt(native.stages[r.afterCancel],true),selectionReceipt(r.before,true));diff('replay.after.selection',selectionReceipt(native.stages.preview,true),selectionReceipt(r.after,true));for(const k of ['foundOriginalHandle','picked','began','moved','automaticPreviewReady'])assert.equal(r[k],true,`replay ${k}`);diff('replay.original-node',web.input.move.nodeKey,r.requestedNodeKey);diff('replay.actual-handle',web.input.move.nodeKey,r.handle?.nodeKey);diff('replay.coordinate',web.input.move.coordinate,r.coordinate);diff('replay.preparedDraftPaths',native.stages.prepared.draftPaths,r.before?.draftPaths);diff('replay.canonical-before',nativeCanonical(native.stages.before),nativeCanonical(r.before));diff('replay.canonical-after',nativeCanonical(native.stages.before),nativeCanonical(r.after));assert.equal(r.after?.edit?.previewReady,true,'replay did not return to preview');}
 });
 check('stage.unexpected',()=>{for(const name of Object.keys(native.stages??{}))assert.ok(STAGES.includes(name)||name==='delayed','unexpected native stage '+name);for(const name of Object.keys(web.stages??{}))assert.ok(STAGES.includes(name)||name==='delayed','unexpected web stage '+name);
  if(own(web.stages,'delayed')){
   assert.ok(DELAYED_GAPS.has(web.case),'undeclared delayed stage');const w=web.stages.delayed,n=object(native.stages.delayed,'required native delayed observation');assert.equal(w.observed,true);assert.equal(native.observationLimits?.workerResultInterception,false,'browser RPC interception must remain unclaimed');
   if(web.case==='root-stale-move'){
    assert.equal(n.observed,false,'synchronous native boundary-move cannot observe held worker reply');assert.equal(n.reason,'Native boundary-move is synchronous; no completed boundary-move worker reply exists to withhold.','undeclared synchronous move reason');assert.equal(native.observationLimits.completedWorkerOwnerDeliveryWithheld,false);unobserved.push({case:web.case,scope:'native boundary-move worker-result interception',reason:n.reason});
   }else{
    assert.equal(n.observed,true,'required completed-worker delayed stage');assert.equal(native.observationLimits.completedWorkerOwnerDeliveryWithheld,true,'missing completed-worker observation flag');const barrier=object(native.deliveryBarrier,'completed-worker delivery barrier');
    assert.deepEqual(barrier,{mechanism:'native-worker-completed-owner-delivery-withheld',timeoutMs:30000,completed:true,ownerEventsProcessed:false},'incomplete or fabricated delivery barrier');assert.equal(n.edit?.calculating,true,'owner delivery must still be pending after worker completion');assert.equal(n.edit?.active,true,'delayed session must remain active');canonicalHashCheck(n);diff('delayed.selection',selectionReceipt(w,false),selectionReceipt(n,true));
    diff('delayed.web.canonical-before',exactWeb(web.stages.before),exactWeb(w));diff('delayed.native.canonical-before',nativeCanonical(native.stages.before),nativeCanonical(n));assert.equal(n.state.documentSha256,native.stages.before.state.documentSha256,'delayed canonical SHA changed');assert.equal(n.state.unchangedFromBefore,true,'delayed canonical bytes changed');diff('delayed.history.public',history(w.state,false),history(n.state,true));
   }
  }else assert.ok(!own(native.stages,'delayed'),'unexpected native delayed stage');
 });
 return {case:web.case,passed:differences.length===0,rawParity:false,helperObserved,controllerStages,differences,unobserved};
}

export function compareBoundaryObservations(web,native,options={}){
 const differences=[];let rows=[];try{assert.deepEqual(Object.keys(options).filter(k=>k!=='expectedCaseIds'),[],'unexpected comparison exclusions/options');list(web,'browser cases');assert.equal(native?.schema,'pando-m974-native-boundary-workflows','missing or wrong native workflow schema');assert.equal(native.version,1,'unsupported native workflow version');rows=list(native.rows,'native cases');const expected=options.expectedCaseIds??BOUNDARY_CASE_IDS;assert.deepEqual(web.map(r=>r.case),expected,'complete ordered browser cases');assert.deepEqual(rows.map(r=>r.case),expected,'complete ordered native cases');assert.equal(new Set(expected).size,expected.length,'duplicate case IDs');}catch(error){differences.push({field:'corpus',error:error.message});}
 const results=Array.isArray(web)?web.map(w=>compareBoundaryCase(w,rows.find(n=>n.case===w.case))):[];
 return {schema:'pando-m974-native-boundary-comparison',version:1,passed:differences.length===0&&results.every(r=>r.passed),rawParity:false,coverage:{expectedCases:options.expectedCaseIds?.length??BOUNDARY_CASE_IDS.length,browserCases:web?.length??0,nativeCases:rows.length,helperCases:results.filter(r=>r.helperObserved).length,controllerStages:results.reduce((n,r)=>n+(r.controllerStages??0),0)},representationMappings:REPRESENTATION_MAPPINGS,limits:['Browser pixel pointer projection/hit-testing and GPU rendering are not observed.','Native raw preview packet, parent preview presentation, and private history depth remain unobserved. Helper prepared candidates do not replace controller observations.','Web LabelSettings.visible has no native counterpart; label-reference existence and all supported native payload fields are checked separately.'],differences,unobserved:results.flatMap(r=>r.unobserved),results};
}

export function compareBoundaryCapture({browserDirectory,nativeFile,outputFile,expectedCommit,expectedRunId,nativeBinarySha256,nativeCommit}){
 const pin=JSON.parse(fs.readFileSync(path.join(browserDirectory,'suite-pin.json'))),suiteBytes=gunzipSync(fs.readFileSync(path.join(browserDirectory,'suite.json.gz')));assert.equal(digest(suiteBytes),pin.decompressedSha256,'suite bytes');assert.equal(suiteBytes.length,pin.decompressedBytes,'suite length');const suite=verifyCaptureSuite(JSON.parse(suiteBytes));assert.deepEqual(suite.identity,pin.identity,'suite pin identity');
 const reportBytes=fs.readFileSync(path.join(browserDirectory,'browser-report.json')),transfer=JSON.parse(fs.readFileSync(path.join(browserDirectory,'report-transfer.json')));assert.equal(digest(reportBytes),transfer.sha256,'browser report bytes');assert.equal(reportBytes.length,transfer.bytes,'browser report length');const browser=verifyBrowserReport(suite,JSON.parse(reportBytes));verifyBoundaryCaptureIdentity(suite,browser,{expectedCommit,expectedRunId,nativeBinarySha256,nativeCommit});
 const boundaryBytes=fs.readFileSync(path.join(browserDirectory,'boundary-observations.json'));assert.deepEqual(JSON.parse(boundaryBytes),browser.boundary,'standalone boundary receipt is exact browser report extraction');
 const nativeBytes=fs.readFileSync(nativeFile);const comparison=compareBoundaryObservations(browser.boundary,JSON.parse(nativeBytes));comparison.provenance={expectedCommit,expectedRunId,browserCommit:suite.identity.commit,browserRunId:suite.identity.runId,identity:suite.identity,nativeRuntime:{commit:nativeCommit??null,binarySha256:nativeBinarySha256??null,verification:'Runtime binary and COMMIT verification is external to this comparator; supplied identities are recorded, never inferred from an older browser capture.'},browserReportSha256:digest(reportBytes),boundaryObservationsSha256:digest(boundaryBytes),nativeObservationsSha256:digest(nativeBytes)};
 if(outputFile)fs.writeFileSync(outputFile,JSON.stringify(comparison,null,2)+'\n');return comparison;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const r=compareBoundaryCapture(parseBoundaryCaptureArgs(process.argv.slice(2)));console.log(JSON.stringify({passed:r.passed,rawParity:r.rawParity,coverage:r.coverage,failures:r.results.filter(x=>!x.passed).map(x=>({case:x.case,differences:x.differences})),unobserved:r.unobserved},null,2));if(!r.passed)process.exitCode=1;}catch(error){console.error(error.stack);process.exitCode=1;}
}

export function verifyBoundaryCaptureIdentity(suite,browser,{expectedCommit,expectedRunId,nativeBinarySha256,nativeCommit}={}){
 assert.match(expectedCommit??'',/^[a-f0-9]{40}$/,'explicit expected commit required');assert.match(expectedRunId??'',/^[0-9]+$/,'explicit expected run ID required');
 for(const [name,identity]of [['suite',suite],['suite identity',suite.identity],['browser identity',browser.identity]]){assert.equal(identity?.commit,expectedCommit,`${name} commit does not match caller expectation`);assert.equal(identity?.runId,expectedRunId,`${name} run does not match caller expectation`);}
 if(nativeCommit!==undefined){assert.match(nativeCommit,/^[a-f0-9]{40}$/,'invalid native commit');assert.equal(nativeCommit,expectedCommit,'native runtime commit mismatch');}
 if(nativeBinarySha256!==undefined)assert.match(nativeBinarySha256,/^[a-f0-9]{64}$/,'invalid native binary SHA256');
}
export function parseBoundaryCaptureArgs(args){
 const keys={'--browser-directory':'browserDirectory','--native-file':'nativeFile','--output-file':'outputFile','--expected-commit':'expectedCommit','--expected-run-id':'expectedRunId','--native-binary-sha256':'nativeBinarySha256','--native-commit':'nativeCommit'},result={};
 for(let i=0;i<args.length;i+=2){const option=args[i],key=keys[option],value=args[i+1];assert.ok(key,`unknown or positional argument ${option}`);assert.ok(!own(result,key),`duplicate option ${option}`);assert.ok(typeof value==='string'&&value.length&&!value.startsWith('--'),`missing option value ${option}`);result[key]=value;}
 for(const key of ['browserDirectory','nativeFile','expectedCommit','expectedRunId'])assert.ok(result[key],`required capture option ${key}`);return result;
}
