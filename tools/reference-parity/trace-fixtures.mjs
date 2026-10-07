import {readFileSync,writeFileSync} from 'node:fs';
import assert from 'node:assert/strict';
const load=async name=>import('data:text/javascript;base64,'+readFileSync(new URL('../../app/reference-web/'+name,import.meta.url)).toString('base64'));
const web=await load('reference-image-live-wire.js');
const georef=await load('reference-image-georef.js');
const cases=[{id:'vertical',anchors:[[.5,.15],[.5,.85]],edge:'vertical'}, {id:'turn',anchors:[[.5,.15],[.5,.85],[.8,.85]],edge:'turn'}, {id:'weak',anchors:[[.5,.15],[.5,.85]],edge:'weak'}];
const fixtures=cases.map(input=>{
 const data=[];for(let y=0;y<64;y++)for(let x=0;x<64;x++){const v=input.edge==='weak'?128:(x>=32&&(input.edge!=='turn'||y<54))?255:0;data.push(v,v,v,255)}
 const field=web.buildReferenceImageLiveWireField({width:64,height:64,data});const segments=[];
 for(let i=1;i<input.anchors.length;i++){
  const start=web.analysisPointFromUv(field,input.anchors[i-1]),target=web.analysisPointFromUv(field,input.anchors[i]);
  const result=web.traceLiveWirePath(web.buildLiveWireTree(field,start,{target,corridorWeight:.08}),target);
  if(!result.ok)return {input,expected:{ok:false,reason:result.reason}};
  segments.push(result.points);
 }
 const pixels=web.sourcePixelsFromAnalysis(field,web.simplifyLiveWireSegments(segments,{tolerance:1.5}));
 const warp=georef.buildReferenceImageProjectiveWarpFromQuad([[10,40],[20,40],[20,30],[10,30]]);
 return {input,expected:{ok:true,coordinates:pixels.map(p=>warp.project([p[0]/63,p[1]/63]))}};
});
assert.equal(fixtures[0].expected.ok,true);assert.equal(fixtures[2].expected.reason,'insufficient-edge-strength');
const file=new URL('../../tests/fixtures/reference-trace.json',import.meta.url);
if(process.argv.includes('--record'))writeFileSync(file,JSON.stringify(fixtures,null,2)+'\n');else assert.deepEqual(JSON.parse(readFileSync(file)),fixtures);
console.log(JSON.stringify({cases:fixtures.length,mismatches:0}));
