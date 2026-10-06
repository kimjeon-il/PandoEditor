import assert from 'node:assert/strict';
import fs from 'node:fs';
import {createHash} from 'node:crypto';
import {runInNewContext} from 'node:vm';
import {readSourceHistorySources,verifySourceHistorySources,loadSourceHistoryNodeSources} from '../m974-snap-boundary/source-history-sources.mjs';
import {sha256} from '../m974-snap-boundary/sources.mjs';
export {sha256};
const root=new URL('../../tests/fixtures/web-m977-pending-input/',import.meta.url);
const addendumPin='f15683f49c05ccdd0026fdd1bb0a0e555a9ea92faee98cd5e64c3fdf56bfe91b';
const blob=bytes=>createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`),bytes])).digest('hex');
export function verifyPendingSources(bundle){
 const {manifest,sources,addendum,corpus}=bundle;
 assert.equal(sha256(JSON.stringify(addendum)),addendumPin,'immutable pending-input addendum');
 const row=addendum.supplementary,baseSources={...sources};delete baseSources[row.path];
 verifySourceHistorySources({manifest,sources:baseSources});
 assert.equal(addendum.behavioralCommit,manifest.behavioralCommit);
 assert.equal(sha256(JSON.stringify(manifest)),addendum.baseSourceHistoryManifestSha256);
 const bytes=Buffer.from(sources[row.path]);assert.equal(bytes.length,row.bytes);assert.equal(sha256(bytes),row.sha256);assert.equal(blob(bytes),row.blob);
 assert.equal(sha256(JSON.stringify(corpus,null,2)+'\n'),addendum.corpusSha256,'immutable bounded corpus');
 return bundle;
}
export function readPendingSources(){
 const {manifest,sources}=readSourceHistorySources(),addendum=JSON.parse(fs.readFileSync(new URL('manifest.json',root))),corpus=JSON.parse(fs.readFileSync(new URL('corpus.json',root)));
 sources[addendum.supplementary.path]=fs.readFileSync(new URL(addendum.supplementary.file,root),'utf8');
 return verifyPendingSources({manifest,sources,addendum,corpus});
}
export async function loadPendingNodeSources(){
 const bundle=readPendingSources(),loaded=await loadSourceHistoryNodeSources();
 // Verify the original bytes before the file import; no generated tool behavior.
 const tool=await import(new URL(bundle.addendum.supplementary.file,root));
 return {...loaded,api:{...loaded.api,...tool},bundle};
}

// Validation uses the exact same immutable production D3 bytes, not a handwritten
// projection approximation. The VM has no Node/module/network capabilities.
const projections=new Map();
export function pendingProjection(bundle,definition){
 const path='assets/js/vendor/d3.min.js',source=bundle.sources[path],digest=sha256(source);
 assert.equal(digest,bundle.manifest.sources.find(row=>row.path===path)?.sha256,'verified actual D3 source');
 let d3=projections.get(digest);if(!d3){const context={};runInNewContext(source,context,{timeout:1000});d3=context.d3;projections.set(digest,d3);}
 const view=definition.view;assert.equal(view.kind,'flat','bounded corpus projection');
 return d3.geo.equirectangular().scale(view.scale).translate(view.translate).rotate(view.rotate).center(view.center);
}
