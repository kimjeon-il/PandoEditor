import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readFileSync} from 'node:fs';
const platform=()=>readFileSync(new URL('../../assets/geometry/cut/platform.js',import.meta.url),'utf8');
const cases=`(() => {
 const values=[],a=['first','middle','last'];
 for(const index of [undefined,NaN,0,-0,1,2,3,-1,-2,-3,-4,1.9,-1.9,Infinity,-Infinity,'1','-1'])values.push(a.at(index));
 values.push([].at(-1),Array.prototype.at.call({0:'a',1:'b',length:2},-1));
 values.push(Array.prototype.at.call({0:'a',length:NaN},0),Array.prototype.at.call({0:'a',length:-1},0));
 values.push(Array.prototype.at.call({0:'a',length:Infinity},0),Array.prototype.at.call('abc',-1));
 values.push(Object.getOwnPropertyDescriptor(Array.prototype,'at').enumerable);
 for(const receiver of [null,undefined]){try{Array.prototype.at.call(receiver,0);values.push(false);}catch(e){values.push(e instanceof TypeError);}}
 return JSON.stringify(values);
})()`;
test('missing Array.at uses standard relative indices and generic array-like lookup',()=>{
 const expected=vm.runInNewContext(cases);
 const context=vm.createContext({});vm.runInContext('delete Array.prototype.at;',context);
 vm.runInContext(platform(),context);assert.equal(vm.runInContext(cases,context),expected);
});
test('existing Array.at is not replaced',()=>{
 const context=vm.createContext({});vm.runInContext('globalThis.before=Array.prototype.at;',context);
 vm.runInContext(platform(),context);assert.equal(vm.runInContext('before===Array.prototype.at',context),true);
});
