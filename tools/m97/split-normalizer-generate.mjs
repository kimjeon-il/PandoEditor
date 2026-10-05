/** Complete approved source resource; no syntax, arithmetic or export edits. */
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {resolve,dirname} from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
export const root=fileURLToPath(new URL('../../',import.meta.url));
export const asset=resolve(root,'assets/geometry/split-corrections');
export const sourceHash='a645827c46f7c62dbf929f850c67f350516669dfeafe5c637aec6c67efcfebf4';
export const manifestHash='d15bd80373af3ff98c3cc4fcec2f78b6268956c275e3318b08a5a71208c30096';
export const commit='07d3e2053c71573e11c5cf89151f5f6686038511';
export const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
export function verifyApproved(directory=asset) {
  const source=readFileSync(resolve(directory,'original/polygon-geometry.js'));
  const manifestBytes=readFileSync(resolve(directory,'approved-manifest.json'));
  assert.equal(hash(source),sourceHash,'SPLIT_NORMALIZER_SOURCE_HASH_MISMATCH');
  assert.equal(hash(manifestBytes),manifestHash,'SPLIT_NORMALIZER_MANIFEST_HASH_MISMATCH');
  const manifest=JSON.parse(manifestBytes);
  assert.equal(manifest.behavioralCommit,commit);assert.equal(manifest.changes.length,10);
  const fixture=resolve(root,'tests/fixtures/web-m973-split/corrections/dateline');
  assert.deepEqual(source,readFileSync(resolve(fixture,'assets/js/modules/polygon-geometry.js')));
  assert.deepEqual(manifestBytes,readFileSync(resolve(fixture,'manifest.json')));
  const entry=manifest.changes.find(row=>row.path==='assets/js/modules/polygon-geometry.js');
  assert.equal(entry.sha256,sourceHash);assert.equal(entry.bytes,source.length);
  assert.equal(createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${source.length}\0`),source])).digest('hex'),entry.blob);
  return {source,manifest,entry};
}
export function generate(directory=asset) {
  const {manifest,entry}=verifyApproved(directory);
  const provenance={schema:'pando-approved-split-normalizer',version:1,webCommit:commit,
    baseBehavioralCommit:manifest.baseBehavioralCommit,tree:manifest.tree,
    sourceManifest:'tests/fixtures/web-m973-split/corrections/dateline/manifest.json',sourceManifestSha256:manifestHash,
    source:{path:entry.path,gitBlob:entry.blob,sha256:sourceHash,bytes:entry.bytes,resource:'original/polygon-geometry.js'},
    generator:'tools/m97/split-normalizer-generate.mjs',
    transformation:'None. The complete approved application module, including every export, is evaluated byte-identically.',
    license:{application:'Shared-owner Pando application source. No separate upstream application license is asserted.',
      polygonClipping:'MIT; assets/geometry/LICENSE.polygon-clipping.txt'},
    dependencies:{polygonClipping:'Existing loadPinnedPolygonClipping, retaining its original hash and single reviewed Qt comma-return correction.'}};
  const qrc='<RCC>\n  <qresource prefix="/split-corrections">\n'+['approved-manifest.json','original/polygon-geometry.js','provenance.json'].map(file=>`    <file alias="${file}">../assets/geometry/split-corrections/${file}</file>`).join('\n')+'\n  </qresource>\n</RCC>\n';
  return new Map([[resolve(directory,'provenance.json'),Buffer.from(JSON.stringify(provenance,null,2)+'\n')],
    [resolve(root,'resources/m973_split_normalizer.qrc'),Buffer.from(qrc)]]);
}
if(process.argv[1]&&import.meta.url===pathToFileURL(resolve(process.argv[1])).href){
  for(const [file,bytes]of generate())if(process.argv.includes('--check'))assert.deepEqual(readFileSync(file),bytes,file);
  else{mkdirSync(dirname(file),{recursive:true});writeFileSync(file,bytes);}
  console.log('Verified byte-identical complete approved split normalizer.');
}
