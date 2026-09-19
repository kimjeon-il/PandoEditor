import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { createObjectSelectionController, objectRefKey } from '../tests/reference/object-selection-controller.mjs';

// Unmodified web source; this validates its Git blob identity, not a C++-generated golden.
const source=readFileSync(new URL('../tests/reference/object-selection-controller.mjs',import.meta.url));
const blob=createHash('sha1').update(`blob ${source.length}\0`).update(source).digest('hex');
assert.equal(blob,'3af8a3c9f7b85bf268aa8c9d4766547c3f8b373c');
if(process.argv[2]==='--verify') {
  console.log(`Pinned web selection reference verified: 58e4087 / ${blob}`);
  process.exit(0);
}
assert.ok(process.argv[2],'Usage: node tools/m3-selection-oracle.mjs <selection_probe>');
const ref=(domain,type,id)=>({domain,type,id});
const refs=[ref('territorial','country','A'),ref('territorial','country','B'),ref('territorial','subunit','S'),ref('territorial','region','지방:%/ 1'),ref('label','city','A'),ref('hydro','river','강'),ref('generic','feature','G')];
const scopes=['objects','search','other'];
const operations=[
 {op:'replace',ref:refs[0],scope:'objects'},
 {op:'toggle',ref:refs[1],scope:'objects'},
 {op:'toggle',ref:refs[2],scope:'objects'},
 {op:'toggle',ref:refs[2],scope:'objects'},
 {op:'range',ref:refs[3],ordered:refs,scope:'search'},
 {op:'range',ref:refs[0],ordered:refs,scope:'search'},
 {op:'range',ref:refs[2],ordered:refs,scope:'search'},
 {op:'range',ref:refs[4],ordered:refs.slice(0,3),scope:'objects'},
 {op:'setMany',refs:[refs[0],refs[1],refs[0]],scope:'objects'},
 {op:'prune',keep:[refs[1]]},
 {op:'range',ref:refs[3],ordered:refs,scope:'objects'},
 {op:'clear'}, {op:'toggle',ref:refs[0],scope:'search'},
 {op:'toggle',ref:refs[0],scope:'search'}, {op:'clear'},
 {op:'range',ref:refs[2],ordered:refs,scope:'search'},
 {op:'replace',ref:null}, {op:'toggle',ref:{}},
];
let seed=0x31415926;
const random=n=>{seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed%n;};
const kinds=['replace','toggle','range','setMany','remove','prune','clear'];
for(let i=0;i<2048;i++) {
 const op=kinds[random(kinds.length)], scope=scopes[random(scopes.length)];
 const picked=refs[random(refs.length)];
 const ordered=refs.filter(()=>random(3)!==0); if(random(2)) ordered.reverse();
 operations.push({op,ref:picked,scope,ordered,refs:[...ordered,...ordered.slice(0,2)],primary:refs[random(refs.length)],additive:random(2)===1,keep:ordered});
}
let revision=0;
const selection=createObjectSelectionController({onChange:()=>revision++});
const expected=[];
for(const step of operations) {
 switch(step.op) {
 case 'replace': selection.replace(step.ref,{scope:step.scope});break;
 case 'toggle': selection.toggle(step.ref,{scope:step.scope});break;
 case 'range': selection.selectRange(step.ref,step.ordered,{scope:step.scope,additive:step.additive});break;
 case 'setMany': selection.setMany(step.refs,{primary:step.primary,scope:step.scope});break;
 case 'remove': selection.remove(step.ref);break;
 case 'prune': {const keys=new Set(step.keep.map(objectRefKey));selection.prune(r=>keys.has(r.key));break;}
 case 'clear': selection.clear();break;
 default: throw new Error('Unknown oracle operation');
 }
 const snapshot=selection.snapshot();
 expected.push({keys:snapshot.keys,primaryKey:snapshot.primaryKey,revision,anchors:Object.fromEntries(scopes.map(s=>[s,selection.rangeAnchor(s)]))});
}
const child=spawnSync(process.argv[2],[],{input:JSON.stringify({refs,scopes,operations}),encoding:'utf8',maxBuffer:8*1024*1024});
assert.equal(child.status,0,child.stderr||child.error?.message);
const actual=JSON.parse(child.stdout);
assert.equal(actual.length,expected.length);
for(let i=0;i<expected.length;i++) assert.deepEqual(actual[i],expected[i],`Web/C++ mismatch at ${i}: ${JSON.stringify(operations[i])}`);
console.log(`PASS: ${expected.length} transitions match pinned web selection order, primary, scope anchors and notifications.`);
