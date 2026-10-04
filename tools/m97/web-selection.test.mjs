import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync,writeFileSync,mkdtempSync,cpSync,rmSync,unlinkSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { resolve } from 'node:path';
import { runSelectionCorpus,defaultSelectionSourceRoot } from './web-selection.mjs';
import { runRootSelectionLifecycleCorpus,readRootSelectionLifecycleObservations } from './web-selection-lifecycle.mjs';
const root=process.env.WEB_SELECTION_ROOT||defaultSelectionSourceRoot;
const fixtures=process.env.WEB_SELECTION_FIXTURES||new URL('../../tests/fixtures/web-m97/',import.meta.url);
const read=name=>JSON.parse(readFileSync(typeof fixtures==='string'?resolve(fixtures,name):new URL(name,fixtures)));
test('exact pinned selection import closure matches Git source blobs',()=>{
 const manifest=read('selection-manifest.json');assert.equal(manifest.behavioralCommit,'53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47');
 for(const source of manifest.sources){const bytes=readFileSync(resolve(root,source.path));const blob=createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`),bytes])).digest('hex');assert.equal(blob,source.blob,source.path);}
});
test('actual production selection session replay equals captured observations',async()=>{
 const observed=await runSelectionCorpus({root});assert.deepEqual(observed,read('session-observations.json'));
 const full=observed.cases.find(c=>c.case==='full-donor-polygon-candidate-blocked');assert.equal(full.stages.fullCandidate.canAddPart,false);assert.equal(full.stages.fullCandidate.remainingGeometry,null);
 const line=observed.cases.find(c=>c.case==='line-two-crossing-candidate-toggle-order');assert.deepEqual(line.stages.initial.candidates.map(c=>c.area),[50,50]);assert.deepEqual(line.stages.initial.selectedCandidateIds,[line.stages.initial.candidates[0].id]);assert.deepEqual(line.stages.reselectedOrder.selectedCandidateIds,line.stages.initial.candidates.map(c=>c.id).reverse());
});
test('actual production root annex preview, cancel, apply, rollback and history replay equals captured observations',async()=>{
 const observed=await runRootSelectionLifecycleCorpus({selectionRoot:root});assert.deepEqual(observed,readRootSelectionLifecycleObservations(typeof fixtures==='string'?resolve(fixtures,'root-lifecycle-observations.json'):new URL('root-lifecycle-observations.json',fixtures)));
 for(const c of observed.cases){assert.deepEqual(c.stages.preview.document.document,c.stages.before.document.document,c.case+' preview mutates document');assert.deepEqual(c.stages.cancel.document.document,c.stages.before.document.document,c.case+' cancel mutates document');
  if(c.stages.confirm.outcome.ok){assert.deepEqual(c.stages.undo.document.document,c.stages.before.document.document,c.case+' undo');assert.deepEqual(c.stages.redo.document.document,c.stages.confirm.document.document,c.case+' redo');}
  else {assert.equal(c.stages.confirm.document.history.undo,0);assert.deepEqual(c.stages.confirm.document.document,c.stages.before.document.document);assert.ok(c.stages.confirm.errors.length);}
 }
 const remote=observed.cases.find(c=>c.case==='root-selected-remote-donor');assert.deepEqual(remote.previews.at(-1).payload.donorIds,['remote-donor','donor']);assert.deepEqual(remote.stages.preview.document.preview.affectedIds,['target','donor']);
});

test('published full-donor correction preserves the original pin and executes real polygon/line session and root lifecycle',async()=>{
 const { runCorrectedSelectionCorpus }=await import('./web-selection-correction.mjs');
 const actual=await runCorrectedSelectionCorpus();assert.deepEqual(actual,read('selection-correction-observations.json'));
 assert.equal(actual.behavioralCommit,'12cd8c8ec47c83cfb8c650e8f44c81cdfac10043');
 assert.equal(actual.sourceDelta.length,1);
 assert.equal(actual.sourceDelta[0].baseBlob,'e99033f1402a6c1e55cfe2a32e8398026aff86fc');
 for(const c of actual.cases){assert.equal(c.stages.candidate.remainingGeometry,null);assert.equal(c.stages.candidate.canAddPart,true);assert.equal(c.stages.review.stage,'review');assert.equal(c.stages.cancel,null);}
 for(const c of actual.rootLifecycle.cases){assert.equal(c.stages.confirm.outcome.ok,true);assert.deepEqual(c.referenceEffects.deletedIds,['donor']);assert.equal(c.stages.confirm.document.history.undo,1);assert.deepEqual(c.stages.cancel.document.document,c.stages.before.document.document);assert.deepEqual(c.stages.undo.document.document,c.stages.before.document.document);assert.deepEqual(c.stages.redo.document.document,c.stages.confirm.document.document);}
 const original=read('session-observations.json').cases.find(c=>c.case==='full-donor-polygon-candidate-blocked');assert.equal(original.stages.fullCandidate.canAddPart,false);
});

test('corrected source delta rejects a forged source blob before import',async()=>{
 const { prepareCorrectedSelectionSources }=await import('./web-selection-correction.mjs');
 const manifest=read('selection-correction-manifest.json');manifest.changes[0].blob='0'.repeat(40);
 assert.throws(()=>prepareCorrectedSelectionSources({manifest}),/corrected production source hash/);
});


test('split root corpus rejects missing, duplicate, extra and mismatched case identities',()=>{
 const source=typeof fixtures==='string'?fixtures:fileURLToPath(fixtures);
 const check=(mutate,pattern)=>{
  const temporary=mkdtempSync(resolve(tmpdir(),'pando-root-case-index-'));
  const indexPath=resolve(temporary,'root-lifecycle-observations.json');
  try {
   cpSync(resolve(source,'root-lifecycle-observations.json'),indexPath);
   cpSync(resolve(source,'root-lifecycle-observations'),resolve(temporary,'root-lifecycle-observations'),{recursive:true});
   const index=JSON.parse(readFileSync(indexPath));mutate({temporary,index,indexPath});
   assert.throws(()=>readRootSelectionLifecycleObservations(indexPath),pattern);
  } finally {rmSync(temporary,{recursive:true,force:true});}
 };
 const save=({index,indexPath})=>writeFileSync(indexPath,JSON.stringify(index));
 check(context=>{context.index.caseFiles.pop();save(context);},/case IDs/);
 check(context=>{context.index.caseFiles[1]=context.index.caseFiles[0];save(context);},/case IDs/);
 check(context=>{context.index.caseFiles.push({case:'extra',path:'root-lifecycle-observations/extra.json'});save(context);},/case IDs/);
 check(context=>{context.index.caseFiles[0].path='../outside.json';save(context);},/case path/);
 check(({temporary})=>unlinkSync(resolve(temporary,'root-lifecycle-observations/root-partial.json')),/case files/);
 check(({temporary})=>writeFileSync(resolve(temporary,'root-lifecycle-observations/extra.json'),'{}'),/case files/);
 check(({temporary})=>{const file=resolve(temporary,'root-lifecycle-observations/root-partial.json');const row=JSON.parse(readFileSync(file));row.case='root-full';writeFileSync(file,JSON.stringify(row));},/case file identity/);
});
