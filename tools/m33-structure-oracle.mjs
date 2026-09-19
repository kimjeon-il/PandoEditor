// The oracle deliberately verifies source identity before it asks the native probe
// anything.  Subsequent M3.3 tasks extend the cases without replacing these bytes.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import { spawnSync } from 'node:child_process';
import { territorialDeletionAllowed } from '../tests/fixtures/web-structure/source/territorial-interaction-policy.js';

const root = new URL('../', import.meta.url);
const fixture = new URL('tests/fixtures/web-structure/', root);
const source = new URL('source/', fixture);
const manifest = JSON.parse(fs.readFileSync(new URL('manifest.json', fixture)));
for (const [name, expected] of Object.entries(manifest.blobs)) {
  const bytes = fs.readFileSync(new URL(name, source));
  const blob = Buffer.concat([Buffer.from(`blob ${bytes.length}\0`), bytes]);
  assert.equal(crypto.createHash('sha1').update(blob).digest('hex'), expected,
    `pinned source modified: ${name}`);
}

const unit = (id, parentId = '', locked = false) => ({ id, properties: { parentId, locked } });
const deleteCases = [
  { case: 'unlocked leaf', group: 'delete', kind: 'delete', targets: ['S'], units: [unit('S')] },
  { case: 'locked leaf', group: 'delete', kind: 'delete', targets: ['S'], units: [unit('S', '', true)] },
  { case: 'base child', group: 'delete', kind: 'delete', targets: ['P'], units: [unit('P'), unit('S', 'P')] },
  { case: 'multi atomicity', group: 'delete', kind: 'delete', targets: ['S', 'L'], units: [unit('S'), unit('L', '', true)] }
];
const groups = {
  parent: [{ case: 'country parent', group: 'parent' }, { case: 'nested subunit', group: 'parent' }, { case: 'self', group: 'parent' }, { case: 'descendant', group: 'parent' }, { case: 'other sovereign', group: 'parent' }],
  containment: [{ case: 'boundary-touch', group: 'containment' }, { case: 'outside sliver', group: 'containment' }, { case: 'parent hole', group: 'containment' }, { case: 'multipolygon island', group: 'containment' }],
  delete: deleteCases,
  create: [{ case: 'subunit defaults', group: 'create' }, { case: 'region empty relations', group: 'create' }, { case: 'duplicate ID', group: 'create' }],
  'convert-preview': [{ case: 'country-to-subunit', group: 'convert-preview' }, { case: 'subunit-to-country', group: 'convert-preview' }],
  'transfer-plan': [{ case: 'subunit transfer structural ownership impacts', group: 'transfer-plan' }],
  'reference-rewrite': [{ case: 'distribution dated generic owner and topology', group: 'reference-rewrite' }]
};
const requested = process.argv.includes('--group') ? process.argv[process.argv.indexOf('--group') + 1] : null;
const cases = requested ? (groups[requested] || []) : Object.values(groups).flat();
assert.ok(cases.length, `unknown or empty group: ${requested}`);
const probe = process.argv[2];
assert.ok(probe, 'usage: node tools/m33-structure-oracle.mjs <structure_probe> [--group name]');
const child = spawnSync(probe, [], { input: JSON.stringify(cases), encoding: 'utf8' });
assert.equal(child.status, 0, child.stderr || `probe exited ${child.status}`);
const actual = JSON.parse(child.stdout);
assert.equal(actual.length, cases.length);
const expected = cases.map(row => row.kind === 'delete'
  ? { case: row.case, supported: true, result: territorialDeletionAllowed(row.targets.map(id => row.units.find(unit => unit.id === id)), row.units) }
  : { case: row.case, supported: false });
assert.deepEqual(actual, expected, 'web/native structure differences');
console.log(`Pinned source ${manifest.commit}: ${Object.keys(manifest.blobs).length} blob hashes verified; ${cases.length} web/native structure cases matched.`);
