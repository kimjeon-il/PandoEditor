#!/usr/bin/env node
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { dirname, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import vm from 'node:vm';

const source=resolve(dirname(fileURLToPath(import.meta.url)),'../tests/fixtures/web-hydro/source');
const { drawGpuBaseScene }=await import(pathToFileURL(resolve(source,'gpu-base-scene-pass.js')));
const draw=[];
const gl=new Proxy({}, {get:()=>()=>{}});
const item=(key,kind='polygon')=>({kind,packet:{key,role:key}});
const pass={hasResource:()=>true,
  drawPackets:packets=>{draw.push(...packets.map(row=>row.key));return {renderedKeys:[]}},
  drawBatches:packets=>{draw.push(...packets.map(row=>row.key));return {renderedKeys:[]}}};
drawGpuBaseScene({
  gl,frame:{},width:800,height:500,terrainVisible:false,countriesVisible:true,
  countries:{mesh:{triangleIndices:[]},overrideMesh:null,fillProgram:'country-fill'},
  prepared:{baseTriangleDraw:{ranges:[]},baseBoundaryDraw:{ranges:[]},
    overrideTriangleDraw:{ranges:[]},overrideBoundaryDraw:{ranges:[]},
    territoryItems:[item('territory-fill')],
    polygonItems:[item('polygon-overlay')],strokeItems:[item('stroke-overlay','stroke')],
    deferredOverlayKeys:new Set(),failedOverlayKeys:new Set()},
},{drawProgram:program=>{if(program==='country-fill')draw.push('country-fill')},
  renderTerrain:()=>draw.push('terrain'),drawHydro:kind=>draw.push(kind),
  drawCountryBoundaryStrokes:()=>draw.push('country-boundary'),
  polygonOverlayPass:pass,strokeRenderer:pass});

const pickingSource=await readFile(resolve(source,'app-object-picking.js'),'utf8');
const start=pickingSource.indexOf('  function selectableVisualRank(ref) {');
const end=pickingSource.indexOf('\n  async function selectableObjectsAt',start);
assert.ok(start>=0&&end>start);
const dependencies={
  projectState:{state:{layerPresentation:{overlayOrder:[
    'religions','ethnicities','languages','subunits','regions','genericFeatures']}}},
  renderScene:{OVERLAY_GROUPS:[]},
  distributionPresentation:{DISTRIBUTION_TYPE_GROUPS:{religion:'religions',ethnicity:'ethnicities',language:'languages'}},
  territorialModel:{TERRITORIAL_UNIT_TYPES:{COUNTRY:'country',SUBUNIT:'subunit',REGION:'region'}},
};
const rank=vm.runInNewContext(`(${pickingSource.slice(start,end).trim()})`,{dependencies});
const refs=[
  {key:'country',domain:'territorial',type:'country'},
  {key:'river',domain:'hydro',type:'river'},
  {key:'lake',domain:'hydro',type:'lake'},
  ...['religion','ethnicity','language'].map(type=>({key:type,domain:'distribution',type})),
  {key:'subunit',domain:'territorial',type:'subunit'},
  {key:'region',domain:'territorial',type:'region'},
  {key:'generic',domain:'generic',type:'feature'},
  {key:'label',domain:'label',type:'city'},
];
const pick=refs.sort((a,b)=>rank(b)-rank(a)).map(ref=>ref.key);
const actual={draw,pick};
if(process.argv.includes('--web-only'))console.log(JSON.stringify(actual));
else {
  const probe=process.argv.at(-1);
  assert.ok(probe&&!probe.startsWith('--'));
  assert.deepEqual(JSON.parse(execFileSync(probe,{encoding:'utf8'})),actual);
  console.log('web map draw/pick order parity passed');
}
