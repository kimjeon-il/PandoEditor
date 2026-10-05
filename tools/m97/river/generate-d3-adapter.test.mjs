import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createRequire } from 'node:module';
import { test } from 'node:test';
import vm from 'node:vm';
import { generateHoistAdapter, loadPinnedAcorn } from './generate-d3-adapter.mjs';
const require = createRequire(import.meta.url);
const acorn = require(process.env.M972_D3_ACORN_PATH || './d3-adapter/node_modules/acorn');
const transform = source => generateHoistAdapter(source, acorn);
const observe = source => {
  const context = { observation: null }; vm.createContext(context); vm.runInContext(source, context);
  return JSON.parse(JSON.stringify(context.observation));
};
test('prepends only redundant var declarations inside each exact function scope', () => {
  const source = 'var root=1;(function(p){function nested(q){var inside=3;return q+inside;}var later=2;observation=[root,p,later,nested(4)];})(5);';
  const result = transform(source);
  assert.equal(result.functionScopeCount, 3);
  assert.equal(result.insertions.length, 3);
  assert.deepEqual(result.insertions.map(row => row.names), [['root'], ['later'], ['inside']]);
  assert.deepEqual(observe(result.code), observe(source));
  for(const row of result.insertions)assert.match(row.text, /^var [A-Za-z_$][\w$]*(?:,[A-Za-z_$][\w$]*)*;$/);
  let recovered = result.code;
  for(const row of [...result.insertions].reverse())recovered = recovered.slice(0,row.generatedOffset)+recovered.slice(row.generatedOffset+row.text.length);
  assert.equal(recovered, source);
});
test('preserves strict directive prologue and original initializer order', () => {
  const source = '(function(){"use strict";"other directive";var a=(observation=[typeof b],2),b=(observation.push(a),3);observation.push(this===undefined);})();';
  const result = transform(source);
  assert.match(result.code, /"use strict";"other directive";var a,b;var a=/);
  assert.deepEqual(observe(result.code), ['undefined',2,true]);
  assert.deepEqual(observe(result.code),observe(source));
});
test('does not shadow or add redundant declarations for parameters and function bindings', () => {
  const source = '(function(param){var param;function declared(){return 4;}var declared;var ordinary=2;observation=[param,declared(),ordinary];})(7);';
  const result = transform(source);
  assert.equal(result.insertions.length,1);
  assert.deepEqual(result.insertions[0].names,['ordinary']);
  assert.deepEqual(observe(result.code),observe(source));
});
test('preserves catch-local bindings and nested function boundaries', () => {
  const source = '(function(){var outside=1;try{throw 9;}catch(error){observation=[error,outside];var local=2;observation.push(local);(function(){var nested=3;observation.push(nested,error);})();}observation.push(typeof error,local);})();';
  const result = transform(source);
  assert.deepEqual(observe(result.code),observe(source));
  assert(!result.insertions.some(row=>row.names.includes('error')));
  assert.deepEqual(result.insertions.map(row=>row.names),[['outside','local'],['nested']]);
});
test('fails closed on direct eval, with, catch-var binding collision and unsupported syntax', () => {
  for(const source of [
    '(function(){eval("var late=2;");var late=1;})();',
    '(function(){with({x:1}){var y=x;}})();',
    '(function(){try{throw 1;}catch(e){var e=2;}})();',
    'let x=1;', '(function(){const x=1;})();', '(()=>1)();',
    '(function(){if(true){function f(){return 1;}}})();',
    'class Example {}', '({get value(){return 1;}});',
  ])assert.throws(()=>transform(source), /D3_ADAPTER_UNSUPPORTED|Unexpected|keyword|reserved/);
});
test('handles no-semicolon directive prologues without changing strictness', () => {
  const source = '(function(){"use strict"\nvar late=1;observation=[this===undefined,late];})();';
  const result = transform(source);
  assert.deepEqual(observe(result.code),[true,1]);
  assert.match(result.code,/"use strict"\nvar late;/);
});
test('generates deterministic insertion-only pinned D3 artifact', async () => {
  const source = await readFile(new URL('../../../assets/geometry/river/original/d3.min.js',import.meta.url),'utf8');
  const first=transform(source), second=transform(source);
  assert.equal(first.code,second.code);assert.deepEqual(first.insertions,second.insertions);
  assert(first.insertions.length>100);assert(first.functionScopeCount>300);
  let recovered=first.code;
  for(const row of [...first.insertions].reverse())recovered=recovered.slice(0,row.generatedOffset)+recovered.slice(row.generatedOffset+row.text.length);
  assert.equal(recovered,source);
});

test('fails closed when parser is absent or not the exact pinned version', () => {
  assert.throws(()=>loadPinnedAcorn('/tmp/pandoeditor-no-such-acorn-module'),/D3_ADAPTER_PARSER_UNAVAILABLE/);
  assert.throws(()=>generateHoistAdapter('var x=1;',{...acorn,version:'8.14.0'}),/D3_ADAPTER_PARSER_VERSION/);
  assert.throws(()=>generateHoistAdapter('var x=1;',null),/D3_ADAPTER_PARSER_VERSION/);
});
test('checked-in adapter and provenance exactly match deterministic regeneration', async () => {
  const original=await readFile(new URL('../../../assets/geometry/river/original/d3.min.js',import.meta.url),'utf8');
  const adapted=await readFile(new URL('../../../assets/geometry/river/adapted/d3.min.js',import.meta.url),'utf8');
  const provenance=JSON.parse(await readFile(new URL('../../../assets/geometry/river/d3-provenance.json',import.meta.url),'utf8'));
  const generated=transform(original);
  assert.equal(generated.code,adapted);
  assert.deepEqual(generated.insertions,provenance.adapter.insertions);
  assert.equal(generated.functionScopeCount,provenance.adapter.functionScopeCount);
  assert.equal(generated.insertedBytes,provenance.adapter.insertedBytes);
});
