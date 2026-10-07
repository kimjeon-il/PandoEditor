import {buildReferenceImageCalibrationWarp,buildReferenceImageProjectiveWarpFromQuad,buildReferenceImageMesh,referenceImageWarpQuad} from './reference-image-georef.js';
import {alignReferenceImageAnchor,applyReferenceImageFreeTransformDrag} from './reference-image-transform.js';
export function calibration(record) {
 const calibrated=buildReferenceImageCalibrationWarp({...record,mode:record.warpMode});
 let warp=calibrated;
 if(!warp.ok&&record.mapQuad)warp=buildReferenceImageProjectiveWarpFromQuad(record.mapQuad);
 const mesh=warp.ok?buildReferenceImageMesh(warp):null;
 return {ok:warp.ok,calibrationOk:calibrated.ok,calibrationReason:calibrated.reason||'',mode:warp.mode,reason:warp.reason||'',diagnostics:calibrated.diagnostics||{},mesh:mesh?{...mesh,vertices:mesh.vertices.map(vertex=>({...vertex,uv:calibrated.ok?vertex.uv:[record.flipX?1-vertex.uv[0]:vertex.uv[0],record.flipY?1-vertex.uv[1]:vertex.uv[1]]}))}:null};
}
export function constraintRemoval(input) {
 const before=buildReferenceImageCalibrationWarp({...input.before,mode:input.before.warpMode});
 const record={...input.after};
 const after=buildReferenceImageCalibrationWarp({...record,mode:record.warpMode});
 const fallback=referenceImageWarpQuad(before);
 if(!after.ok&&fallback){record.mapQuad=fallback;if(record.anchor)alignReferenceImageAnchor(record);}
 return {ok:true,mapQuad:record.mapQuad};
}
export function cornerQuad(input) {
 const record={...input.record,mapQuad:input.quad};
 const host={unproject:()=>input.quad[0],project:coordinate=>{
   const index=record.mapQuad.findIndex(p=>p[0]===coordinate[0]&&p[1]===coordinate[1]);
   return index>=0?input.screenQuad[index]:null;
 }};
 let ok=applyReferenceImageFreeTransformDrag(record,{index:0,startMapQuad:input.quad},input.screenQuad[0],host);
 const warp=buildReferenceImageCalibrationWarp({...record,cornerPinEnabled:true,mode:record.warpMode});
 if((record.controlPoints||[]).length||record.anchor)ok=ok&&warp.ok&&warp.diagnostics.hardMaxMeters<=.01;
 return {ok,mapQuad:record.mapQuad};
}
export function anchor(input) {
 const record={...input.record,anchor:input.anchor};
 const warp=buildReferenceImageCalibrationWarp({...record,mode:record.warpMode});
 const ok=warp.ok?warp.diagnostics.hardMaxMeters<=.01:
   warp.reason==='singular-control-points'&&warp.pointCount>=warp.minimumPoints?false:alignReferenceImageAnchor(record);
 return {ok,mapQuad:record.mapQuad,anchor:record.anchor};
}

import {buildReferenceImageLiveWireField,buildLiveWireTree,traceLiveWirePath,simplifyLiveWireSegments,analysisPointFromUv,sourcePixelsFromAnalysis} from './reference-image-live-wire.js';
import {buildReferenceImageSourceMapping} from './reference-image-source-mapping.js';
export function trace(input) {
 const field=buildReferenceImageLiveWireField(input.image,{sourceWidth:input.sourceWidth,sourceHeight:input.sourceHeight});
 const warp=buildReferenceImageSourceMapping(input.record);
 if(!warp.ok)return {ok:false,reason:warp.reason};
 const anchors=input.anchors.map(uv=>analysisPointFromUv(field,uv)),segments=[];
 for(let i=1;i<anchors.length;i++) {
  const tree=buildLiveWireTree(field,anchors[i-1],{target:anchors[i],corridorWeight:.08});
  const result=traceLiveWirePath(tree,anchors[i]);
  if(!result.ok)return {ok:false,reason:result.reason};
  segments.push(result.points);
 }
 const points=sourcePixelsFromAnalysis(field,simplifyLiveWireSegments(segments,{tolerance:1.5}));
 const uv=points.map(p=>[p[0]/Math.max(1,input.sourceWidth-1),p[1]/Math.max(1,input.sourceHeight-1)]);
 const coordinates=uv.map(p=>warp.project(p));
 return {ok:true,reason:'',coordinates,uv};
}

import {buildReferenceImageGradientField,refineReferenceImageLine,referenceImagePixelsToCoordinates} from './reference-image-line-refiner.js';
export function refine(input) {
 const field=buildReferenceImageGradientField(input.image,{sourceWidth:input.sourceWidth,sourceHeight:input.sourceHeight});
 const warp=buildReferenceImageSourceMapping(input.record);
 if(!warp.ok)return {ok:false,reason:warp.reason};
 const roughPoints=input.anchors.map(p=>[p[0]*(input.sourceWidth-1),p[1]*(input.sourceHeight-1)]);
 const result=refineReferenceImageLine({field,roughPoints,corridorRadius:12,simplifyTolerance:1.5});
 return result.ok?{ok:true,coordinates:referenceImagePixelsToCoordinates(result.points,{sourceWidth:input.sourceWidth,sourceHeight:input.sourceHeight,warp}),uv:result.points.map(p=>[p[0]/Math.max(1,input.sourceWidth-1),p[1]/Math.max(1,input.sourceHeight-1)])}:{ok:false,reason:result.reason};
}

import {defaultReferenceImageMapQuad,setReferenceImagePlacementRotation,referenceImagePlacementRotation} from './reference-image-transform.js';
function nativeHost(projection) {
 return {project:c=>{const p=projection.referenceScreenAtCoordinate(c[0],c[1]);return p.visible?[p.x,p.y]:null},unproject:p=>JSON.parse(JSON.stringify(projection.referenceCoordinateAtScreen(p[0],p[1]))),getProjectionKind:()=>projection.projectionMode};
}
export function placementRotate(input,projection) {
 const record={...input.record};const ok=setReferenceImagePlacementRotation(record,nativeHost(projection),input.angle);
 return {ok,mapQuad:record.mapQuad,rotation:referenceImagePlacementRotation(record,nativeHost(projection))};
}
export function placementDefault(input,projection) {
 const mapQuad=defaultReferenceImageMapQuad({naturalWidth:input.width,naturalHeight:input.height},{getBoundingClientRect:()=>input.viewport},nativeHost(projection));
 return {ok:!!mapQuad,mapQuad};
}
export function placementAngle(input,projection) { return {angle:referenceImagePlacementRotation(input.record,nativeHost(projection))}; }
