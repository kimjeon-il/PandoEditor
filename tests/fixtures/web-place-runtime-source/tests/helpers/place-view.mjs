import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import { createMapVisualFrame } from '../../assets/js/modules/map-visual-frame.js';
const scope={};
vm.runInNewContext(readFileSync(new URL('../../assets/js/vendor/d3.min.js',import.meta.url),'utf8'),scope);

/** Test view uses the same D3 camera and canonical projection frame as the app. */
export function placeView(raw={}) {
  const view={projection:'flat',threshold:10,width:400,height:400,scale:1000,flatCenter:[0,0],rotation:[0,0,0],...raw};
  const translate=raw.translate || [view.width/2,view.height/2];
  const projection=(view.projection==='globe' ? scope.d3.geo.orthographic().rotate(view.rotation) : scope.d3.geo.equirectangular().center(view.flatCenter)).translate(translate).scale(view.scale);
  const frame=createMapVisualFrame({viewState:{projection:view.projection,size:{width:view.width,height:view.height},translate,scale:view.scale,rotation:view.rotation,flatCenter:view.flatCenter,safeInset:raw.safeInset},projectCoordinate:projection});
  return {...view,projectionFrame:{mode:frame.mode,cssTranslate:frame.cssTranslate,cssScale:frame.cssScale,cssViewport:frame.cssViewport,safeInset:frame.safeInset,flatCenter:frame.flatCenter,worldOffsets:frame.worldOffsets,rows:{rowX:frame.rowX,rowY:frame.rowY,rowZ:frame.rowZ}}};
}
