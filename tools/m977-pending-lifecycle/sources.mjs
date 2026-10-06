import assert from 'node:assert/strict';
import fs from 'node:fs';
import {loadExchangeSources,readExchangeSources,exchangeEntrypoints} from '../m975-model-exchange/sources.mjs';
import {readPendingSources,pendingProjection,sha256} from '../m977-pending-input/sources.mjs';
export {sha256,pendingProjection};
export const corpusPin='e15f7b7c5db519d015e3c4f1da4000477622748ee33653aaf98d63816f0a3bfd';
export const entrypoints=Object.freeze([...exchangeEntrypoints,'tool-controller']);
// Test-input projection only. The immutable Web9 behavioral corpus and receipts
// remain Web9; this does not provide a production reader or migration path.
export function projectCurrentLifecycleInput(project){
 const current=structuredClone(project);assert.equal(current.schemaVersion,9);assert.equal(current.territorialModel.schemaVersion,5);
 current.schemaVersion=10;current.territorialModel.schemaVersion=6;
 for(const row of current.territorialEntities){const p=row.properties;assert.equal(p.schemaVersion,5);assert.ok(Object.hasOwn(p,'sourceLibraryId'));assert.equal(Object.hasOwn(p,'sourceEntityId'),false);p.schemaVersion=6;p.sourceEntityId=p.sourceLibraryId;delete p.sourceLibraryId;}
 return current;
}
function readCurrentInput(historicalCorpus,historicalRaw){
 const corpusPath='tools/m977-pending-lifecycle/corpus.web10.json',corpusRaw=fs.readFileSync(new URL('../../'+corpusPath,import.meta.url),'utf8');
 assert.equal(sha256(corpusRaw),'aacd2e653ea8ea37d4accc8f585644c08f7ce8e7fa26fb13f193cebca6767104','explicit current lifecycle corpus');
 const corpus=JSON.parse(corpusRaw),fixtureRaw=fs.readFileSync(new URL('../../'+corpus.fixture.path,import.meta.url),'utf8');
 assert.equal(Buffer.byteLength(fixtureRaw),corpus.fixture.bytes);assert.equal(sha256(fixtureRaw),corpus.fixture.sha256);
 const proofRaw=fs.readFileSync(new URL('../../tests/fixtures/lineage-v10-content/pending-lifecycle-input-provenance.json',import.meta.url),'utf8');
 assert.equal(sha256(proofRaw),'57ed8c4c4295f01c04ec8b9e7c40f89cc016318b5f2bf89aaf36e1b50634c86d','explicit input provenance');
 const provenance=JSON.parse(proofRaw);assert.equal(provenance.sha256,sha256(fixtureRaw));assert.equal(provenance.originalSha256,sha256(historicalRaw));
 assert.deepEqual({...corpus,fixture:historicalCorpus.fixture},historicalCorpus,'only the native input fixture binding changes');
 assert.deepEqual(JSON.parse(fixtureRaw),projectCurrentLifecycleInput(JSON.parse(historicalRaw)),'four explicit input boundaries; all content retained');
 return {corpusPath,corpus,corpusSha256:sha256(corpusRaw),fixtureRaw,provenance};
}
export function readLifecycleSources(){
 const exchange=readExchangeSources(),pending=readPendingSources(),corpusText=fs.readFileSync(new URL('corpus.json',import.meta.url),'utf8');
 assert.equal(sha256(corpusText),corpusPin,'immutable separately identified lifecycle corpus');
 const corpus=JSON.parse(corpusText);assert.equal(corpus.schema,'pando-m977-pending-lifecycle-corpus');assert.equal(corpus.version,2);const fixtureRaw=fs.readFileSync(new URL('../../'+corpus.fixture.path,import.meta.url),'utf8');
 assert.equal(Buffer.byteLength(fixtureRaw),corpus.fixture.bytes);assert.equal(sha256(fixtureRaw),corpus.fixture.sha256,'unchanged current-model reference fixture');
 const tool=pending.addendum.supplementary.path;
 for(const [path,source] of Object.entries(pending.sources))if(path!==tool)assert.equal(exchange.sources[path],source,'same approved per-gate production source '+path);
 return {...exchange,pendingAddendum:pending.addendum,corpus,corpusSha256:sha256(corpusText),fixtureRaw,currentInput:readCurrentInput(corpus,fixtureRaw),sources:{...exchange.sources,[tool]:pending.sources[tool]},entrypoints};
}
export function verifyLifecycleSources(bundle){assert.deepEqual(bundle,readLifecycleSources(),'exact source, fixture and corpus identities');return bundle;}
export async function loadLifecycleSources(){
 const bundle=readLifecycleSources(),loaded=await loadExchangeSources();
 const tool=await import(new URL('../../tests/fixtures/web-m977-pending-input/tool-controller.js',import.meta.url));
 return {...loaded,api:{...loaded.api,...tool},bundle};
}
