import { createHash } from 'node:crypto';
import { readFile, writeFile } from 'node:fs/promises';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

export const D3_ORIGINAL_SHA256 = '4cdf92091ed0cfdd8b862af1c6d4744bd0458e746b92c1bbd5403a1143ecd538';
export const D3_ORIGINAL_GIT_BLOB = '6733ae4c8bfa093fdd9a4630f986018e0a893a01';
export const D3_WEB_COMMIT = '53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47';
export const ACORN_VERSION = '8.15.0';
const sha256 = source => createHash('sha256').update(source).digest('hex');
const supported = new Set([
  'Program','Identifier','Literal','ExpressionStatement','BlockStatement','EmptyStatement',
  'DebuggerStatement','WithStatement','ReturnStatement','LabeledStatement','BreakStatement',
  'ContinueStatement','IfStatement','SwitchStatement','SwitchCase','ThrowStatement',
  'TryStatement','CatchClause','WhileStatement','DoWhileStatement','ForStatement','ForInStatement',
  'FunctionDeclaration','FunctionExpression','VariableDeclaration','VariableDeclarator',
  'ThisExpression','ArrayExpression','ObjectExpression','Property','SequenceExpression',
  'UnaryExpression','BinaryExpression','AssignmentExpression','UpdateExpression',
  'LogicalExpression','ConditionalExpression','NewExpression','CallExpression','MemberExpression',
]);
const unsupported = detail => {throw new Error(`D3_ADAPTER_UNSUPPORTED: ${detail}`);};
const identifier = node => {
  if(node?.type!=='Identifier'||!/^[A-Za-z_$][\w$]*$/.test(node.name))unsupported('non-ASCII or non-Identifier binding');
  return node.name;
};
const children = node => Object.entries(node).filter(([key])=>!['type','start','end','loc','range'].includes(key));

export function loadPinnedAcorn(modulePath) {
  const require=createRequire(import.meta.url);
  let parser;
  try {parser=require(modulePath||'./d3-adapter/node_modules/acorn');}
  catch(error) {throw new Error('D3_ADAPTER_PARSER_UNAVAILABLE: run npm ci --ignore-scripts in tools/m97/river/d3-adapter', {cause:error});}
  if(parser.version!==ACORN_VERSION)throw new Error(`D3_ADAPTER_PARSER_VERSION: expected ${ACORN_VERSION}, got ${parser.version}`);
  return parser;
}

// ES5 var bindings are function-scoped even inside ordinary blocks and catches.
// Catch parameters are separate bindings: never hoist them, and fail closed on
// catch/var same-name collisions. Entering a function resets the catch stack.
// No initializer, existing token, function, parameter or directive is moved.
export function generateHoistAdapter(source,parser) {
  if(typeof source!=='string')throw new TypeError('D3_ADAPTER_SOURCE_REQUIRED');
  if(parser?.version!==ACORN_VERSION||typeof parser.parse!=='function')throw new Error('D3_ADAPTER_PARSER_VERSION');
  const ast=parser.parse(source,{ecmaVersion:5,sourceType:'script',allowReserved:false});
  const scopes=[],catchScopes=[];
  function scopeFor(node) {
    const body=node.type==='Program'?node:node.body;
    if(body.type!=='Program'&&body.type!=='BlockStatement')unsupported('non-block function body');
    const scope={node,body,vars:new Set(),excluded:new Set()};
    if(node.type!=='Program') {
      if(node.async||node.generator)unsupported('async or generator function');
      for(const param of node.params)scope.excluded.add(identifier(param));
      if(node.type==='FunctionExpression'&&node.id)scope.excluded.add(identifier(node.id));
    }
    scopes.push(scope);return scope;
  }
  function walk(node,scope,catches,parent) {
    if(!node||typeof node!=='object'||typeof node.type!=='string')return;
    if(!supported.has(node.type))unsupported(node.type);
    if(node.type==='WithStatement')unsupported('with statement');
    if(node.type==='CallExpression'&&node.callee.type==='Identifier'&&node.callee.name==='eval')unsupported('direct eval');
    if(node.type==='Property'&&node.kind!=='init')unsupported('accessor property');
    if(node.type==='FunctionDeclaration'||node.type==='FunctionExpression') {
      if(node.type==='FunctionDeclaration') {
        if(parent!==scope.body)unsupported('block/catch function declaration');
        scope.excluded.add(identifier(node.id));
      }
      const inner=scopeFor(node);
      for(const statement of inner.body.body)walk(statement,inner,[],inner.body);
      return;
    }
    if(node.type==='CatchClause') {
      const name=identifier(node.param);catchScopes.push({start:node.start,end:node.end,name});
      walk(node.body,scope,[...catches,name],node);return;
    }
    if(node.type==='VariableDeclaration') {
      if(node.kind!=='var')unsupported('lexical declaration');
      for(const declaration of node.declarations) {
        const name=identifier(declaration.id);
        if(catches.includes(name))unsupported(`catch/var binding collision: ${name}`);
        scope.vars.add(name);
      }
    }
    for(const [,value] of children(node)) {
      if(Array.isArray(value))for(const child of value)walk(child,scope,catches,node);
      else walk(value,scope,catches,node);
    }
  }
  const root=scopeFor(ast);for(const statement of ast.body)walk(statement,root,[],ast);
  const insertions=[];
  for(const scope of scopes) {
    const names=[...scope.vars].filter(name=>!scope.excluded.has(name));
    if(!names.length)continue;
    const statements=scope.body.body;let directiveCount=0;
    while(directiveCount<statements.length&&typeof statements[directiveCount].directive==='string')++directiveCount;
    // Inserting at the following statement keeps the original newline/comment
    // boundary for ASI-terminated directives, so "use strict" stays a directive.
    const offset=directiveCount
      ? (statements[directiveCount]?.start??scope.body.end-(scope.node.type==='Program'?0:1))
      : (scope.node.type==='Program'?0:scope.body.start+1);
    insertions.push({offset,scopeStart:scope.node.start,scopeEnd:scope.node.end,scopeType:scope.node.type,names,text:`var ${names.join(',')};`});
  }
  insertions.sort((a,b)=>a.offset-b.offset);
  let code='',cursor=0,added=0;
  for(const insertion of insertions) {
    if(insertion.offset<cursor)throw new Error('D3_ADAPTER_INSERTION_ORDER');
    code+=source.slice(cursor,insertion.offset)+insertion.text;
    insertion.generatedOffset=insertion.offset+added;added+=insertion.text.length;cursor=insertion.offset;
  }
  code+=source.slice(cursor);
  let recovered=code;
  for(const row of [...insertions].reverse()) {
    if(recovered.slice(row.generatedOffset,row.generatedOffset+row.text.length)!==row.text)throw new Error('D3_ADAPTER_INSERTION_VERIFICATION');
    recovered=recovered.slice(0,row.generatedOffset)+recovered.slice(row.generatedOffset+row.text.length);
  }
  if(recovered!==source)throw new Error('D3_ADAPTER_ORIGINAL_CHANGED');
  const generatedAst=parser.parse(code,{ecmaVersion:5,sourceType:'script',allowReserved:false});
  const insertedByStart=new Map(insertions.map(row=>[row.generatedOffset,row]));
  const removed=Symbol('inserted redundant var');let verifiedDeclarations=0;
  function semanticShape(value,adapted) {
    if(Array.isArray(value))return value.map(row=>semanticShape(row,adapted)).filter(row=>row!==removed);
    if(!value||typeof value!=='object')return value;
    const insertion=adapted&&value.type==='VariableDeclaration'&&insertedByStart.get(value.start);
    if(insertion) {
      if(value.kind!=='var'||value.declarations.some(row=>row.init!==null)||
         JSON.stringify(value.declarations.map(row=>identifier(row.id)))!==JSON.stringify(insertion.names))
        throw new Error('D3_ADAPTER_INVALID_INSERTED_DECLARATION');
      ++verifiedDeclarations;return removed;
    }
    return Object.fromEntries(Object.entries(value).filter(([key])=>!['start','end','loc','range'].includes(key))
      .map(([key,row])=>[key,semanticShape(row,adapted)]));
  }
  if(JSON.stringify(semanticShape(ast,false))!==JSON.stringify(semanticShape(generatedAst,true))||
     verifiedDeclarations!==insertions.length)throw new Error('D3_ADAPTER_NONDECLARATION_AST_DIFF');
  return {code,insertions,functionScopeCount:scopes.length,catchScopes,insertedBytes:added};
}

async function main(args) {
  const check=args.includes('--check');const parserArg=args.indexOf('--acorn');
  if(args.some((arg,index)=>arg!=='--check'&&arg!=='--acorn'&&!(parserArg>=0&&index===parserArg+1)))throw new Error('Usage: generate-d3-adapter.mjs [--check] [--acorn absolute-module-path]');
  const parser=loadPinnedAcorn(parserArg<0?undefined:args[parserArg+1]);
  const root=fileURLToPath(new URL('../../../',import.meta.url));
  const originalPath=path.join(root,'assets/geometry/river/original/d3.min.js');
  const adaptedPath=path.join(root,'assets/geometry/river/adapted/d3.min.js');
  const provenancePath=path.join(root,'assets/geometry/river/d3-provenance.json');
  const bytes=await readFile(originalPath);const source=bytes.toString('utf8');
  if(!Buffer.from(source,'utf8').equals(bytes)||bytes.length!==151144||sha256(bytes)!==D3_ORIGINAL_SHA256)throw new Error('D3_ADAPTER_ORIGINAL_HASH_MISMATCH');
  const blob=createHash('sha1').update(`blob ${bytes.length}\0`).update(bytes).digest('hex');
  if(blob!==D3_ORIGINAL_GIT_BLOB)throw new Error('D3_ADAPTER_ORIGINAL_BLOB_MISMATCH');
  for(const filename of ['lifecycle-manifest.json','selection-manifest.json']) {
    const manifest=JSON.parse(await readFile(path.join(root,'tests/fixtures/web-m97',filename),'utf8'));
    const row=manifest.sources.find(row=>row.path==='assets/js/vendor/d3.min.js');
    if(manifest.behavioralCommit!==D3_WEB_COMMIT||row?.blob!==blob)throw new Error('D3_ADAPTER_MANIFEST_MISMATCH');
  }
  const generated=generateHoistAdapter(source,parser);
  const originalProvenance=JSON.parse(await readFile(provenancePath,'utf8'));
  if(originalProvenance.sha256!==D3_ORIGINAL_SHA256||originalProvenance.webCommit!==D3_WEB_COMMIT)throw new Error('D3_ADAPTER_PROVENANCE_MISMATCH');
  const adapter={
    file:'assets/geometry/river/adapted/d3.min.js',sha256:sha256(generated.code),bytes:Buffer.byteLength(generated.code),
    generator:'tools/m97/river/generate-d3-adapter.mjs',parser:{name:'acorn',version:ACORN_VERSION,ecmaVersion:5,sourceType:'script'},
    transformation:'Only redundant function-scope var declarations without initializers are inserted after directive prologues; all original source bytes retain exact order.',
    functionScopeCount:generated.functionScopeCount,modifiedScopeCount:generated.insertions.length,
    insertedBytes:generated.insertedBytes,insertedVarCount:generated.insertions.reduce((sum,row)=>sum+row.names.length,0),
    proof:'Removing recorded insertion ranges recovers byte-identical original; removing only generated no-initializer var AST nodes recovers the identical original AST excluding positions.',
    catchScopes:generated.catchScopes,insertionsSha256:sha256(JSON.stringify(generated.insertions)),
    insertions:generated.insertions,
  };
  if(check) {
    if((await readFile(adaptedPath,'utf8'))!==generated.code)throw new Error('D3_ADAPTER_REGENERATION_DIFF');
    if(JSON.stringify(originalProvenance.adapter)!==JSON.stringify(adapter))throw new Error('D3_ADAPTER_PROVENANCE_REGENERATION_DIFF');
  }else {
    await writeFile(adaptedPath,generated.code);
    originalProvenance.adapter=adapter;
    await writeFile(provenancePath,JSON.stringify(originalProvenance,null,2)+'\n');
  }
  console.log(JSON.stringify({checked:check,originalSha256:D3_ORIGINAL_SHA256,generatedSha256:adapter.sha256,bytes:adapter.bytes,functionScopeCount:adapter.functionScopeCount,modifiedScopeCount:adapter.modifiedScopeCount,insertedVarCount:adapter.insertedVarCount,insertedBytes:adapter.insertedBytes,catchScopes:adapter.catchScopes}));
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
  main(process.argv.slice(2)).catch(error=>{console.error(error.message);process.exitCode=1;});
}
