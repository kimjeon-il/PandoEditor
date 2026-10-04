#!/usr/bin/env node
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { timelineStorageCases } from '../tests/fixtures/timeline-storage-cases.mjs';

if (process.argv.length !== 4) throw new Error('Usage: node tools/timeline-storage-parity.mjs NATIVE_TEST_BINARY WEB_STORAGE_MODULE');
const binary = resolve(process.argv[2]);
const web = await import(pathToFileURL(resolve(process.argv[3])));
const cases = timelineStorageCases().filter(row => row.native !== false);
const output = execFileSync(binary, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'], maxBuffer: 4 * 1024 * 1024 });
const native = output.trim().split(/\r?\n/).map(line => JSON.parse(line));
assert.equal(native.length, cases.length);
let snapshots = 0;
for (const [index, row] of cases.entries()) {
  let verdict = 'OK', snapshot = null;
  try {
    const restored = web.restoreTimelineStorage(row.input, row.entities);
    snapshot = web.snapshotTimelineStorage(restored.records, restored.geometries, row.entities);
  } catch (error) { verdict = error.code || 'UNEXPECTED'; }
  assert.equal(verdict, row.expected, row.name);
  assert.equal(native[index].name, row.name);
  assert.equal(native[index].verdict, verdict, row.name);
  assert.deepEqual(native[index].snapshot, snapshot, `${row.name}: records, geometry IDs/versions, types or coordinates differ`);
  if (snapshot) snapshots += 1;
}
console.log(`${cases.length}/${cases.length} verdicts and ${snapshots}/${snapshots} complete storage snapshots match.`);
