// Deterministic syntax-only Qt adapter for pinned, application-owned modules.
// Only object spread is rewritten. An unrelated UI dependency is reduced to its
// exact imported prefix. Every retained arithmetic expression is unchanged.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
import {loadPinnedAcorn} from './river/generate-d3-adapter.mjs';
export const ROOT=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
export const CUT=path.join(ROOT,'assets/geometry/cut');
const sha256=bytes=>createHash('sha256').update(bytes).digest('hex');
export const PINS={
  "map-edit-geometry.js": "0560653313500d5e9a4ca62fa5611cc4f4c18f3fb1cac353a828278094a1504e",
  "app-country-validation.js": "f1198177b310e835b0ac7febffa38ec2e3711d9091d4f5973deeea00ae40d513",
  "app-cut-geometry.js": "b91c06a4eddc151890d4ea381eba7c57807d50fb34b19aa460e17a9e9f22cc30",
  "app-land-relations.js": "c98e42a15794fcfa967fd9b5efe2cbb81c3fbcc9bb7db1c9db97d39eb10ab93e",
  "app-territory-components.js": "c82290d2e3601a4d07b6d94746a665bfad9a49c1020f447a0ace90740ef226d9",
  "boundary-spatial-index.js": "e8721595a89faadfdaa8c61a70505c8f941e12db8ceed189ba829904d2878b5f",
  "coordinate-bounds.js": "67e2598e59d6e5ff98d871566332286dc83d7e4f9fbc3dc1236c9cc9d08665e0",
  "cut-preparation-cache.js": "38278991e38a0976e0337d805a0d44a611e453d80c2e91f8cb47780f8e8d1a11",
  "cut-worker-preparation.js": "f80584f053717baa9cc172bbc8e26ed5a01316ae64c00f091744348c4736f2a2",
  "editing-render-packet.js": "2f1f97a7758a3f1930065511878bc19acd872dcea04ea8362bf8b40c5327f387",
  "geometry-segment-index.js": "63680763f49fd03345b1503a135f17c8b66b538673d040dacd69f6a6b648aeb1",
  "geometry-versions.js": "e28a1992e25049a32ef8accd7a1e5bfa4871a85556c0a6aa5a6cbef61f27333a",
  "planar-graph-faces.js": "283da7701c21cb80e4fd9ef97e8f68e69a8d6f0ec9fc611ee8c23cab45d6c804",
  "polygon-geometry.js": "cc987c4076861a02a5d60720ebf536908175a9f1a605a86701ae4cf50c3a3fb5",
  "ring-hit-test.js": "2f35ea5f6b8e01a14ad269b6689cf45f5212d1595a7c7bccc5ff39e8bb89f0b0",
  "territorial-geometry.js": "25f9b30cc2527d3eafdb3473d0fe8dbc056fc654ce7ff51fc72861e47b3ee827"
};
export function adapt(source, parser=loadPinnedAcorn()) {
  const ast=parser.parse(source,{ecmaVersion:2022,sourceType:'module'}), edits=[];
  function visit(node) {
    if(!node||typeof node!=='object')return;
    if(node.type==='ObjectExpression'&&node.properties.some(p=>p.type==='SpreadElement')) {
      // Pinned expressions have no methods, accessors, computed keys or nested
      // spread objects. Refuse such future inputs rather than widening semantics.
      for(const property of node.properties)if(property.type!=='SpreadElement'&&
        (property.type!=='Property'||property.kind!=='init'||property.method||property.computed||property.key.name==='__proto__'))
        throw new Error('CUT_ADAPTER_UNSUPPORTED_PROPERTY');
      let expression='{}', ordinary=[];
      function flush() {if(ordinary.length){expression=`__riverOwnDataMerge(${expression}, {${ordinary.join(', ')}})`;ordinary=[];}}
      for(const property of node.properties){
        if(property.type==='SpreadElement'){flush();expression=`__riverOwnDataMerge(${expression}, ${source.slice(property.argument.start,property.argument.end)})`;}
        else ordinary.push(source.slice(property.start,property.end));
      }
      flush();edits.push({start:node.start,end:node.end,before:source.slice(node.start,node.end),after:expression});
    }
    for(const [key,value] of Object.entries(node))if(!['start','end'].includes(key)){
      if(Array.isArray(value))value.forEach(visit);else if(value&&typeof value==='object')visit(value);
    }
  }
  visit(ast);edits.sort((a,b)=>a.start-b.start);
  for(let i=1;i<edits.length;++i)if(edits[i].start<edits[i-1].end)throw new Error('CUT_ADAPTER_NESTED_SPREAD');
  let adapted=source;for(const edit of edits.slice().reverse())adapted=adapted.slice(0,edit.start)+edit.after+adapted.slice(edit.end);
  parser.parse(adapted,{ecmaVersion:2022,sourceType:'module'});
  return {adapted,edits};
}
export function extractPacketDependency(source, sources, parser=loadPinnedAcorn()) {
  const ast=parser.parse(source,{ecmaVersion:2022,sourceType:'module'});
  const declaration=ast.body[1];
  if(ast.body[0]?.type!=='VariableDeclaration'||ast.body[0].declarations[0]?.id.name!=='immutableGeometries'||
     declaration?.type!=='ExportNamedDeclaration'||declaration.declaration?.id.name!=='freezeEditingGeometry')
    throw new Error('CUT_PACKET_PREFIX_SHAPE');
  const omitted=ast.body.slice(2).filter(node=>node.type==='ExportNamedDeclaration').flatMap(node=>
    node.declaration?.id?[node.declaration.id.name]:(node.declaration?.declarations||[]).map(row=>row.id.name));
  let imported=0;
  for(const [name,text] of Object.entries(sources)) {
    if(name==='editing-render-packet.js')continue;
    const module=parser.parse(text,{ecmaVersion:2022,sourceType:'module'});
    for(const node of module.body)if(node.type==='ImportDeclaration'&&node.source.value==='./editing-render-packet.js') {
      if(node.specifiers.length!==1||node.specifiers[0].type!=='ImportSpecifier'||node.specifiers[0].imported.name!=='freezeEditingGeometry')
        throw new Error('CUT_PACKET_UNEXPECTED_IMPORT');
      ++imported;
    }
    if(omitted.some(symbol=>new RegExp('\\b'+symbol+'\\b').test(text))||/import\s*\(/.test(text)||/export[^;]+from\s+['"]\.\/editing-render-packet\.js/.test(text))
      throw new Error('CUT_PACKET_OMITTED_EXPORT_REFERENCE');
  }
  if(imported!==1)throw new Error('CUT_PACKET_IMPORT_COUNT');
  const end=declaration.end+1;
  if(source[end-1]!=='\n')throw new Error('CUT_PACKET_PREFIX_BOUNDARY');
  return {adapted:source.slice(0,end),edits:[],dependencyPruning:{
    retainedByteRange:[0,Buffer.byteLength(source.slice(0,end))],retainedExports:['freezeEditingGeometry'],
    omittedExports:omitted,reason:'The complete cut import closure consumes only freezeEditingGeometry and its private WeakSet. Unreachable UI packet exports include async syntax unsupported by Qt; the retained prefix is byte-identical.'}};
}
export function generate(cut=CUT) {
  const parser=loadPinnedAcorn(),outputs=new Map(),modules=[];
  const sources=Object.fromEntries(Object.keys(PINS).map(name=>[name,readFileSync(path.join(cut,'original',name),'utf8')]));
  const upstream=JSON.parse(readFileSync(path.join(ROOT,'tests/fixtures/web-m97/lifecycle-manifest.json')));
  const correctionRoot=path.join(ROOT,'tests/fixtures/web-m973-split/corrections/dateline');
  const correction=JSON.parse(readFileSync(path.join(correctionRoot,'manifest.json')));
  if(correction.behavioralCommit!=='07d3e2053c71573e11c5cf89151f5f6686038511'||correction.baseBehavioralCommit!=='6c3f930b8573fa09991885b661879ea36725472e')throw Error('CUT_CORRECTION_PIN_MISMATCH');
  const activeSources={...sources};
  for(const row of correction.changes){const name=path.basename(row.path);if(Object.hasOwn(PINS,name)){const bytes=readFileSync(path.join(correctionRoot,row.path));if(sha256(bytes)!==row.sha256)throw Error('CUT_CORRECTION_HASH_MISMATCH: '+name);activeSources[name]=bytes.toString('utf8');}}


  for(const [name,pin] of Object.entries(PINS)) {
    const original=readFileSync(path.join(cut,'original',name));
    if(sha256(original)!==pin)throw new Error(`CUT_ORIGINAL_HASH_MISMATCH: ${name}`);
    const fixture=readFileSync(path.join(ROOT,'tests/fixtures/web-m97/lifecycle-source/assets/js/modules',name));
    if(!original.equals(fixture))throw new Error(`CUT_ORIGINAL_FIXTURE_MISMATCH: ${name}`);
    const {adapted,edits,dependencyPruning}=name==='editing-render-packet.js'
      ?extractPacketDependency(original.toString('utf8'),sources,parser):adapt(original.toString('utf8'),parser);
    outputs.set(path.join(cut,'adapted',name),Buffer.from(adapted));
    const sourcePath=`assets/js/modules/${name}`;
    const overlay=correction.changes.find(row=>row.path===sourcePath);
    let activeOriginal=original,activeOriginalPath='original/'+name;
    if(overlay){activeOriginal=readFileSync(path.join(correctionRoot,sourcePath));if(sha256(activeOriginal)!==overlay.sha256)throw Error('CUT_CORRECTION_HASH_MISMATCH: '+name);activeOriginalPath='corrected-original/'+name;outputs.set(path.join(cut,activeOriginalPath),activeOriginal);}
    const active=name==='editing-render-packet.js'?extractPacketDependency(activeOriginal.toString('utf8'),activeSources,parser):adapt(activeOriginal.toString('utf8'),parser);
    outputs.set(path.join(cut,'corrected-adapted',name),Buffer.from(active.adapted));
    modules.push({name,sourcePath,gitBlob:upstream.sources.find(row=>row.path===sourcePath).blob,
      originalSha256:pin,adaptedSha256:sha256(adapted),syntaxReplacements:edits,
      correctedOriginalPath:activeOriginalPath,correctedOriginalSha256:sha256(activeOriginal),correctedSha256:sha256(active.adapted),correctedSyntaxReplacements:active.edits,...(dependencyPruning?{dependencyPruning}:{})});
  }
  const manifest={webCommit:upstream.behavioralCommit,sourceManifest:'tests/fixtures/web-m97/lifecycle-manifest.json',
    sourceRoot:'tests/fixtures/web-m97/lifecycle-source',approvedCorrections:{manifest:'tests/fixtures/web-m973-split/corrections/dateline/manifest.json',baseBehavioralCommit:correction.baseBehavioralCommit,behavioralCommit:correction.behavioralCommit,tree:correction.tree},generator:'tools/m97/cut-kernel-generate.mjs',
    parser:{name:'acorn',version:parser.version,ecmaVersion:2022,sourceType:'module'},
    transformation:'Only object-spread expressions become ordered, nested own-data merges; an unused UI module is reduced to its exact imported prefix. No cut arithmetic or algorithm changes.',
    platform:{file:'assets/geometry/cut/platform.js',sha256:sha256(readFileSync(path.join(cut,'platform.js'))),role:'Array.prototype.at fallback only when missing'},
    dependencies:{d3:'assets/geometry/river/d3-provenance.json',polygonClipping:'assets/geometry/README.md',
      platform:'assets/geometry/river/platform.js',polygonGeometry:'Original pinned application module, evaluated unchanged.'},modules};
  outputs.set(path.join(cut,'provenance.json'),Buffer.from(JSON.stringify(manifest,null,2)+'\n'));
  const paths=[...outputs.keys()].map(file=>path.relative(path.join(ROOT,'assets/geometry/cut'),file));
  // Originals are retained and verified; the polygon normalizer is evaluated unchanged.
  paths.push(...Object.keys(PINS).map(name=>'original/'+name),'platform.js');
  const qrc='<RCC>\n  <qresource prefix="/cut">\n'+paths.sort().map(file=>`    <file alias="${file}">../assets/geometry/cut/${file}</file>`).join('\n')+'\n  </qresource>\n</RCC>\n';
  outputs.set(path.join(ROOT,'resources/m973_cut.qrc'),Buffer.from(qrc));
  return outputs;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
  const check=process.argv.includes('--check');
  for(const [file,bytes] of generate()){
    if(check){if(!readFileSync(file).equals(bytes))throw new Error(`CUT_GENERATED_DRIFT: ${file}`);}
    else {mkdirSync(path.dirname(file),{recursive:true});writeFileSync(file,bytes);}
  }
  console.log('Verified pinned cut dependency closure and deterministic syntax/dependency adapter.');
}
