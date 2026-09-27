#!/usr/bin/env node
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {readFileSync, writeFileSync} from 'node:fs';
import {join, resolve} from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {runInNewContext} from 'node:vm';

const worldMapCommit = 'c0bd31d13dc8495593d78cf51f7cc195de7c9469';
const pinned = Object.freeze({
  'render-scene.js': ['assets/js/modules/render-scene.js', 'd780aa573bd2576cbc3b182ccf55cb92ebe68967'],
  'render-lod.js': ['assets/js/modules/render-lod.js', '13f91ce3bda90201966a866be304bdbca49102d4'],
  'gpu-map-renderer.js': ['assets/js/modules/gpu-map-renderer.js', 'eb5f9542caa2c20e20ffe74771caf3909838cd2d'],
  'selection-stroke-geometry.js': ['assets/js/modules/selection-stroke-geometry.js', 'a0a9a2282537c64e7cff06fb07b237748f58bbdd'],
  'layer-presentation.js': ['assets/js/modules/layer-presentation.js', '531c81f888ace0e11c535efaa6f0bf37922a193f'],
  'geographic-boundary.js': ['assets/js/modules/geographic-boundary.js', '37cac30e70fd905b8fecaac2cb26532ef390ff79'],
});

function gitBlobSha(bytes) {
  return createHash('sha1').update(`blob ${bytes.length}\0`).update(bytes).digest('hex');
}

export function verifyPinnedSources(root) {
  for(const [name,[path,sha]] of Object.entries(pinned)) {
    const bytes=readFileSync(join(root,'source',name));
    if(gitBlobSha(bytes)!==sha) throw new Error(`blob mismatch: ${name}`);
    if(path!==`assets/js/modules/${name}`) throw new Error(`unexpected web path: ${path}`);
  }
  return true;
}

function webWorldOffsets(root) {
  const source=readFileSync(join(root,'source','gpu-map-renderer.js'),'utf8');
  const start=source.indexOf('export function visibleFlatWorldOffsets(');
  const end=source.indexOf('export function createCountryGeometryRevisionTracker(',start);
  if(start<0||end<start) throw new Error('pinned web world offset function missing');
  const declaration=source.slice(start,end).replace(/^export /,'');
  return runInNewContext(`${declaration}\nvisibleFlatWorldOffsets`, {Math,Number});
}

export async function generateExpected(root) {
  verifyPinnedSources(root);
  const {buildPolygonGeometryPacket,buildStrokeGeometryPacket,createRenderSceneBuilder}=
    await import(pathToFileURL(join(root,'source','render-scene.js')).href);
  const {layerObjectRank,OVERLAY_GROUPS}=
    await import(pathToFileURL(join(root,'source','layer-presentation.js')).href);
  const offsets=webWorldOffsets(root);
  const rectangle={type:'Polygon',coordinates:[[[0,0],[2,0],[2,2],[0,2],[0,0]]]};
  const dateline={type:'Polygon',coordinates:[[[179,70],[-179,70],[-179,80],[179,80],[179,70]]]};
  const hole={type:'Polygon',coordinates:[rectangle.coordinates[0],[[.5,.5],[.5,1],[1,1],[1,.5],[.5,.5]]]};
  const rectanglePacket=buildPolygonGeometryPacket(rectangle);
  const datelinePacket=buildPolygonGeometryPacket(dateline);
  const holePacket=buildPolygonGeometryPacket(hole);
  const strokePacket=buildStrokeGeometryPacket({type:'LineString',coordinates:[[0,0],[0,0],[1,0]]});
  const builder=createRenderSceneBuilder();
  const a={key:'a',geometryRevision:1,order:10,geometry:rectangle,style:{color:'#112233'}};
  const b={key:'b',geometryRevision:1,order:20,geometry:rectangle,style:{color:'#445566'}};
  const first=builder.build({polygons:[b,a],revisions:{view:1}});
  const styled=builder.build({polygons:[{...b,style:{color:'#ffffff'}},a],revisions:{style:2,view:1}});
  const selected=builder.build({polygons:[b,a],revisions:{selection:2,view:1},interaction:{selectionPacket:{key:'a'}}});
  const viewed=builder.build({polygons:[b,a],revisions:{view:2}});
  const patch=builder.patch(viewed,{removePolygonKeys:['a'],revisions:{view:3}});
  return {
    schema:'pandoeditor-m72-web-render-oracle',version:1,worldMapCommit,
    sources:Object.fromEntries(Object.entries(pinned).map(([name,[path,gitBlobSha]])=>[name,{path,gitBlobSha}])),
    flatWorldOffsets:{
      center:Array.from(offsets({translateX:300,scale:100,viewportWidth:600})),
      rightVisible:Array.from(offsets({translateX:0,scale:100,viewportWidth:600})),
      noOverlapFallback:Array.from(offsets({translateX:5000,scale:100,viewportWidth:600})),
    },
    packet:{
      rectangle:{vertexCount:rectanglePacket.vertexCount,triangleCount:rectanglePacket.triangleCount,
        indices:Array.from(rectanglePacket.indices),ringOffsets:Array.from(rectanglePacket.ringOffsets)},
      datelineLongitudes:Array.from(datelinePacket.positions).filter((_,i)=>i%2===0),
      hole:{ringOffsets:Array.from(holePacket.ringOffsets),polygonOffsets:Array.from(holePacket.polygonOffsets)},
      strokeSegmentCount:strokePacket.segmentCount,
    },
    cache:{styleReusesGeometry:first.polygons[0].positions===styled.polygons[0].positions,
      selectionReusesGeometry:first.polygons[0].positions===selected.polygons[0].positions,
      viewReusesGeometry:first.polygons[0].positions===viewed.polygons[0].positions,
      hits:builder.stats().polygonCacheHits,misses:builder.stats().polygonCacheMisses},
    patch:{orderedKeys:first.polygons.map(p=>p.key),afterRemoval:patch.polygons.map(p=>p.key),
      revision:patch.revision},
    drawOrder:{overlayGroups:Array.from(OVERLAY_GROUPS),
      firstRank:layerObjectRank({objectOrder:['a','b']},'a'),
      secondRank:layerObjectRank({objectOrder:['a','b']},'b')},
  };
}

export async function verifyExpected(root) {
  const actual=await generateExpected(root);
  const bytes=readFileSync(join(root,'expected.json'),'utf8');
  assert.deepStrictEqual(actual,JSON.parse(bytes));
  return actual;
}

if(process.argv[1]&&resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
  try {
    const root=resolve(process.argv[3]??'tests/fixtures/web-m72');
    if(process.argv[2]==='--write')
      writeFileSync(join(root,'expected.json'),JSON.stringify(await generateExpected(root),null,2)+'\n');
    else if(process.argv[2]==='--verify') await verifyExpected(root);
    else throw new Error('usage: m72-render-oracle.mjs --write|--verify [fixture-root]');
    console.log('M7.2 pinned web oracle: PASS');
  } catch(error) {console.error(`M7.2 web oracle: ${error.message}`);process.exitCode=1;}
}
