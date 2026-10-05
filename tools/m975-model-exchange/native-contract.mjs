// Observation validation only. No projection/geometry/edit algorithm is replaced.
import assert from 'node:assert/strict';
const object=(value,label)=>assert.ok(value&&typeof value==='object'&&!Array.isArray(value),label);
const finitePair=(value,label)=>{assert.ok(Array.isArray(value)&&value.length===2,label);for(const number of value)assert.ok(typeof number==='number'&&Number.isFinite(number),label+' finite double');};
const publicViewFields=view=>({projection:view.kind,scale:view.scale,translateX:view.translate[0],translateY:view.translate[1],rotationLongitude:view.rotate[0],rotationLatitude:view.rotate[1],rotationRoll:view.rotate[2],centerLongitude:view.center[0],centerLatitude:view.center[1],viewportWidth:view.size.width,viewportHeight:view.size.height});
function verifyView(view,label){object(view,label);assert.ok(['flat','globe'].includes(view.projection),label+' projection');for(const key of ['scale','translateX','translateY','rotationLongitude','rotationLatitude','rotationRoll','centerLongitude','centerLatitude','viewportWidth','viewportHeight','devicePixelRatio','revision','fitScale','mapScale','originX','originY','panX','panY','flatZoom','globeZoom','zoom'])assert.ok(typeof view[key]==='number'&&Number.isFinite(view[key]),label+' '+key);for(const key of ['scale','viewportWidth','viewportHeight','devicePixelRatio','fitScale','mapScale','flatZoom','globeZoom','zoom'])assert.ok(view[key]>0,label+' positive '+key);assert.ok(Number.isInteger(view.revision)&&view.revision>=0,label+' revision');}
function verifyProjection(value,label){object(value,label);assert.deepEqual(Object.keys(value).sort(),['cosLatitude','maxLatitude','minX']);for(const key of Object.keys(value))assert.ok(typeof value[key]==='number'&&Number.isFinite(value[key]),label+' '+key);assert.ok(value.cosLatitude>0&&value.cosLatitude<=1,label+' cosine');}
export function advanceUlp(value,steps){
 assert.ok(Number.isFinite(value));assert.ok(Number.isInteger(steps)&&Math.abs(steps)<=4);const view=new DataView(new ArrayBuffer(8)),direction=Math.sign(steps);let current=value;
 for(let n=0;n<Math.abs(steps);n++){if(current===0){current=direction*Number.MIN_VALUE;continue;}view.setFloat64(0,current,false);const increment=(current>0)===(direction>0)?1n:-1n;view.setBigUint64(0,view.getBigUint64(0,false)+increment,false);current=view.getFloat64(0,false);assert.ok(Number.isFinite(current),'chosen finite screen double');}
 return current;
}
export function verifyNativeInputObservations(result,request){
 const expected=request.actions.flatMap(action=>action.op==='draft'?action.points.map(intended=>({op:action.op,intended})):action.op==='move-node'?[{op:action.op,intended:action.coordinate}]:[]);
 assert.ok(Array.isArray(result.inputObservations),'required actual coordinate observations');assert.equal(result.inputObservations.length,expected.length,'complete ordered action-derived coordinate inventory');
 if(!expected.length)return;
 const before=result.stages.before;object(before,'initial coordinate stage');verifyView(before.mapViewState,'initial coordinate view');verifyProjection(before.controllerProjection,'initial coordinate projection');
 for(const [index,row]of result.inputObservations.entries()){
  assert.equal(row.op,expected[index].op,'coordinate public action order');finitePair(row.intended,'intended coordinate');finitePair(row.inverse,'inverse coordinate');finitePair(row.naive,'naive projected screen coordinate');finitePair(row.chosen,'chosen screen coordinate');assert.deepEqual(row.intended,expected[index].intended,'exact requested geographic doubles');assert.deepEqual(row.inverse,row.intended,'exact native inverse equals requested geographic doubles');assert.equal(row.exact,true,'actual exact input preimage');assert.equal(row.maximumUlpRadius,4,'fixed maximum four-ULP search, no expanded tolerance');
  for(const [axis,key]of ['xUlpSteps','yUlpSteps'].entries()){assert.ok(Number.isInteger(row[key])&&Math.abs(row[key])<=4,'bounded integral '+key);assert.equal(row.chosen[axis],advanceUlp(row.naive[axis],row[key]),'chosen screen double exactly accounted by '+key);}
  verifyView(row.view,'coordinate actual public view');verifyProjection(row.controllerProjection,'coordinate actual public projection');
  // These bounded recipes enter both drafts before the sole canonical confirm.
  // Automatic camera metrics after confirm/Undo/Redo are observed separately.
  assert.deepEqual(row.view,before.mapViewState,'coordinate view before canonical commit');assert.deepEqual(row.controllerProjection,before.controllerProjection,'coordinate actual projection matches initial canonical scene');
 }
}
export function verifyNativeEditStages(result,request){
 if(request.mode!=='edit')return;const initial=result.stages.before;verifyView(initial?.mapViewState,'initial public view');const expectedView=publicViewFields(request.view);for(const [key,value]of Object.entries(expectedView))assert.equal(initial.mapViewState[key],value,'requested initial public view '+key);
 let tool=null,selectionDomain='territorial',activeRole=null;
 for(const [index,expected]of request.actions.entries()){
  const action=result.actions[index];assert.equal(typeof action.dispatchAccepted,'boolean','observed public dispatch outcome');assert.ok(Array.isArray(action.errors)&&action.errors.every(e=>typeof e==='string'),'current public action error slice');if(action.accepted)assert.equal(action.dispatchAccepted,true,'accepted action was dispatched');
  if(expected.op==='select')selectionDomain=expected.domain||'territorial';
  if(expected.op==='begin'&&action.accepted){tool=expected.tool;activeRole=tool==='delete'?(selectionDomain==='territorial'?'structure':'content'):'geometry';}
  if(expected.op==='cancel'||(expected.op==='confirm'&&action.accepted)){tool=null;activeRole=null;}
  if(!expected.stage)continue;const row=result.stages[expected.stage];object(row.state,'observed geometry state '+expected.stage);object(row.content,'observed content state '+expected.stage);object(row.structure,'observed structure state '+expected.stage);assert.equal(row.accepted,action.accepted,'stage acceptance is the corresponding current action');assert.deepEqual(row.errors,action.errors,'stage errors are the current action slice before save refusal');
  assert.equal(row.state.active,activeRole==='geometry','actual geometry activity '+expected.stage);assert.equal(row.content.active,activeRole==='content','actual content activity '+expected.stage);assert.equal(row.structure.open,activeRole==='structure','actual structure activity '+expected.stage);
  verifyView(row.mapViewState,'actual stage public view '+expected.stage);verifyProjection(row.controllerProjection,'actual stage public projection '+expected.stage);if(['before','preview','cancel','rejected'].includes(expected.stage)){assert.deepEqual(row.mapViewState,initial.mapViewState,'precommit observed view matches initial view');assert.deepEqual(row.controllerProjection,initial.controllerProjection,'precommit observed projection matches initial scene');}
  if(row.state.active){assert.equal(row.state.tool,tool==='shared-boundary'?'boundary':tool,'actual requested geometry tool');assert.equal(row.state.calculating,false,'observed geometry calculation settled');if(tool!=='shared-boundary')assert.equal(row.state.applying,false,'observed geometry application settled');assert.equal(typeof row.state.previewReady,'boolean','observed preview readiness');}
  if(expected.stage==='preview')assert.equal(row.state.previewReady,true,'actual ready uncommitted preview');
  if(expected.op==='confirm'&&!action.accepted){assert.equal(expected.expectAccepted,false);assert.equal(request.expectedCommitError,'DANGLING_REF','bounded fixture-specific expected commit failure');assert.ok(action.errors.length>0&&action.errors.every(error=>/\bVALIDATION_FAILED\b/.test(error)&&/\bDANGLING_REF\b/.test(error)),'actual current dangling-reference validation refusal');assert.ok(typeof row.state.error==='string'&&/\bDANGLING_REF\b/.test(row.state.error),'observed current tool rejection reason');}
  assert.equal(row.nativeFile.canonicalUnchanged,true,'stage persistence canonical unchanged proof');assert.equal(row.nativeFile.historyUnchanged,true,'stage persistence history unchanged proof');
 }
}
