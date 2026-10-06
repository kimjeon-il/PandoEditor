import assert from 'node:assert/strict';
import {isDeepStrictEqual} from 'node:util';

const clone=value=>structuredClone(value);
const list=(value,name)=>{assert.ok(Array.isArray(value),`missing ${name} observations`);return value;};
const unique=(values,name)=>{assert.ok(values.every(id=>typeof id==='string'&&id.length),`invalid ${name} identity`);assert.equal(new Set(values).size,values.length,`duplicate ${name} identity`);return values;};
function geometry(value){
 assert.ok(value&&['Polygon','MultiPolygon'].includes(value.type)&&Array.isArray(value.coordinates),'missing or invalid geometry observation');
 const polygons=value.type==='Polygon'?[value.coordinates]:value.coordinates;assert.ok(polygons.length,'empty geometry');
 for(const polygon of polygons){assert.ok(Array.isArray(polygon)&&polygon.length,'invalid polygon');for(const ring of polygon){assert.ok(Array.isArray(ring)&&ring.length>=4,'invalid ring');for(const point of ring)assert.ok(Array.isArray(point)&&point.length===2&&point.every(Number.isFinite),'invalid geometry coordinate');assert.deepEqual(ring[0],ring.at(-1),'unclosed ring');}}
 return value;
}
// Geometry acceptance never calls polygon-clipping: its internal rounder
// can erase one-ULP boundary movement. These exact dyadic operations remove
// only vertices proved to be on the same straight segment between neighbors.
const dyadicCache=new Map(),doubleBytes=new DataView(new ArrayBuffer(8));
function dyadic(value){
 if(dyadicCache.has(value))return dyadicCache.get(value);doubleBytes.setFloat64(0,value,false);const hi=doubleBytes.getUint32(0,false),lo=doubleBytes.getUint32(4,false),exponent=(hi>>>20)&2047,fraction=(BigInt(hi&0xfffff)<<32n)|BigInt(lo);let n=exponent?fraction|(1n<<52n):fraction;if(hi>>>31)n=-n;const result={n,e:exponent?exponent-1023-52:-1074};dyadicCache.set(value,result);return result;
}
function subtract(a,b){const e=Math.min(a.e,b.e);return {n:(a.n<<BigInt(a.e-e))-(b.n<<BigInt(b.e-e)),e};}
function exactlyCollinear(a,b,c){const dx1=subtract(dyadic(b[0]),dyadic(a[0])),dy1=subtract(dyadic(b[1]),dyadic(a[1])),dx2=subtract(dyadic(c[0]),dyadic(b[0])),dy2=subtract(dyadic(c[1]),dyadic(b[1])),left={n:dx1.n*dy2.n,e:dx1.e+dy2.e},right={n:dy1.n*dx2.n,e:dy1.e+dx2.e};return subtract(left,right).n===0n;}
const samePoint=(a,b)=>a[0]===b[0]&&a[1]===b[1];
function canonicalRing(ring){
 const points=[];for(const point of ring.slice(0,-1))if(!points.length||!samePoint(points.at(-1),point))points.push(point);if(points.length>1&&samePoint(points[0],points.at(-1)))points.pop();
 let changed=true;while(changed&&points.length>=3){changed=false;for(let i=0;i<points.length;i++){const a=points[(i+points.length-1)%points.length],b=points[i],c=points[(i+1)%points.length];if(b[0]>=Math.min(a[0],c[0])&&b[0]<=Math.max(a[0],c[0])&&b[1]>=Math.min(a[1],c[1])&&b[1]<=Math.max(a[1],c[1])&&exactlyCollinear(a,b,c)){points.splice(i,1);changed=true;break;}}}
 assert.ok(points.length>=3,'degenerate exact boundary ring');let best=null;
 for(let i=0;i<points.length;i++)for(const direction of [1,-1]){const representation=JSON.stringify(points.map((_,offset)=>points[(i+direction*offset+points.length)%points.length]));if(best===null||representation<best)best=representation;}
 return best;
}
export function exactGeometryBoundary(value){geometry(value);const polygons=value.type==='Polygon'?[value.coordinates]:value.coordinates;return polygons.map(polygon=>JSON.stringify({exterior:canonicalRing(polygon[0]),holes:polygon.slice(1).map(canonicalRing).sort()})).sort();}
const exactGeometryEqual=(a,b)=>a===null||b===null?a===b:Boolean(a&&b&&isDeepStrictEqual(exactGeometryBoundary(a),exactGeometryBoundary(b)));
function validateRawCandidateContinuity(row,native=false){
 const a=row.stages.candidates,b=row.stages.selected;assert.ok(a&&b,'missing candidate stages');const candidates=s=>native?list(s.raw?.candidates,'raw candidates'):list(s.selection?.candidates,'raw candidates');
 for(const name of ['selected','emptied','reactivated'])if(row.stages[name])assert.deepEqual(candidates(a).map(c=>c.id),candidates(row.stages[name]).map(c=>c.id),'candidate batch identity changed without a new draft');
 const parts=s=>native?list(s.raw?.parts,'raw parts'):list(s.selection?.parts,'raw parts');assert.ok(row.stages.archive&&row.stages.review,'missing archive/review stages');assert.deepEqual(parts(row.stages.archive).map(p=>p.id),parts(row.stages.review).map(p=>p.id),'part identity changed during review');
}
function mapObjectIds(row,entities){
 const initial=unique(entities(row.stages.before).map(f=>f.id),'initial object');
 const confirmed=row.stages.confirm?entities(row.stages.confirm).filter(f=>!initial.includes(f.id)).map(f=>f.id):[];
 assert.ok(confirmed.length<=1,'split created more than one identity');
 const stable=id=>{if(initial.includes(id)||id==='')return id;if(confirmed.includes(id))return '$created';throw Error('unknown created or replaced object identity '+id);};
 return {initial,confirmed,stable,preview(features){const ids=features.filter(f=>!initial.includes(f.id)).map(f=>f.id);unique(ids,'preview object');assert.ok(ids.length<=1,'preview created more than one identity');return id=>initial.includes(id)||id===''?id:ids.includes(id)?'$created-preview':stable(id);}};
}
function observedParentId(feature){
 const hasOuter=Object.hasOwn(feature,'parentId'),hasInner=feature.properties&&Object.hasOwn(feature.properties,'parentId');
 assert.ok(hasOuter||hasInner,'missing explicit parent identity observation');
 if(hasOuter)assert.equal(typeof feature.parentId,'string','invalid parent identity');
 if(hasInner)assert.equal(typeof feature.properties.parentId,'string','invalid parent identity');
 if(hasOuter&&hasInner)assert.equal(feature.parentId,feature.properties.parentId,'conflicting parent identity observations');
 return hasOuter?feature.parentId:feature.properties.parentId;
}
function mappedFeatures(features,map){return list(features,'features').map(f=>{
 const parentId=observedParentId(f);
 const name=f.name??f.properties?.name,coverageMode=f.coverageMode??f.properties?.coverageMode;assert.equal(typeof name,'string','missing explicit object name observation');assert.equal(typeof coverageMode,'string','missing explicit coverage observation');return {id:map(f.id),parentId:map(parentId),name,coverageMode,geometry:clone(geometry(f.geometry))};
});}
function mappedPreviewFeatures(features,map){return features.map(f=>({id:map(f.id),parentId:map(observedParentId(f)),geometry:clone(geometry(f.geometry))}));}
function webReviewPreview(stage){
 assert.ok(Object.hasOwn(stage.document,'preview'),'missing explicit review preview observation');
 const preview=stage.document.preview;if(preview===null)return {present:false,features:[]};
 assert.ok(preview&&typeof preview==='object','invalid review preview observation');
 return {present:true,features:list(preview.afterFeatures,'preview features')};
}
function nativeReviewPreview(stage){
 assert.ok(Object.hasOwn(stage.raw,'splitPreview'),'missing explicit native review preview observation');
 const receipt=stage.raw.splitPreview,features=list(stage.previewFeatures,'preview features');
 for(const key of ['splitPreviewPresent','splitPreviewReceiptPresent'])assert.equal(typeof stage.raw[key],'boolean','missing explicit '+key+' observation');
 assert.equal(stage.raw.splitPreviewReceiptPresent,receipt!==null,'preview receipt presence must match the raw receipt');
 if(receipt===null){assert.equal(stage.raw.splitPreviewPresent,false,'null receipt cannot produce a preview');assert.deepEqual(features,[],'null preview must not expose preview features');return {present:false,features};}
 assert.ok(receipt&&typeof receipt==='object','invalid native preview receipt');
 assert.ok(['completed','empty','failed','cancelled'].includes(receipt.status),'missing or invalid preview receipt status');
 assert.equal(typeof receipt.detail,'string','missing preview receipt detail');assert.equal(typeof receipt.ok,'boolean','missing preview receipt validity');assert.equal(typeof receipt.blocking,'boolean','missing preview receipt blocking state');
 assert.equal(stage.raw.splitPreviewPresent,receipt.status==='completed','preview presence must match completed calculation status');
 const receiptRows=list(receipt.rows,'preview receipt rows');
 if(!stage.raw.splitPreviewPresent){assert.equal(receipt.ok,false,'failed receipt cannot be valid');assert.equal(receipt.blocking,true,'failed receipt must remain blocking');assert.deepEqual(features,[],'failed calculation must not expose preview features');return {present:false,features};}
 const rows=receiptRows.filter(row=>row.after!==null).map(row=>{
  assert.ok(row.owner&&row.owner.domain==='territorial'&&typeof row.owner.id==='string','missing preview receipt owner');
  return {id:row.owner.id,parentId:observedParentId(row),geometry:clone(geometry(row.after))};
 });
 assert.deepEqual(features,rows,'preview features must preserve the actual receipt rows and parent identities');
 return {present:true,features};
}
function selection(value,raw){
 if(value===null)return null;
 assert.ok(value&&typeof value==='object','missing selection observation');for(const key of ['combinedGeometry','remainingGeometry'])assert.ok(Object.hasOwn(raw??value,key),`missing explicit ${key} geometry observation`);assert.equal(typeof value.stage,'string','missing stage observation');assert.ok(Object.hasOwn(value,'activeMethod'),'missing active method observation');assert.ok([null,'','line','polygon','components'].includes(value.activeMethod),'invalid active method observation');assert.ok(Object.hasOwn(value,raw?'selectionPhase':'activePhase'),'missing active phase observation');assert.ok(raw?typeof value.selectionPhase==='string':value.activePhase===null||typeof value.activePhase==='string','invalid phase observation');assert.equal(typeof value.previewReady,'boolean','missing preview readiness');assert.equal(typeof value.canAddPart,'boolean','missing archive readiness');
 const candidates=list(raw?.candidates??value.candidates,'candidates'),parts=list(raw?.parts??value.parts,'parts');
 unique(candidates.map(c=>c.id),'candidate');unique(parts.map(p=>p.id),'part');for(const candidate of candidates)assert.ok(Number.isFinite(candidate.area),'missing candidate area observation');for(const part of parts)assert.ok(['line','polygon','components'].includes(part.method),'missing part method observation');
 const selected=list(value.selectedCandidateIds,'selected candidate');unique(selected,'selected candidate');
 for(const id of selected)assert.ok(candidates.some(c=>c.id===id),'selected candidate identity missing from actual candidate batch');
 const candidateId=id=>'candidate:'+candidates.findIndex(c=>c.id===id);
 const phase=value.activePhase??(value.selectionPhase==='candidates'?'candidate':['','result','sources'].includes(value.selectionPhase)?null:value.selectionPhase)??null;
 return {stage:value.stage,activeMethod:value.activeMethod||null,activePhase:phase,sourceCountryIds:raw?list(value.providers,'providers').map(p=>p.id):list(value.baseSourceFeatures,'base source features').map(f=>f.id),
  candidates:candidates.map(c=>({id:candidateId(c.id),geometry:clone(geometry(c.geometry)),area:c.area})),selectedCandidateIds:selected.map(candidateId),
  parts:parts.map((p,index)=>({id:'part:'+index,method:p.method,geometry:clone(geometry(p.geometry))})),previewReady:value.previewReady,canAddPart:value.canAddPart,
  combinedGeometry:clone(raw?raw.combinedGeometry:value.combinedGeometry),remainingGeometry:clone(raw?raw.remainingGeometry:value.remainingGeometry)};
}
function identities(stage,initial){const ids=stage.features.map(f=>f.id);return {createdIds:ids.filter(id=>!initial.includes(id)),retainedIds:initial.filter(id=>ids.includes(id)),deletedIds:initial.filter(id=>!ids.includes(id))};}
function webRefs(stage,map){const d=stage.document;return [...list(d.document.distributionEntries,'distribution entries').filter(e=>e.territorialUnitId).map(e=>({kind:'distribution',id:e.id,target:map(e.territorialUnitId)})),...list(d.labels,'labels').filter(e=>e.countryId).map(e=>({kind:'label',id:e.id,target:map(e.countryId)})),...Object.keys(d.presentation.labelSettings).map(id=>({kind:'label-settings',id:map(id.replace(/^territorial:/,'')),target:map(id.replace(/^territorial:/,''))}))];}
function webPresentation(stage){const p=stage.document.presentation;return {hiddenItems:Object.keys(p.itemVisibility).sort().flatMap(group=>Object.keys(p.itemVisibility[group]).sort().filter(id=>p.itemVisibility[group][id]===false).map(id=>({group,id}))),objectStyles:Object.keys(p.layerPresentation.objectStyles).sort().map(key=>({key,...p.layerPresentation.objectStyles[key]}))};}
export function normalizeSplitWebCase(row){
 assert.ok(row.stages?.before,'missing before stage');validateRawCandidateContinuity(row);const entities=s=>list(s.document?.document?.entities,'document entities'),ids=mapObjectIds(row,entities),stages={};
 for(const [name,s]of Object.entries(row.stages)){
  assert.ok(s.document?.history&&['undo','redo'].every(key=>Number.isInteger(s.document.history[key])&&s.document.history[key]>=0),'missing exact web history observation');
  const features=mappedFeatures(entities(s),ids.stable),review=name==='review'?webReviewPreview(s):{present:null,features:[]},preview=review.features;
  const normalized={outcome:s.outcome,selection:selection(s.selection),features,references:webRefs(s,ids.stable),presentation:webPresentation(s),genericMetadata:list(s.document.genericFeatures,'generic metadata').map(f=>({id:f.id,sourceDetails:f.properties.source?.details??{}})),history:{canUndo:s.document.history.undo>0,canRedo:s.document.history.redo>0},previewPresent:review.present,previewFeatures:mappedPreviewFeatures(preview,ids.preview(preview))};
  if(name==='confirm'&&s.outcome){const intent=s.selectionEffects?.filter(e=>e.name==='selection.applyIntent').at(-1)?.args?.[0];assert.ok(intent&&typeof intent.id==='string','missing committed selection intent');normalized.committedSelection={domain:intent.domain,id:ids.stable(intent.id)};}else normalized.committedSelection=null;
  Object.assign(normalized,identities(normalized,ids.initial));stages[name]=normalized;
 }
 if(stages.redo)assert.deepEqual(stages.redo.createdIds,stages.confirm.createdIds,'redo must retain the actual created identity');
 return {case:row.case,stages};
}
export function normalizeSplitNativeCase(row){
 assert.ok(row.stages?.before,'missing before stage');for(const event of list(row.events,'native events'))for(const key of ['selectionPending','previewPending','applying','active'])assert.equal(typeof event[key],'boolean',`missing native event ${key} observation`);validateRawCandidateContinuity(row,true);unique(list(row.stageOrder,'stage order'),'stage');const entities=s=>list(s.features,'features'),ids=mapObjectIds(row,entities),stages={};
 for(const name of list(row.stageOrder,'stage order')){
  const s=row.stages[name];assert.ok(s,'missing ordered stage');
  assert.ok(s.state&&typeof s.state==='object'&&!Array.isArray(s.state)&&s.raw&&typeof s.raw==='object'&&!Array.isArray(s.raw)&&typeof s.unchangedFromBefore==='boolean'&&typeof s.documentSha256==='string','missing actual controller observation');assert.equal(typeof s.state.active,'boolean','missing native active boolean observation');assert.match(s.documentSha256,/^[a-f0-9]{64}$/,'missing exact native document hash observation');
  const review=name==='review'?nativeReviewPreview(s):{present:null,features:[]},preview=review.features,features=mappedFeatures(entities(s),ids.stable);
  const normalized={outcome:s.outcome,selection:s.state.active?selection(s.state,s.raw):null,features,references:list(s.references,'references').map(r=>({...r,target:ids.stable(r.target),...(r.kind==='label-settings'?{id:ids.stable(r.id)}:{})})),presentation:s.presentation,genericMetadata:s.genericMetadata,history:s.history,previewPresent:review.present,previewFeatures:mappedPreviewFeatures(preview,ids.preview(preview))};
  if(name==='confirm'&&s.outcome){assert.ok(s.primaryObject&&typeof s.primaryObject.id==='string','missing native committed primary selection');normalized.committedSelection={domain:s.primaryObject.domain,id:ids.stable(s.primaryObject.id)};}else normalized.committedSelection=null;
  Object.assign(normalized,identities(normalized,ids.initial));stages[name]=normalized;
 }
 for(const name of ['candidates','selected','archive','review','cancel']){assert.ok(row.stages[name],`missing ${name} stage`);assert.equal(row.stages[name].documentSha256,row.stages.before.documentSha256,`${name} must not mutate document`);assert.equal(row.stages[name].unchangedFromBefore,true,`${name} must preserve document bytes`);}
 if(row.stages.confirm?.outcome===false)assert.equal(row.stages.confirm.documentSha256,row.stages.before.documentSha256,'rejected commit must roll back');
 if(row.stages.undo)assert.equal(row.stages.undo.documentSha256,row.stages.before.documentSha256,'undo must restore exact document bytes');
 if(row.stages.redo)assert.equal(row.stages.redo.documentSha256,row.stages.confirm.documentSha256,'redo must restore exact committed bytes');
 return {case:row.case,stages};
}
function validate(row){
 assert.ok(row&&typeof row.case==='string'&&row.stages,'missing case observations');
 for(const name of ['before','candidates','selected','archive','review','cancel'])assert.ok(row.stages[name],`missing ${name} stage`);
 for(const [name,s]of Object.entries(row.stages)){
  assert.ok(Object.hasOwn(s,'selection')&&Object.hasOwn(s,'outcome'),'missing observation fields');assert.ok(s.outcome===null||typeof s.outcome==='boolean','invalid outcome observation');
  for(const key of ['features','previewFeatures']){unique(list(s[key],key).map(f=>f.id),key);for(const f of s[key]){geometry(f.geometry);assert.equal(typeof f.parentId,'string','missing parent identity');}}
  const previewParentIds=new Set(['',...s.features.map(f=>f.id),...s.previewFeatures.map(f=>f.id)]);for(const feature of s.previewFeatures)assert.ok(previewParentIds.has(feature.parentId),'unknown preview parent identity');
  assert.ok(name==='review'?typeof s.previewPresent==='boolean':s.previewPresent===null,'missing explicit preview presence observation');if(s.previewPresent===false)assert.deepEqual(s.previewFeatures,[],'absent preview cannot expose features');
  list(s.references,'references');assert.ok(s.presentation,'missing presentation observation');list(s.genericMetadata,'generic metadata');assert.ok(s.history&&typeof s.history.canUndo==='boolean'&&typeof s.history.canRedo==='boolean','missing history observation');
  for(const key of ['createdIds','retainedIds','deletedIds'])unique(list(s[key],key),key);
  if(s.selection){const state=s.selection;assert.ok(typeof state.previewReady==='boolean'&&typeof state.canAddPart==='boolean','missing selection readiness');unique(list(state.candidates,'candidates').map(c=>c.id),'candidate');for(const c of state.candidates)geometry(c.geometry);for(const p of list(state.parts,'parts'))geometry(p.geometry);for(const id of list(state.selectedCandidateIds,'selected candidates'))assert.ok(state.candidates.some(c=>c.id===id),'selected candidate identity missing');for(const key of ['combinedGeometry','remainingGeometry'])if(state[key]!==null)geometry(state[key]);}
 }
}
export async function compareSplitObservations(web,native){
 validate(web);validate(native);assert.equal(native.case,web.case,'case identity mismatch');assert.deepEqual(Object.keys(native.stages),Object.keys(web.stages),'missing or reordered stage observations');
 const differences=[];
 const different=(field,w,n)=>{if(!isDeepStrictEqual(w,n))differences.push({field,web:w,native:n});};
 const shapes=(field,w,n)=>{const a=new Map(w.map(f=>[f.id,f])),b=new Map(n.map(f=>[f.id,f]));for(const id of [...new Set([...a.keys(),...b.keys()])].sort()){if(!a.has(id)||!b.has(id)){differences.push({field:field+'.object',id,web:a.has(id),native:b.has(id)});continue;}if(!exactGeometryEqual(a.get(id).geometry,b.get(id).geometry))differences.push({field:field+'.geometry',id,web:a.get(id).geometry,native:b.get(id).geometry});}};
 const shape=(field,w,n)=>{if(w===null||n===null){different(field,w,n);return;}shapes(field,[{id:'geometry',geometry:w}],[{id:'geometry',geometry:n}]);};
 for(const [name,w]of Object.entries(web.stages)){
  const n=native.stages[name];for(const field of ['outcome','previewPresent','committedSelection','references','presentation','genericMetadata','history','createdIds','retainedIds','deletedIds'])different(`${name}.${field}`,w[field],n[field]);
  for(const field of ['features','previewFeatures']){different(`${name}.${field}.metadata`,w[field].map(({geometry,...rest})=>rest),n[field].map(({geometry,...rest})=>rest));shapes(`${name}.${field}`,w[field],n[field]);}
  if(!w.selection||!n.selection){different(`${name}.selection`,w.selection,n.selection);continue;}
  for(const field of ['stage','activeMethod','activePhase','sourceCountryIds','selectedCandidateIds','previewReady','canAddPart'])different(`${name}.selection.${field}`,w.selection[field],n.selection[field]);
  for(const field of ['candidates','parts']){const a=w.selection[field],b=n.selection[field];different(`${name}.${field}.metadata`,a.map(({geometry,...rest})=>rest),b.map(({geometry,...rest})=>rest));shapes(`${name}.${field}`,a,b);}
  for(const field of ['combinedGeometry','remainingGeometry'])shape(`${name}.${field}`,w.selection[field],n.selection[field]);
 }
 return differences;
}

// This does not erase a difference. It records a narrowly inactive upstream
// cache discrepancy, and admits the observable gate only after an independent
// actual reactivation replay has rebuilt the values before they are consumed.
export async function classifyInactiveSplitCache(web,native,differences,reactivation){
 if(!['root-empty-selection','root-empty-selection-reactivated'].includes(web.case)||!differences.length||!reactivation?.web||!reactivation?.native)return null;
 const names=web.case==='root-empty-selection'?['selected','archive','review']:['emptied'];
 if(differences.length!==names.length||differences.some(d=>!names.some(name=>d.field===name+'.remainingGeometry.geometry')))return null;
 const rw=reactivation.web,rn=reactivation.native;if(rw.case!=='root-empty-selection-reactivated'||rn.case!==rw.case)return null;
 // Recompute rather than trusting a caller-provided difference summary.
 const rebuiltDifferences=await compareSplitObservations(rw,rn);
 if(rebuiltDifferences.some(d=>d.field!=='emptied.remainingGeometry.geometry'))return null;
 for(const row of [rw,rn]){
  if(row.stages.emptied?.outcome!==false||row.stages.reactivated?.outcome!==true||row.stages.archive?.outcome!==true||row.stages.review?.outcome!==true||row.stages.confirm?.outcome!==true||row.stages.undo?.outcome!==true||row.stages.redo?.outcome!==true)return null;
  const state=row.stages.reactivated.selection;if(!state?.previewReady||!state.canAddPart||state.selectedCandidateIds.length!==1||state.combinedGeometry===null)return null;
 }
 const equal=exactGeometryEqual;
 const source=native.stages.before.features.find(f=>f.id==='source')?.geometry;if(!source)return null;if(!equal(source,rw.stages.before.features.find(f=>f.id==='source')?.geometry)||!equal(web.stages.candidates.selection.remainingGeometry,rw.stages.candidates.selection.remainingGeometry)||!isDeepStrictEqual(web.stages.candidates.selection.selectedCandidateIds,rw.stages.candidates.selection.selectedCandidateIds))return null;const checkpoints=[];
 for(const name of names){
  const w=web.stages[name]?.selection,n=native.stages[name]?.selection;
  if(!w||!n||[w,n].some(s=>s.selectedCandidateIds.length||s.combinedGeometry!==null||s.previewReady||s.canAddPart))return null;
  if(!equal(n.remainingGeometry,source)||!equal(w.remainingGeometry,web.stages.candidates.selection.remainingGeometry))return null;
  checkpoints.push({stage:name,web:{selectedCandidateIds:w.selectedCandidateIds,combinedGeometry:w.combinedGeometry,previewReady:w.previewReady,canAddPart:w.canAddPart,remainingGeometry:w.remainingGeometry},native:{selectedCandidateIds:n.selectedCandidateIds,combinedGeometry:n.combinedGeometry,previewReady:n.previewReady,canAddPart:n.canAddPart,remainingGeometry:n.remainingGeometry}});
 }
 if(web.case==='root-empty-selection'&&[web,native].some(r=>r.stages.archive.outcome!==false||r.stages.review.outcome!==false))return null;
 return {classification:'bounded-inactive-cache-difference',rawParity:false,checkpoints,reactivation:{case:rw.case,emptiedAdvanceAccepted:false,reactivatedSelectionAccepted:true,rebuiltBeforeConsumption:true,archive:true,confirm:true,undo:true,redo:true}};
}
