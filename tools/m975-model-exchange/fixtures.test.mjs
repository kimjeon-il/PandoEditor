import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {sha256} from './contract.mjs';
const root=new URL('../../tests/fixtures/web-m975-model-exchange/',import.meta.url);
test('new exchange fixtures are exact-byte valid-v9 inputs with complete fixed inventory',async()=>{
 assert.ok(fs.existsSync(new URL('manifest.json',root)),'New valid-v9 exchange corpus is required');
 const manifest=JSON.parse(fs.readFileSync(new URL('manifest.json',root)));
 assert.equal(manifest.schema,'pando-m975-model-exchange-inputs');assert.equal(manifest.cases.length,10);
 const {loadExchangeSources}=await import('./sources.mjs');const loaded=await loadExchangeSources();
 try{for(const row of manifest.cases){const bytes=fs.readFileSync(new URL(row.file,root));assert.equal(bytes.length,row.bytes);assert.equal(sha256(bytes),row.sha256);const p=loaded.api.prepareProjectForStorage(JSON.parse(bytes));assert.equal(p.schemaVersion,9);if(row.operation==='storage')assert.throws(()=>loaded.api.prepareProjectForActivation(p));else loaded.api.prepareProjectForActivation(p);}}
 finally{loaded.cleanup();}
});
test('same-commit supplemental color provider and hydro constants have immutable source identity',async()=>{
 const sources=await import('./sources.mjs');assert.equal(typeof sources.readExchangeSources,'function','Approved M975 source completion reader required');
 const loaded=sources.readExchangeSources();assert.equal(Object.keys(loaded.sources).length,108);assert.equal(loaded.supplement.behavioralCommit,'ad78780f79f4f38fcd7c2a3c2fb1fe0ba8e37c32');assert.ok(loaded.evidenceSources['assets/js/modules/app-environment.js']);
});
test('v2 preserves all v1 inputs and four exact preimage failures, changing only approved unrelated inline coordinates',async()=>{
 const {gunzipSync}=await import('node:zlib'),manifest=JSON.parse(fs.readFileSync(new URL('manifest.json',root)));assert.equal(manifest.version,2,'explicit new input corpus version');const archive=manifest.previousInputs,compressed=fs.readFileSync(new URL(archive.file,root));assert.equal(sha256(compressed),archive.sha256);const decoded=gunzipSync(compressed);assert.equal(sha256(decoded),archive.decompressedSha256);const prior=JSON.parse(decoded),v1=JSON.parse(prior['manifest.json']);assert.equal(v1.version,1);assert.equal(sha256(JSON.stringify(v1)),'6e7401e03d8fda69c634bce19a35e412c6b829c14daeac0fc5a2bfd4f1e8d931');
 const changed=new Map([['m975-annex-partial-refs',10],['m975-split-child-descendants',30],['m975-boundary-root-descendants',2],['m975-boundary-child-fixed-parent',2]]);assert.deepEqual(manifest.coordinateRevision.cases,[...changed.keys()]);
 for(const row of manifest.cases){const original=v1.cases.find(c=>c.id===row.id),before=JSON.parse(prior[original.file]),after=JSON.parse(fs.readFileSync(new URL(row.file,root)));assert.equal(sha256(prior[original.file]),original.sha256);const expected=structuredClone(before);if(changed.has(row.id)){expected.labels[0].coordinates=[40,0];expected.genericFeatures[0].geometry.coordinates=[41,0];expected.hydroEdits[0].geometry.coordinates=[[45,-changed.get(row.id)],[46,0]];}assert.deepEqual(after,expected,'only explicitly approved inline coordinate replacement '+row.id);const {file,bytes,sha256:hash,...definition}=row,{file:oldFile,bytes:oldBytes,sha256:oldHash,...oldDefinition}=original;assert.deepEqual(definition,oldDefinition,'territorial/edit definitions remain exact');}
 const failures=JSON.parse(fs.readFileSync(new URL(manifest.coordinateRevision.failureFile,root)));assert.deepEqual(failures.cases.map(c=>c.id),[...changed.keys()]);for(const failure of failures.cases){assert.equal(sha256(failure.stderr),failure.sha256);assert.match(failure.stderr,/No exact public projection preimage within four ULPs/);}
});
