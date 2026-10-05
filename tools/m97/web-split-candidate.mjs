/** Explicitly test-only candidate source loader. Never overwrites pinned fixtures. */
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {resolve,dirname} from 'node:path';
import {pathToFileURL} from 'node:url';
import {productionModules,verifyLifecycleSources} from './web-lifecycle.mjs';
import {loadSelectionModules} from './web-selection.mjs';
import {prepareRiverRemovalCorrectedSelectionSources} from './web-selection-removal-correction.mjs';
const sha256=bytes=>createHash('sha256').update(bytes).digest('hex');
const gitBlob=bytes=>createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`),bytes])).digest('hex');
export async function loadSplitCandidateModules({candidateRoot,candidateChanges}){
 assert.equal(typeof candidateRoot,'string','Explicit candidateRoot required');
 assert.ok(Array.isArray(candidateChanges)&&candidateChanges.length,'Explicit changed-path/SHA256 list required');
 const base=verifyLifecycleSources(),corrected=prepareRiverRemovalCorrectedSelectionSources();
 try{
  const supplementalPath='assets/js/modules/app-object-picking.js';
  const supplemental=readFileSync(new URL('../../tests/fixtures/web-m973-split/original/app-object-picking.js',import.meta.url));
  assert.equal(sha256(supplemental),'19e62d26000134b4c7ca695ed21e3a9bbaad176ed3b90b757f59d0b141c13e23');
  writeFileSync(resolve(corrected.root,supplementalPath),supplemental);
  const allowed=new Set();
  for(const change of candidateChanges){
   assert.match(change.path,/^assets\/js\/modules\/[a-z0-9-]+\.js$/,'Only explicitly named source modules accepted');
   assert.match(change.sha256,/^[a-f0-9]{64}$/,'Expected candidate SHA256 required');assert.ok(!allowed.has(change.path),'Duplicate candidate path');allowed.add(change.path);
   const bytes=readFileSync(resolve(candidateRoot,change.path));assert.equal(sha256(bytes),change.sha256,'Candidate bytes changed since review: '+change.path);
   if(change.blob)assert.equal(gitBlob(bytes),change.blob,'Candidate Git blob mismatch');
   const destination=resolve(corrected.root,change.path);mkdirSync(dirname(destination),{recursive:true});writeFileSync(destination,bytes);
  }
  // Every executable is accounted for in a bounded manifest. Only the explicit
  // delta list and existing approved overlays can differ from the original pin.
  const paths=[...new Set([...base.manifest.sources.map(row=>row.path),supplementalPath,...allowed])];
  const manifest={...base.manifest,sources:paths.map(path=>{const bytes=readFileSync(resolve(corrected.root,path));return {path,blob:gitBlob(bytes),sha256:sha256(bytes)};})};
  const loaded=await productionModules({root:corrected.root,manifest});
  const api={...loaded.api,...await loadSelectionModules(corrected.root),
   ...await import(pathToFileURL(resolve(corrected.root,'assets/js/modules/app-country-validation.js'))),
   ...await import(pathToFileURL(resolve(corrected.root,supplementalPath)))};
  assert.equal(typeof globalThis.polygonClipping?.difference,'function','Use ESM file or node --test, not CommonJS global exports.');
  return {...loaded,api,selectionRoot:corrected.root,sourceChain:corrected.sourceChain,cleanup:corrected.cleanup,
   testOnlyCandidate:true,candidateRoot,candidateChanges:structuredClone(candidateChanges),candidateManifest:manifest};
 }catch(error){corrected.cleanup();throw error;}
}
