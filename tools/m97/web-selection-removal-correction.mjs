import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {prepareCorrectedSelectionSources} from './web-selection-correction.mjs';

const fixtures=fileURLToPath(new URL('../../tests/fixtures/web-m97/',import.meta.url));
const gitBlob=bytes=>createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`),bytes])).digest('hex');
/** Apply only the published approved incremental bytes after verifying both historical pins. */
export function prepareRiverRemovalCorrectedSelectionSources({manifest=JSON.parse(readFileSync(resolve(fixtures,'selection-removal-correction-manifest.json'))),...baseOptions}={}){
  const base=prepareCorrectedSelectionSources(baseOptions);
  try{
    assert.equal(manifest.schema,'pando-web-selection-source-correction');assert.equal(manifest.version,1);assert.equal(manifest.repository,'kimjeon-il/Pando');
    assert.equal(manifest.baseBehavioralCommit,base.manifest.behavioralCommit);assert.equal(manifest.behavioralCommit,'6c3f930b8573fa09991885b661879ea36725472e');
    assert.equal(manifest.publishedTree,'a7ad5e8f79f0315228d5fe7614de3653666f88dc');assert.equal(manifest.sourceRoot,'selection-removal-correction-source');assert.equal(manifest.changes.length,1);
    const changed=manifest.changes[0];assert.equal(changed.path,'assets/js/modules/app-territory-selection-workflow.js');
    assert.equal(changed.baseBlob,'8f04dc31c77de0e7168074d92195040fee3d0f7a');assert.equal(changed.blob,'9cd369065747de407e4e83df3b6060383b3636a8','Approved removal source blob');
    assert.equal(changed.sha256,'626d2dd6c0a8263224272adacaa3303f41a28a83cf22214bdedfffd11a9bc44d','Approved removal source sha256');
    const target=resolve(base.root,changed.path);assert.equal(gitBlob(readFileSync(target)),changed.baseBlob,'Exact prior approved workflow bytes');
    const replacement=readFileSync(resolve(fixtures,manifest.sourceRoot,changed.path));assert.equal(gitBlob(replacement),changed.blob);assert.equal(createHash('sha256').update(replacement).digest('hex'),changed.sha256);
    writeFileSync(target,replacement);
    return {root:base.root,manifest,sourceChain:[base.manifest,manifest],cleanup:base.cleanup};
  }catch(error){base.cleanup();throw error;}
}
