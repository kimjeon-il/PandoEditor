import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,mkdtempSync,cpSync,appendFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import path from 'node:path';
import vm from 'node:vm';
import {adapt,generate,extractPacketDependency,PINS,CUT,ROOT} from './cut-kernel-generate.mjs';
const platform=readFileSync(path.join(ROOT,'assets/geometry/river/platform.js'),'utf8');
test('cut adapters regenerate byte-identically and restore every original byte outside guarded replacements',()=>{
  const first=generate(),second=generate();assert.deepEqual(first,second);
  for(const [file,bytes] of first)assert.deepEqual(readFileSync(file),bytes,file);
  let replacements=0;
  for(const name of Object.keys(PINS)){
    const original=readFileSync(path.join(CUT,'original',name),'utf8'),{adapted,edits}=adapt(original);
    let restored=adapted,delta=0;
    const offsets=edits.map(edit=>{const result={...edit,offset:edit.start+delta};delta+=edit.after.length-(edit.end-edit.start);return result;});
    for(const edit of offsets.reverse())restored=restored.slice(0,edit.offset)+edit.before+restored.slice(edit.offset+edit.after.length);
    assert.equal(restored,original,name);replacements+=edits.length;
  }
  assert.equal(replacements,15);
});
test('cut adapter refuses changed pinned source rather than silently regenerating',()=>{
  const dir=mkdtempSync(path.join(tmpdir(),'cut-adapter-'));
  try {cpSync(CUT,dir,{recursive:true});appendFileSync(path.join(dir,'original/cut-worker-preparation.js'),'\n');
    assert.throws(()=>generate(dir),/CUT_ORIGINAL_HASH_MISMATCH/);
  }finally{rmSync(dir,{recursive:true,force:true});}
});
test('syntax bridge preserves spread enumeration, copy timing, data properties and symbols',()=>{
  const source=`(() => {const events=[];const symbol=Symbol('item');let state=1;
    const source={get a(){events.push('copy');return state;}, [symbol]:3};
    const result={...source, later:(state=2), ...null, ...'xy'};
    return {events,a:result.a,later:result.later,zero:result[0],symbol:result[symbol],own:Object.getOwnPropertyDescriptor(result,'a').get===undefined};})()`;
  // General computed/accessor definitions are outside this cut adapter's rewrite;
  // only the spread-containing result literal is rewritten.
  const original=vm.runInNewContext(source);
  const adapted=vm.runInNewContext(platform+'\n'+adapt(source).adapted);
  assert.equal(JSON.stringify(adapted),JSON.stringify(original));
});
test('adapter fails closed on unsupported spread-object forms',()=>{
  for(const source of ['({...x,get a(){return 1;}})','({...x,[name]:1})','({...x,__proto__:y})','({...x,nested:{...y}})'])
    assert.throws(()=>adapt(source),/CUT_ADAPTER_(UNSUPPORTED_PROPERTY|NESTED_SPREAD)/);
});
test('original and syntax-adapted cut workers retain full Node-observed results without mutation',async()=>{
  const {pathToFileURL}=await import('node:url');
  const context=vm.createContext({});
  const fixture=path.join(ROOT,'tests/fixtures/web-m97/lifecycle-source/assets/js');
  vm.runInContext(readFileSync(path.join(fixture,'vendor/d3.min.js'),'utf8'),context);
  vm.runInContext(readFileSync(path.join(fixture,'vendor/polygon-clipping.min.js'),'utf8'),context);
  vm.runInContext(readFileSync(path.join(fixture,'modules/polygon-geometry.js'),'utf8'),context);
  vm.runInThisContext(platform);
  const original=await import(pathToFileURL(path.join(fixture,'modules/cut-worker-preparation.js')));
  const adapted=await import(pathToFileURL(path.join(CUT,'adapted/cut-worker-preparation.js')));
  const cases=JSON.parse(readFileSync(path.join(ROOT,'tests/fixtures/web-m97/cut-corpus.json')));
  for(const row of cases){
    const a=JSON.parse(JSON.stringify(row.payload)),b=JSON.parse(JSON.stringify(row.payload));
    const run=(module,input)=>JSON.parse(JSON.stringify(module.prepareCutInWorker(input,context.PandoLabPolygonGeometry,context.d3,context.polygonClipping)));
    assert.deepEqual(run(original,a),row.result,row.id+' original');
    assert.deepEqual(run(adapted,b),row.result,row.id+' adapted');
    assert.deepEqual(a,row.payload,row.id+' original input');assert.deepEqual(b,row.payload,row.id+' adapted input');
  }
});
test('cut-only packet dependency retains the exact freezing export and private WeakSet',()=>{
  const original=readFileSync(path.join(CUT,'original/editing-render-packet.js'),'utf8');
  const expected=original.slice(0,original.indexOf('\nconst coordinate ='));
  const actual=generate().get(path.join(CUT,'adapted/editing-render-packet.js')).toString();
  assert.equal(actual,expected);
  assert.equal((actual.match(/export /g)||[]).length,1);
});

test('all baseline and corrected module imports resolve to retained exports',async()=>{
  const {loadPinnedAcorn}=await import('./river/generate-d3-adapter.mjs');const parser=loadPinnedAcorn();
  for(const directory of ['adapted','corrected-adapted']){
   const modules=new Map();for(const name of Object.keys(PINS))modules.set(name,parser.parse(readFileSync(path.join(CUT,directory,name),'utf8'),{ecmaVersion:2022,sourceType:'module'}));
   for(const [name,ast] of modules)for(const node of ast.body)if(node.type==='ImportDeclaration'){
    const target=modules.get(node.source.value.replace('./',''));assert.ok(target,directory+'/'+name+' import '+node.source.value);
    const exports=new Set(target.body.filter(row=>row.type==='ExportNamedDeclaration').flatMap(row=>[...(row.declaration?.id?[row.declaration.id.name]:(row.declaration?.declarations||[]).map(value=>value.id.name)),...(row.specifiers||[]).map(value=>value.exported.name)]));
    for(const specifier of node.specifiers)assert.ok(exports.has(specifier.imported.name),directory+'/'+name+' imported '+specifier.imported.name);
   }
  }
});
test('UI dependency pruning refuses newly imported or referenced omitted exports',()=>{
  const sources=Object.fromEntries(Object.keys(PINS).map(name=>[name,readFileSync(path.join(CUT,'original',name),'utf8')]));
  const source=sources['editing-render-packet.js'];
  for(const reference of ["import { adoptBoundaryRenderPacket } from './editing-render-packet.js';", "const x=adoptBoundaryRenderPacket;", "import * as packets from './editing-render-packet.js';", "import('./editing-render-packet.js');"])
    assert.throws(()=>extractPacketDependency(source,{...sources,'extra.js':reference}),/CUT_PACKET_(UNEXPECTED_IMPORT|OMITTED_EXPORT_REFERENCE)/);
});

test('approved dateline overlay is explicit while baseline originals remain immutable',()=>{
 const manifest=JSON.parse(readFileSync(path.join(CUT,'provenance.json')));
 assert.equal(manifest.approvedCorrections?.behavioralCommit,'07d3e2053c71573e11c5cf89151f5f6686038511');
 for(const row of manifest.modules){assert.equal(typeof row.correctedSha256,'string');assert.equal(readFileSync(path.join(CUT,'corrected-adapted',row.name)).length>0,true);}
});

test('approved corrected original and corrected Qt syntax bridge match all ten dateline inputs',async()=>{
 const {loadSplitCandidateModules}=await import('./web-split-candidate.mjs');const {pathToFileURL}=await import('node:url');
 const candidateRoot=path.join(ROOT,'tests/fixtures/web-m973-split/corrections/dateline');const manifest=JSON.parse(readFileSync(path.join(candidateRoot,'manifest.json')));
 const loaded=await loadSplitCandidateModules({candidateRoot,candidateChanges:manifest.changes});
 try {
  const context=vm.createContext({});const fixture=path.join(ROOT,'tests/fixtures/web-m97/lifecycle-source/assets/js');
  vm.runInContext(readFileSync(path.join(fixture,'vendor/d3.min.js'),'utf8'),context);vm.runInContext(readFileSync(path.join(fixture,'vendor/polygon-clipping.min.js'),'utf8'),context);
  vm.runInContext(readFileSync(path.join(candidateRoot,'assets/js/modules/polygon-geometry.js'),'utf8'),context);if(!globalThis.__riverOwnDataMerge)vm.runInThisContext(platform);
  const original=await import(pathToFileURL(path.join(loaded.root,'assets/js/modules/cut-worker-preparation.js')));const adapted=await import(pathToFileURL(path.join(CUT,'corrected-adapted/cut-worker-preparation.js')));
  for(const definition of JSON.parse(readFileSync(path.join(candidateRoot,'case-definitions.json')))){
   const payload={source:definition.source,coords:definition.coords,view:definition.view,buildPreview:true},a=structuredClone(payload),b=structuredClone(payload);
   const run=(module,input)=>JSON.parse(JSON.stringify(module.prepareCutInWorker(input,context.PandoLabPolygonGeometry,context.d3,context.polygonClipping)));
   assert.deepEqual(run(adapted,b),run(original,a),definition.id);assert.deepEqual(a,payload);assert.deepEqual(b,payload);
  }
 }finally{loaded.cleanup();}
});
