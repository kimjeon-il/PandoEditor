import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync, mkdirSync, mkdtempSync, copyFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { verifyLifecycleSources } from './web-lifecycle.mjs';
import { loadSelectionModules,createSelectionRuntime,seedSelectionFeatures,settle,observeSelection } from './web-selection.mjs';
import { runRootSelectionLifecycleCorpus } from './web-selection-lifecycle.mjs';

const fixtures = fileURLToPath(new URL('../../tests/fixtures/web-m97/',import.meta.url));
const gitBlob = bytes => createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`),bytes])).digest('hex');

/** Exact-byte overlay only: the one approved correction stays separate from original behavioral-pin sources. */
export function prepareCorrectedSelectionSources({manifest=JSON.parse(readFileSync(resolve(fixtures,'selection-correction-manifest.json'))),...baseOptions}={}) {
  const original=verifyLifecycleSources(baseOptions);
  assert.equal(manifest.baseBehavioralCommit,original.manifest.behavioralCommit);
  assert.equal(manifest.behavioralCommit,'12cd8c8ec47c83cfb8c650e8f44c81cdfac10043');
  assert.equal(manifest.publishedTree,'2ec7e0badab8804de7a5fc457a4e5e6f8ba3732d');
  assert.equal(manifest.sourceRoot,'selection-correction-source');
  assert.equal(manifest.changes.length,1);
  const changed=manifest.changes[0];
  assert.equal(changed.path,'assets/js/modules/app-territory-selection-workflow.js');
  assert.equal(changed.blob,'8f04dc31c77de0e7168074d92195040fee3d0f7a','corrected production source hash is pinned');
  assert.equal(original.manifest.sources.find(row=>row.path===changed.path)?.blob,changed.baseBlob);
  const replacement=readFileSync(resolve(fixtures,manifest.sourceRoot,changed.path));
  assert.equal(gitBlob(replacement),changed.blob,'corrected production source hash');
  const root=mkdtempSync(resolve(tmpdir(),'pando-selection-correction-'));
  try {
    for(const source of original.manifest.sources) {
      const target=resolve(root,source.path);mkdirSync(dirname(target),{recursive:true});copyFileSync(resolve(original.root,source.path),target);
    }
    copyFileSync(resolve(original.root,'package.json'),resolve(root,'package.json'));
    writeFileSync(resolve(root,changed.path),replacement);
    return {root,manifest,cleanup:()=>rmSync(root,{recursive:true,force:true})};
  } catch(error) {rmSync(root,{recursive:true,force:true});throw error;}
}

async function fullSelection(api,method) {
  const h=createSelectionRuntime(api,{features:seedSelectionFeatures(api,{remote:false})});
  assert.ok(h.workflow.start('annex',{targetCountryId:'target',sourceCountryIds:['donor']}));
  assert.equal(await h.workflow.advance(),true);assert.equal(await h.workflow.selectMethod(method),true);
  h.setDraft(method==='polygon'?[[-1,-1],[11,-1],[11,11],[-1,11]]:[[-1,5],[11,5]]);
  assert.equal(h.workflow.finishDraft(),true);await settle(h);
  if(method==='line') {
    for(const candidate of h.workflow.activeSession().candidates.filter(candidate=>!h.workflow.activeSession().selectedCandidateIds.includes(candidate.id)))h.workflow.selectCandidate(candidate.id);
    await settle(h);
  }
  const stages={candidate:observeSelection(h)};
  assert.equal(stages.candidate.remainingGeometry,null);assert.equal(stages.candidate.canAddPart,true);
  assert.equal(h.workflow.addPart(),true);await settle(h);stages.archived=observeSelection(h);
  assert.equal(await h.workflow.advance(),true);stages.review=observeSelection(h);
  h.workflow.clear();stages.cancel=observeSelection(h);
  return {case:`corrected-full-donor-${method}`,stages,requests:h.requests,previews:h.previews};
}

export async function runCorrectedSelectionCorpus(options={}) {
  const corrected=prepareCorrectedSelectionSources(options);
  try {
    const api=await loadSelectionModules(corrected.root);
    const cases=[];
    for(const method of ['polygon','line'])cases.push(await fullSelection(api,method));
    const lifecycle=await runRootSelectionLifecycleCorpus({selectionRoot:corrected.root,caseNames:['root-full-polygon','root-full-line']});
    delete lifecycle.behavioralCommit;
    return {schema:'pando-web-corrected-selection-observations',version:1,baseBehavioralCommit:corrected.manifest.baseBehavioralCommit,behavioralCommit:corrected.manifest.behavioralCommit,sourceDelta:corrected.manifest.changes,cases,rootLifecycle:lifecycle};
  } finally {corrected.cleanup();}
}

if(process.argv[1]&&import.meta.url===pathToFileURL(resolve(process.argv[1])).href) {
  const observed=await runCorrectedSelectionCorpus();
  if(process.argv[2])writeFileSync(process.argv[2],JSON.stringify(observed,null,2)+'\n');
  else process.stdout.write(JSON.stringify(observed,null,2)+'\n');
}
