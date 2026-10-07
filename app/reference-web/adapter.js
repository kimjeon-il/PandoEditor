import {buildReferenceImageCalibrationWarp,buildReferenceImageProjectiveWarpFromQuad,buildReferenceImageMesh} from './reference-image-georef.js';
import {alignReferenceImageAnchor,applyReferenceImageFreeTransformDrag} from './reference-image-transform.js';
export function calibration(record) {
 const calibrated=buildReferenceImageCalibrationWarp({...record,mode:record.warpMode});
 let warp=calibrated;
 if(!warp.ok&&record.mapQuad)warp=buildReferenceImageProjectiveWarpFromQuad(record.mapQuad);
 const mesh=warp.ok?buildReferenceImageMesh(warp):null;
 return {ok:warp.ok,calibrationOk:calibrated.ok,calibrationReason:calibrated.reason||'',mode:warp.mode,reason:warp.reason||'',diagnostics:calibrated.diagnostics||{},mesh:mesh?{...mesh,vertices:mesh.vertices.map(vertex=>({...vertex,uv:[record.flipX?1-vertex.uv[0]:vertex.uv[0],record.flipY?1-vertex.uv[1]:vertex.uv[1]]}))}:null};
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
