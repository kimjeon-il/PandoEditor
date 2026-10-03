#!/usr/bin/env node
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { dirname, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import vm from 'node:vm';
import { createHash } from 'node:crypto';
import { OVERLAY_GROUPS } from '../tests/fixtures/web-current/source/layer-presentation.js';
import { layoutCountryFlags } from '../tests/fixtures/web-hydro/source/country-label-flags.js';

const source=resolve(dirname(fileURLToPath(import.meta.url)),'../tests/fixtures/web-hydro/source');
for(const [file,sha] of Object.entries({
  'gpu-base-scene-pass.js':'ca9886ff71dad974c2f15755b0fe6e8c4a997568',
  'app-object-picking.js':'56af01e20b8cb3c169028da795c90e5bab08ec7b',
  'layer-presentation.js':'531c81f888ace0e11c535efaa6f0bf37922a193f',
  'country-label-flags.js':'f95de74a7d51b80a83dd94d9cfb00f9a3bf7be86',
})){
  const bytes=Buffer.from((await readFile(resolve(source,file),'utf8')).replace(/\r\n/g,'\n'));
  assert.equal(createHash('sha1').update(`blob ${bytes.length}\0`).update(bytes).digest('hex'),sha,file);
}
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

// Preserve exact committed bytes; the adjacent web checkout can change during this task.
const pickBytes=Buffer.from(await readFile(new URL('../tests/fixtures/web-current/source/app-object-picking.js.base64',import.meta.url),'utf8'),'base64');
const pickingSource=pickBytes.toString('utf8');
assert.equal(createHash('sha1').update(`blob ${pickBytes.length}\0`).update(pickBytes).digest('hex'),'5997bbced0700e03d22aa2517b6db6d00d11b51f');
const start=pickingSource.indexOf('  function selectableVisualRank(ref) {');
const end=pickingSource.indexOf('\n  async function selectableObjectsAt',start);
assert.ok(start>=0&&end>start);
const dependencies={
  projectState:{state:{layerPresentation:{overlayOrder:[
    ...OVERLAY_GROUPS]}}},
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
// Territory is submitted before the country fill, but its stencil ownership
// prevents the later country pass from covering its pixels. This is the
// effective visible order for an opaque overlap, not the raw GL call order.
const visualDraw=[...draw];
const territory=visualDraw.indexOf('territory-fill');
const country=visualDraw.indexOf('country-fill');
assert.ok(territory>=0&&country>territory);
visualDraw.splice(territory,1);
visualDraw.splice(country,0,'territory-fill');
const visibleRank={
  country:visualDraw.indexOf('country-fill'),
  subunit:visualDraw.indexOf('territory-fill')+.1,
  region:visualDraw.indexOf('territory-fill')+.2,
  religion:visualDraw.indexOf('polygon-overlay')+.01,
  ethnicity:visualDraw.indexOf('polygon-overlay')+.02,
  language:visualDraw.indexOf('polygon-overlay')+.03,
  generic:visualDraw.indexOf('polygon-overlay')+.06,
  lake:visualDraw.indexOf('lake'),river:visualDraw.indexOf('river'),
  'lake-boundary':visualDraw.indexOf('lake-boundary'),
  'country-boundary':visualDraw.indexOf('country-boundary'),
  'generic-line':visualDraw.indexOf('stroke-overlay'),
  place:visualDraw.length,label:visualDraw.length+1,
};
const refsByKey=Object.fromEntries(refs.map(ref=>[ref.key,ref]));
refsByKey.place=refsByKey.label;
refsByKey['lake-boundary']=refsByKey.lake;
refsByKey['country-boundary']=refsByKey.country;
refsByKey['generic-line']=refsByKey.generic;
const objects=['country','river','lake','religion','ethnicity','language',
  'subunit','region','generic','place','label'];
const pairNames=objects.flatMap((left,i)=>objects.slice(i+1).map(right=>[left,right]));
pairNames.push(['lake','lake-boundary'],['lake-boundary','river'],
  ['river','country-boundary'],['country-boundary','generic-line'],
  ['generic-line','place']);
const pairs=pairNames.map(([left,right])=>({pair:`${left}/${right}`,
  top:visibleRank[left]>visibleRank[right]?left:right,
  chooserFirst:refsByKey[left]===refsByKey[right]?'same-ref':
    rank(refsByKey[left])===rank(refsByKey[right])?
      (left.localeCompare(right,'ko')<0?left:right):
      rank(refsByKey[left])>rank(refsByKey[right])?left:right}));
const flags=layoutCountryFlags([{sourceType:'country',source:{id:'country'},nameVisible:true,
  box:{left:10,right:30,top:10,bottom:20}}],
{enabled:true,zoom:2,flagUrl:()=>'/flag.svg',isVisible:()=>true});
assert.equal(flags.get('country')?.url,'/flag.svg');
const actual={visualDraw,pick,pairs};
if(process.argv.includes('--web-only'))console.log(JSON.stringify(actual));
else {
  const probe=process.argv.at(-1);
  assert.ok(probe&&!probe.startsWith('--'));
  assert.deepEqual(JSON.parse(execFileSync(probe,{encoding:'utf8'})),actual);
  console.log('web map draw/pick order parity passed');
}
