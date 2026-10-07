import {readFileSync,writeFileSync} from 'node:fs';
import assert from 'node:assert/strict';
const load=async name=>import('data:text/javascript;base64,'+readFileSync(new URL('../../app/reference-web/'+name,import.meta.url)).toString('base64'));
const web=await load('reference-image-line-refiner.js'),georef=await load('reference-image-georef.js');
const cases=[{id:'offset-line',edge:'vertical',uv:[[.45,.15],[.45,.85]]},{id:'turn',edge:'turn',uv:[[.45,.15],[.45,.8],[.85,.8]]},{id:'weak',edge:'weak',uv:[[.5,.15],[.5,.85]]}];
const fixtures=cases.map(input=>{
 const data=[];for(let y=0;y<64;y++)for(let x=0;x<64;x++){const v=input.edge==='weak'?128:(x>=32&&(input.edge!=='turn'||y<54))?255:0;data.push(v,v,v,255)}
 const field=web.buildReferenceImageGradientField({width:64,height:64,data});
 const result=web.refineReferenceImageLine({field,roughPoints:input.uv.map(p=>[p[0]*63,p[1]*63]),corridorRadius:12,simplifyTolerance:1.5});
 const warp=georef.buildReferenceImageProjectiveWarpFromQuad([[10,40],[20,40],[20,30],[10,30]]);
 return {input,expected:result.ok?{ok:true,coordinates:web.referenceImagePixelsToCoordinates(result.points,{sourceWidth:64,sourceHeight:64,warp})}:{ok:false,reason:result.reason}};
});
assert.equal(fixtures[0].expected.ok,true);assert.equal(fixtures[2].expected.reason,'insufficient-edge-strength');
const file=new URL('../../tests/fixtures/reference-refine.json',import.meta.url);
if(process.argv.includes('--record'))writeFileSync(file,JSON.stringify(fixtures,null,2)+'\n');else assert.deepEqual(JSON.parse(readFileSync(file)),fixtures);
console.log(JSON.stringify({cases:fixtures.length,mismatches:0}));
