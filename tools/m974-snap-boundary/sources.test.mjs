import test from 'node:test';
import assert from 'node:assert/strict';
const module = await import('./sources.mjs').catch(error => { if (error.code !== 'ERR_MODULE_NOT_FOUND') throw error; return {}; });
test('source verifier rejects missing or changed pinned production bytes', async () => {
 assert.equal(typeof module.readPinnedSources, 'function', 'Verified source reader must exist');
 const bundle = module.readPinnedSources();
 assert.equal(bundle.manifest.behavioralCommit, 'ad78780f79f4f38fcd7c2a3c2fb1fe0ba8e37c32');
 assert.ok(bundle.sources['assets/js/modules/app-pointer-targets.js']);
 assert.ok(bundle.sources['assets/js/modules/app-domain-assembly.js']);
 assert.ok(bundle.sources['assets/js/modules/territorial-entity-store.js']);
 const changed = structuredClone(bundle); changed.sources['assets/js/modules/geometry-snap.js'] += '\n';
 assert.throws(() => module.verifyPinnedSources(changed), /hash|bytes|blob/);
 const missing = structuredClone(bundle); delete missing.sources['assets/js/modules/geometry-segment-index.js'];
 assert.throws(() => module.verifyPinnedSources(missing), /closure|missing|source/);
 const extra = structuredClone(bundle); extra.sources['assets/js/modules/rogue.js'] = 'export const rogue=true;';
 assert.throws(() => module.verifyPinnedSources(extra), /closure|source/);
 const wrong = structuredClone(bundle); wrong.manifest.sourceChain.at(-1).behavioralCommit = '0'.repeat(40);
 assert.throws(() => module.verifyPinnedSources(wrong), /chain|pin|manifest/);
});
test('transitive relative import closure includes original worker calculations and approved models', () => {
 assert.equal(typeof module.readPinnedSources, 'function', 'Verified source reader must exist');
 const {manifest,sources} = module.readPinnedSources();
 for (const name of ['app-cut-geometry','map-edit-geometry','territorial-edit-plan','geometry-preview','app-project-snapshots']) {
   assert.ok(sources[`assets/js/modules/${name}.js`]);
 }
 assert.ok(manifest.sources.length > 50);
 assert.ok(manifest.sources.every(row => /^[a-f0-9]{40}$/.test(row.blob) && /^[a-f0-9]{64}$/.test(row.sha256)));
 assert.ok(manifest.sources.filter(row=>row.blob!==row.baseBlob).length >= 10);
});
