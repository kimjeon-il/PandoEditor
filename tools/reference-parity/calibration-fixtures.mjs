import {readFileSync,writeFileSync} from 'node:fs';
import assert from 'node:assert/strict';
const url=new URL('../../app/reference-web/reference-image-georef.js',import.meta.url);
const web=await import('data:text/javascript;base64,'+readFileSync(url).toString('base64'));
const point=(id,u,v,x,y)=>({id,image:[u,v],coordinate:[x,y]});
const cases=[
 {id:'similarity',warpMode:'similarity',controlPoints:[point('a',0,0,10,40),point('b',1,0,20,40)]},
 {id:'affine',warpMode:'affine',controlPoints:[point('a',0,0,10,40),point('b',1,0,20,40),point('c',0,1,10,30)]},
 {id:'projective',warpMode:'projective',controlPoints:[point('a',0,0,10,40),point('b',1,0,20,41),point('c',1,1,19,30),point('d',0,1,11,31)]},
 {id:'tps',warpMode:'tps',controlPoints:[point('a',0,0,10,40),point('b',1,0,20,40),point('c',0,1,10,30),point('d',1,1,21,31)]},
 {id:'dateline',warpMode:'affine',controlPoints:[point('a',0,0,179,40),point('b',1,0,-179,40),point('c',0,1,179,30)]},
 {id:'insufficient',warpMode:'affine',controlPoints:[point('a',0,0,10,40)]},
 {id:'collinear',warpMode:'affine',controlPoints:[point('a',0,0,10,40),point('b',.5,0,15,40),point('c',1,0,20,40)]}
];
const fixtures=cases.map(input=>{const warp=web.buildReferenceImageCalibrationWarp({...input,mode:input.warpMode});return {input,expected:{ok:warp.ok,reason:warp.reason||'',mesh:warp.ok?web.buildReferenceImageMesh(warp):null}}});
assert.equal(fixtures[1].expected.mesh.vertices[0].coordinate[0],10);
assert.equal(fixtures[4].expected.mesh.vertices[12].coordinate[0],180);
const destination=new URL('../../tests/fixtures/reference-calibration.json',import.meta.url);
if(process.argv.includes('--record'))writeFileSync(destination,JSON.stringify(fixtures,null,2)+'\n');
else assert.deepEqual(JSON.parse(readFileSync(destination)),fixtures);
console.log(JSON.stringify({cases:fixtures.length,mismatches:0}));
