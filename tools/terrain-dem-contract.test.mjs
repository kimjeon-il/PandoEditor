import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {resolve,dirname} from 'node:path';

const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const read=p=>readFileSync(resolve(root,p),'utf8');
const pinnedWeb='5649c307da24d0965d63bc8c00206aed9b9d3438';
const fixtureRoot=resolve(root,'tests/fixtures/terrain-dem-source');
const sourceManifest=JSON.parse(readFileSync(resolve(fixtureRoot,'manifest.json'),'utf8'));
const sourceBytes=readFileSync(resolve(fixtureRoot,'terrain-dem-shaders.js'));
function verifySourceBytes(bytes) {
    assert.equal(sourceManifest.schema,'pandoeditor-fixed-web-source-v1');
    assert.equal(sourceManifest.sourceRepository,'https://github.com/kimjeon-il/world-map.git');
    assert.equal(sourceManifest.file,'terrain-dem-shaders.js');
    assert.equal(sourceManifest.sourceCommit,pinnedWeb);
    assert.equal(sourceManifest.sourcePath,'assets/js/modules/terrain-dem-shaders.js');
    assert.equal(sourceManifest.bytes,4275);
    assert.equal(sourceManifest.gitBlob,'aa160eb0b32b19c379f3f75a99bf2fcf70ceac13');
    assert.equal(sourceManifest.sha256,'00cc3c301c9a31b5e05bb091c9db0568b6ef69292310f7190ad1b724fefb4c85');
    assert.equal(bytes.length,sourceManifest.bytes);
    assert.equal(createHash('sha256').update(bytes).digest('hex'),sourceManifest.sha256);
    assert.equal(createHash('sha1').update(`blob ${bytes.length}\0`).update(bytes).digest('hex'),sourceManifest.gitBlob);
}
verifySourceBytes(sourceBytes);
// An explicitly requested external comparison is mandatory, never skipped.
if(process.env.PANDOEDITOR_WEB_SOURCE_ROOT) {
    const external=execFileSync('git',['-C',process.env.PANDOEDITOR_WEB_SOURCE_ROOT,'show',`${pinnedWeb}:${sourceManifest.sourcePath}`]);
    verifySourceBytes(external);assert.deepEqual(external,sourceBytes);
}
const web=sourceBytes.toString('utf8');
const common=web.split('const common = (sample) => `\n')[1].split('\n`;')[0].replaceAll('${sample}','texture');
const normalized=s=>s.replace(/\/\/[^\n]*/g,'').replace(/\s+/g,'');
function requireLiteralDemMath(source) {
    const actual=source.slice(source.indexOf('  float elevation('),source.indexOf('\nvoid main()'));
    assert.equal(normalized(actual),normalized(common),'all fixed Web DEM arithmetic must remain literal');
}

test('vendored source has exact immutable Git/blob/hash/byte provenance',()=>{
    verifySourceBytes(sourceBytes);
    assert.throws(()=>verifySourceBytes(Buffer.concat([sourceBytes,Buffer.from('\n')])));
    const mutation=Buffer.from(sourceBytes);mutation[100]^=1;
    assert.throws(()=>verifySourceBytes(mutation));
});

test('Qt DEM shader retains every fixed Web channel/shading/tint expression',()=>{
    const source=read('renderer/shaders/terrain.frag');
    requireLiteralDemMath(source);
    assert.equal((common.match(/elevation\(demSample\(/g)||[]).length,8);
    for(const mutation of [
        ['* 256.0','* 255.0'],['12000.0','11999.0'],
        ['vec2(0.5)','vec2(1.0)'],['40030228.884','40030228.0'],
        ['20015114.442','20015114.0'],['vec3(-0.5, 0.5','vec3(0.5, 0.5'],
        ['89.5','90.0'],['8000.0','800.0'],['0.6);','0.5);'],
        ['uLandPass > 0.5','elevation(encodedPixel) > 0.0'],
        ['(vLonLat.x + 180.0) / 360.0','vUv.x'],['0.5, 1.2','0.0, 1.2'],
    ])assert.throws(()=>requireLiteralDemMath(source.replace(...mutation)),/literal/);
    assert.match(source,/color\s*\*\s*vec3\(0\.60,\s*0\.68,\s*0\.76\),\s*uDarkTheme\s*\*\s*0\.48/);
});

test('geographic land-pass projection remains the exact fill shader route',()=>{
    const fill=read('renderer/shaders/fill.vert');
    const terrain=read('renderer/shaders/terrain.vert');
    for(const start of ['screen=vec2(u.flat0.x','float h=cos(lat)*sin(lon);','float v=sin(lat)*cos(u.globe0.y)','screen=vec2(u.globe1.x']) {
        const statement=fill.slice(fill.indexOf(start),fill.indexOf(';',fill.indexOf(start))+1);
        assert.ok(normalized(terrain).includes(normalized(statement)),start);
    }
    assert.match(terrain,/localUv=\(geographic-u\.bounds\.xy\)\/\(u\.bounds\.zw-u\.bounds\.xy\)/);
    assert.match(terrain,/vUv=mix\(u\.uvBounds\.xy,u\.uvBounds\.zw,localUv\)/);
    assert.match(terrain,/vLonLat\.x-=u\.effects\.w/);
    assert.doesNotMatch(terrain,/vLonLat\.x=mod/);
    assert.match(read('renderer/shaders/terrain.frag'),/lessThan\(vUv,u\.uvBounds\.xy\)/);
});

test('typed frame keeps RG raw; camera changes do not unconditionally delete GPU textures',()=>{
    const bridge=read('app/terrainimageprovider.cpp');
    const item=read('renderer/geographicimageitem.cpp');
    assert.match(bridge,/frame\.image=source->loadTile\(level,column,row,false\)/);
    assert.match(item,/if\(!node->texture\|\|node->imageKey!=uploadImage\.cacheKey\(\)\)/);
    assert.match(item,/std::map<Key,std::weak_ptr<QSGTexture>> cache/);
    const render=item.slice(item.indexOf('QSGNode* GeographicImageItem::updatePaintNode'));
    assert.doesNotMatch(render,/UpdatePaintNodeData\*\)\s*\{\s*delete previous/);
    assert.match(item,/const bool terrain=bool\(terrainBridge_\)/);
    assert.match(item,/rasterDisplayImage\(image_,gray\)/);
});

test('DEM requires a current ordered screen-space mask, never a height fallback',()=>{
    const fragment=read('renderer/shaders/terrain.frag');
    const vertex=read('renderer/shaders/terrain.vert');
    const item=read('renderer/geographicimageitem.cpp');
    const material=read('renderer/scenegraph/terrainmaterial.cpp');
    const contract=read('renderer/terrainrendercontract.cpp');
    assert.match(fragment,/layout\(binding=3\) uniform sampler2D uLandMask/);
    assert.match(vertex,/vMaskScreen=screen\*u\.maskMetrics\.xy/);
    assert.match(fragment,/u\.maskState\.x<0\.5\)discard/);
    assert.match(fragment,/effectiveLandPass=step\(0\.5,texture\(uLandMask,maskUv\)\.a\)/);
    assert.match(fragment,/uPhysicalStyle<0\.5&&effectiveLandPass<0\.5\)discard/);
    assert.match(item,/setFlag\(QSGNode::UsePreprocess,true\)/);
    assert.match(item,/QSGDynamicTexture/);
    assert.match(item,/Qt::DirectConnection/);
    assert.match(item,/renderThreadMaskFrame\(\)/);
    assert.match(item,/TerrainRenderContract::maskCurrent\(frame\.ready,mask&&mask->rhiTexture\(\)!=nullptr/);
    assert.match(contract,/frame\.viewRevision==request\.viewRevision/);
    assert.match(contract,/frame\.sceneRevision==request\.sceneRevision/);
    assert.match(item,/frame\.ready/);
    assert.match(material,/binding==3/);
    assert.doesNotMatch(fragment,/effectiveLandPass\s*=.*elevation/);
});

test('all terrain std140 stages and CPU upload agree on256bytes',()=>{
    const block=s=>s.match(/layout\(std140,binding=0\) uniform buf \{([\s\S]+?)\} u;/)[1];
    const vertex=block(read('renderer/shaders/terrain.vert'));
    const fragment=block(read('renderer/shaders/terrain.frag'));
    assert.equal(normalized(vertex),normalized(fragment));
    assert.equal((vertex.match(/vec4 /g)||[]).length,12);
    const material=read('renderer/scenegraph/terrainmaterial.cpp');
    assert.match(material,/bytes->size\(\)<256/);
    const upload=material.match(/const QVector4D vectors\[\]\{([\s\S]+?)\};/)[1];
    assert.deepEqual(upload.match(/m->\w+/g),['flat0','flat1','globe0','globe1','bounds','uvBounds','dimensions','options','effects','maskTransform','maskMetrics','maskState'].map(n=>`m->${n}`));
});

test('raster fallback uses opaque originalRGB or originalA withWebdark/gutter/mask',()=>{
    const fragment=read('renderer/shaders/terrain.frag');
    const item=read('renderer/geographicimageitem.cpp');
    const contract=read('renderer/terrainrendercontract.cpp');
    assert.match(contract,/QImage::Format_RGBX8888/);
    assert.match(contract,/source\[x\*4\+3\]/);
    assert.match(item,/setDisplayBacking\(this,rasterDisplayImage_\)/);
    assert.match(item,/terrainBridge_=next;image_=\{\};terrainFrame_=\{\};rasterDisplayImage_=\{\};rasterSourceKey_=0/);
    assert.match(item,/releaseDisplayBacking\(this\);return;/);
    assert.match(item,/setFiltering\(terrain\?QSGTexture::Linear:/);
    assert.match(read('app/terrainimageprovider.cpp'),/emit displayBackingChanged\(\)/);
    assert.match(fragment,/if\(u\.effects\.x>0\.5\|\|u\.options\.x<0\.5\)/);
    assert.match(fragment,/else color=texture\(uTerrain,vUv\)\.rgb/);
    assert.match(fragment,/color=mix\(color,color\*vec3\(0\.60,0\.68,0\.76\),uDarkTheme\*0\.48\)/);
    assert.match(item,/material->setTile/);
});

test('actual RHI raster scenario has independent pixels and explicit backing-release phases',()=>{
    const source=read('tests/terrain_gpu_display_tests.cpp');
    const raster=source.slice(source.indexOf('void rasterOriginalChannelsReachActualPixels()'));
    assert.match(raster,/QColor\(255,0,0,64\)/);
    assert.match(raster,/QColor\(255,0,0,255\)/);
    assert.match(raster,/QColor\(64,64,64,255\)/);
    assert.match(raster,/QColor\(52,54,57,255\)/);
    assert.match(raster,/QColor\(206,0,0,255\)/);
    assert.match(raster,/terrainTileValid/);
    assert.match(raster,/terrainEnabled/);
    assert.match(raster,/terrainBridge\.setSource\(\{\}\)/);
    assert.match(raster,/displayBackingBytes\(\)>=additionalBytes/);
    assert.match(raster,/QCOMPARE\(processed,8u\)/);
    assert.doesNotMatch(raster,/QSKIP/);
});
