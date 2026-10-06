import assert from 'node:assert/strict';
import test from 'node:test';
import {readPendingSources,verifyPendingSources} from './sources.mjs';
test('new corpus preserves immutable existing closure and real tool configuration bytes',()=>{
 const bundle=readPendingSources();assert.equal(bundle.corpus.cases.length,14);assert.equal(bundle.corpus.cases.reduce((n,c)=>n+c.stages.length,0),86);
 assert.equal(bundle.addendum.supplementary.blob,'e841223c2c6ba6232476726039eed02f955a5f1c');
 assert.match(bundle.sources['assets/js/modules/tool-controller.js'],/export function toolDraftDefinition/);
 verifyPendingSources(bundle);
});
test('altered supplementary source, corpus and base manifest are rejected',()=>{
 for(const mutate of [b=>b.sources['assets/js/modules/tool-controller.js']+='\n',b=>b.corpus.cases.pop(),b=>b.manifest.behavioralCommit='0'.repeat(40),b=>b.addendum.supplementary.blob='0'.repeat(40)]){
  const b=structuredClone(readPendingSources());mutate(b);assert.throws(()=>verifyPendingSources(b));
 }
});
