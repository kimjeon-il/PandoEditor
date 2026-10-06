import assert from 'node:assert/strict';
import fs from 'node:fs';
import {loadExchangeSources,readExchangeSources,exchangeEntrypoints} from '../m975-model-exchange/sources.mjs';
import {readPendingSources,pendingProjection,sha256} from '../m977-pending-input/sources.mjs';
export {sha256,pendingProjection};
export const corpusPin='e15f7b7c5db519d015e3c4f1da4000477622748ee33653aaf98d63816f0a3bfd';
export const entrypoints=Object.freeze([...exchangeEntrypoints,'tool-controller']);
export function readLifecycleSources(){
 const exchange=readExchangeSources(),pending=readPendingSources(),corpusText=fs.readFileSync(new URL('corpus.json',import.meta.url),'utf8');
 assert.equal(sha256(corpusText),corpusPin,'immutable separately identified lifecycle corpus');
 const corpus=JSON.parse(corpusText);assert.equal(corpus.schema,'pando-m977-pending-lifecycle-corpus');assert.equal(corpus.version,2);const fixtureRaw=fs.readFileSync(new URL('../../'+corpus.fixture.path,import.meta.url),'utf8');
 assert.equal(Buffer.byteLength(fixtureRaw),corpus.fixture.bytes);assert.equal(sha256(fixtureRaw),corpus.fixture.sha256,'unchanged current-model reference fixture');
 const tool=pending.addendum.supplementary.path;
 for(const [path,source] of Object.entries(pending.sources))if(path!==tool)assert.equal(exchange.sources[path],source,'same approved per-gate production source '+path);
 return {...exchange,pendingAddendum:pending.addendum,corpus,corpusSha256:sha256(corpusText),fixtureRaw,sources:{...exchange.sources,[tool]:pending.sources[tool]},entrypoints};
}
export function verifyLifecycleSources(bundle){assert.deepEqual(bundle,readLifecycleSources(),'exact source, fixture and corpus identities');return bundle;}
export async function loadLifecycleSources(){
 const bundle=readLifecycleSources(),loaded=await loadExchangeSources();
 const tool=await import(new URL('../../tests/fixtures/web-m977-pending-input/tool-controller.js',import.meta.url));
 return {...loaded,api:{...loaded.api,...tool},bundle};
}
