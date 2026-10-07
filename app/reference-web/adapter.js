import {buildReferenceImageCalibrationWarp,buildReferenceImageMesh} from './reference-image-georef.js';
export function calibration(record) {
 const warp=buildReferenceImageCalibrationWarp({...record,mode:record.warpMode});
 return {ok:warp.ok,mode:warp.mode,reason:warp.reason||'',diagnostics:warp.diagnostics||{},mesh:warp.ok?buildReferenceImageMesh(warp):null};
}
