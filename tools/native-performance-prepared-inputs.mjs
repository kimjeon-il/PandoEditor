// Explicit new measurement input revision; never overwrite an old fixture.
import {readFileSync,writeFileSync,mkdirSync,existsSync} from 'node:fs';
import {createHash} from 'node:crypto';
import path from 'node:path';
import assert from 'node:assert/strict';
const [original,output]=process.argv.slice(2);
assert.ok(original&&output,'original manifest and fresh output directory required');
assert.equal(existsSync(output),false,'preserve old evidence');
const bytes=readFileSync(original),manifest=JSON.parse(bytes);
assert.equal(manifest.schema,'pandoeditor-native-performance-fixtures');assert.equal(manifest.version,2);
const hash=b=>createHash('sha256').update(b).digest('hex');
const files=new Map(),sourceHashes=new Map(),sourceRoot=path.dirname(path.resolve(original));
for(const fixture of manifest.fixtures) {
  for(const [field,shaField] of [['projectPath','projectSha256'],['viewPath','viewSha256'],['inputContractPath','inputContractSha256']]) {
    const source=path.resolve(sourceRoot,fixture[field]),content=readFileSync(source);
    assert.equal(hash(content),fixture[shaField].toLowerCase());
    const name=path.basename(source);assert.ok(!sourceHashes.has(name)||sourceHashes.get(name)===hash(content));sourceHashes.set(name,hash(content));files.set(name,content);fixture[field]=name;
    if(field==='inputContractPath') {
      const contract=JSON.parse(content);assert.equal(contract.sequenceId,'m98-native-v2');
      assert.equal(contract.script.panCount,120);assert.equal(contract.script.zoomCount,90);assert.equal(contract.script.wheelAngleDelta,5);
      contract.sequenceId='m98-native-v3-prepared-drag';contract.previousInputSha256=hash(content);
      Object.assign(contract.script,{panPreparationOffset:[20,0],panSampleStart:1,demZoomOutInitialScaleMultiplier:2});
      const updated=Buffer.from(JSON.stringify(contract,null,2)+'\n');files.set(name,updated);fixture[shaField]=hash(updated);
    }
  }
  for(const asset of fixture.assets) {
    const content=readFileSync(path.resolve(sourceRoot,asset.path));assert.equal(hash(content),asset.sha256.toLowerCase());
    const name=path.basename(asset.path);assert.ok(!files.has(name)||hash(files.get(name))===hash(content));files.set(name,content);asset.path=name;
  }
}
manifest.previousManifestSha256=hash(bytes);
manifest.inputRevision='m98-native-v3-prepared-drag';
manifest.inputChange='Explicit unmeasured drag start is retained as raw setup; 120 measured moves start at sample1. DEM zoom-out has a recorded scale2 preparation. Original 15 scenarios/counts/wheel delta5, projects/assets and expected final contracts are preserved.';
mkdirSync(output,{recursive:true});for(const [name,content] of files)writeFileSync(path.join(output,name),content);
writeFileSync(path.join(output,'diagnostic-fixtures.json'),JSON.stringify(manifest,null,2)+'\n');
console.log(JSON.stringify({previousManifestSha256:hash(bytes),fixtureManifestSha256:hash(readFileSync(path.join(output,'diagnostic-fixtures.json'))),inputRevision:manifest.inputRevision,fixtureCount:manifest.fixtures.length}));
