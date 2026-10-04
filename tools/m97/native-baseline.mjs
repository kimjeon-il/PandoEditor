import {readFileSync,writeFileSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {loadOracle} from './calculations.mjs';

export function nativeInput(row) {
  const common={case:row.id,countries:row.features,units:[]};
  if(['drawn-annex','annex'].includes(row.operation)&&row.donorIds?.length)
    return {...common,operation:row.operation,targetId:row.targetId,sourceIds:[...row.donorIds],sourceId:row.donorIds[0],draft:row.drawnGeometry??row.transferredGeometry};
  if(row.operation==='merge')return {...common,operation:'merge',targetId:row.sourceId,sourceIds:row.targetIds};
  return null;
}
function checkedFeatures(features, side) {
  if(!Array.isArray(features))throw Error(`${side}: missing feature observations`);
  const ids=new Set();
  for(const feature of features) {
    if(typeof feature?.id!=='string'||!feature.id)throw Error(`${side}: invalid identity`);
    if(ids.has(feature.id))throw Error(`${side}: Duplicate identity ${feature.id}`);
    ids.add(feature.id);
    if(typeof feature.properties?.parentId!=='string')throw Error(`${side}: missing explicit parent relation`);
    const geometry=feature.geometry;
    if(!geometry||!['Polygon','MultiPolygon'].includes(geometry.type)||!Array.isArray(geometry.coordinates))throw Error(`${side}: invalid geometry`);
    const polygons=geometry.type==='Polygon'?[geometry.coordinates]:geometry.coordinates;
    if(!polygons.length)throw Error(`${side}: empty geometry`);
    for(const polygon of polygons) {
      if(!Array.isArray(polygon)||!polygon.length)throw Error(`${side}: invalid geometry polygon`);
      for(const ring of polygon) {
        if(!Array.isArray(ring)||ring.length<4)throw Error(`${side}: invalid ring`);
        for(const coordinate of ring)if(!Array.isArray(coordinate)||coordinate.length!==2||!coordinate.every(Number.isFinite))throw Error(`${side}: invalid coordinate`);
        if(ring[0][0]!==ring.at(-1)[0]||ring[0][1]!==ring.at(-1)[1])throw Error(`${side}: unclosed ring`);
      }
    }
  }
  return new Map(features.map(feature=>[feature.id,feature]));
}
export function compareCalculation(webResult,nativeResult,clipper) {
  const web=webResult.ok?checkedFeatures(webResult.afterFeatures,'web'):null;
  const native=nativeResult.ok?checkedFeatures(nativeResult.features,'native'):null;
  if(webResult.ok!==nativeResult.ok)return [{field:'outcome',web:webResult.ok,native:nativeResult.ok,webError:webResult.error??null,nativeError:nativeResult.error??null}];
  if(!webResult.ok)return []; // Rejection agreement only; error semantics not compared.
  const differences=[];
  const multi=g=>g.type==='Polygon'?[g.coordinates]:g.coordinates;
  for(const id of [...new Set([...web.keys(),...native.keys()])].sort()) {
    if(!web.has(id)||!native.has(id)){differences.push({field:'object',id,web:web.has(id),native:native.has(id)});continue;}
    const a=web.get(id),b=native.get(id),parentA=String(a.properties?.parentId??''),parentB=String(b.properties?.parentId??'');
    if(parentA!==parentB)differences.push({field:'parent',id,web:parentA,native:parentB});
    // Exact topological XOR, with no tolerance that could hide a lost island.
    const xor=clipper.xor(multi(a.geometry),multi(b.geometry));
    if(xor.length)differences.push({field:'geometry',id,xor:structuredClone(xor)});
  }
  return differences;
}
export function nativeCoverage(native, web) {
  return {stages:{before:false,preview:false,cancel:false,confirm:native?.ok===true,undo:false,redo:false},
    retainedRefs:false,errorSemantics:native?.ok===true&&web?.ok===true?'not-applicable':'unobserved'};
}
export async function collectBaseline(binary) {
  const corpus=JSON.parse(readFileSync(new URL('../../tests/fixtures/web-m97/calculation-corpus.json',import.meta.url)));
  const oracle=await loadOracle();
  const supported=corpus.cases.filter(row=>nativeInput(row));
  const processResult=spawnSync(binary,[],{input:JSON.stringify(supported.map(nativeInput)),encoding:'utf8',maxBuffer:32*1024*1024});
  if(processResult.error||processResult.status!==0)throw Error(`Native probe failed: ${processResult.error?.message??processResult.stderr}`);
  const results=JSON.parse(processResult.stdout);
  if(!Array.isArray(results)||results.length!==supported.length)throw Error('Incomplete native probe result');
  const cases=corpus.cases.map(row=>{
    const index=supported.indexOf(row);
    const scope=nativeCoverage(null,null);
    if(index<0)return {id:row.id,status:'unobserved',reason:'Existing M4 probe has no equivalent entrypoint',...scope};
    const native=results[index];
    if(native.case!==row.id||typeof native.ok!=='boolean'||(native.ok&&!Array.isArray(native.features))||(!native.ok&&typeof native.error!=='string'))throw Error(`Invalid native observation: ${row.id}`);
    const web=oracle.calculate(row),differences=compareCalculation(web,native,oracle.clipper);
    return {id:row.id,status:differences.length?'mismatch':'calculation-matched',differences,...nativeCoverage(native,web),web,native};
  });
  return {schema:'pandoeditor-m971-native-baseline',version:1,sourceCommit:oracle.manifest.behavioralCommit,
    scope:'Calculation and final object identity only; never full editing parity.',parityComplete:false,cases};
}
if(process.argv[1]===fileURLToPath(import.meta.url)) {
  try {
    const [binary,path]=process.argv.slice(2);
    if(!binary||!path)throw Error('usage: native-baseline.mjs <m4_geometry_probe> <report.json>');
    const report=await collectBaseline(binary);writeFileSync(path,JSON.stringify(report,null,2)+'\n');
    console.log(JSON.stringify({parityComplete:false,counts:Object.fromEntries(['calculation-matched','mismatch','unobserved'].map(s=>[s,report.cases.filter(c=>c.status===s).length]))}));
  }catch(error){console.error(error);process.exitCode=1;}
}
