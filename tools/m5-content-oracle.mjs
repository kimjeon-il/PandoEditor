import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import assert from 'node:assert/strict';
import { dominantDistributionEntries } from '../tests/fixtures/web-content/source/distribution-model.js';

const hashes = {
  'distribution-model.js': '7fa381ac8ddf67daeb66df027017d83a98fcf845',
  'temporal.js': '31faf94894e302b3eadf61d79a32efd0490affe3',
};
for (const [name, expected] of Object.entries(hashes)) {
  const bytes = readFileSync(new URL(`../tests/fixtures/web-content/source/${name}`, import.meta.url));
  const hash = createHash('sha1').update(`blob ${bytes.length}\0`).update(bytes).digest('hex');
  assert.equal(hash, expected, `Pinned web blob changed: ${name}`);
}
const probe = process.argv[2];
assert.ok(probe, 'Usage: node tools/m5-content-oracle.mjs <content_probe.exe>');
const entry = (id, layerId, territory, share, mode = 'territorial') =>
  ({ id, layerId, territorialUnitId: territory, share, mode });
const cases = [
  { name: 'independent shares', layers: [{ id: 'L' }], entries: [entry('a', 'L', 'A', 60), entry('b', 'L', 'A', 70)] },
  { name: 'tie retains input order', layers: [{ id: 'L' }], entries: [entry('z', 'L', 'A', 60), entry('a', 'L', 'A', 60)] },
  { name: 'hidden layer and geometry tail', layers: [{ id: 'L' }], entries: [entry('g', 'L', '', 20, 'geometry'), entry('hidden', 'H', 'A', 100), entry('b', 'L', 'B', 10), entry('a', 'L', 'A', 30)] },
];
for (const test of cases) {
  const expected = dominantDistributionEntries(test.layers, test.entries).map(row => row.id);
  const result = spawnSync(probe, [], { input: JSON.stringify(test), encoding: 'utf8' });
  assert.equal(result.status, 0, result.error?.message ?? result.stderr);
  assert.deepEqual(JSON.parse(result.stdout), expected, test.name);
  console.log(`PASS ${test.name}`);
}
