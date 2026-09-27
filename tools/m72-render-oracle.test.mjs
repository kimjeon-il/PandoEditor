import assert from 'node:assert/strict';
import {cpSync, mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';
import {test} from 'node:test';

const oracle = await import('./m72-render-oracle.mjs').catch(() => null);
const fixture = resolve('tests/fixtures/web-m72');

test('pinned source verifier rejects a modified web render module', () => {
  assert.ok(oracle?.verifyPinnedSources, 'M7.2 source verifier is missing');
  const root = mkdtempSync(join(tmpdir(), 'm72-web-'));
  try {
    cpSync(join(fixture, 'source'), join(root, 'source'), {recursive: true});
    const file = join(root, 'source/render-scene.js');
    writeFileSync(file, readFileSync(file, 'utf8') + '\n// changed\n');
    assert.throws(() => oracle.verifyPinnedSources(root), /blob mismatch.*render-scene/i);
  } finally {rmSync(root, {recursive: true, force: true});}
});

test('web oracle pins flat world copies, packets, cache reuse and patch ordering', async () => {
  assert.ok(oracle?.generateExpected, 'M7.2 web oracle generator is missing');
  const value = await oracle.generateExpected(fixture);
  assert.equal(value.schema, 'pandoeditor-m72-web-render-oracle');
  assert.equal(value.worldMapCommit, 'c0bd31d13dc8495593d78cf51f7cc195de7c9469');
  assert.deepEqual(value.flatWorldOffsets.center, [0]);
  assert.deepEqual(value.flatWorldOffsets.rightVisible, [0, 2 * Math.PI]);
  assert.equal(value.packet.rectangle.vertexCount, 4);
  assert.equal(value.packet.rectangle.triangleCount, 2);
  assert.deepEqual(value.packet.datelineLongitudes, [179, 181, 181, 179]);
  assert.equal(value.cache.styleReusesGeometry, true);
  assert.equal(value.cache.selectionReusesGeometry, true);
  assert.equal(value.cache.viewReusesGeometry, true);
  assert.deepEqual(value.patch.orderedKeys, ['a', 'b']);
  assert.deepEqual(value.patch.afterRemoval, ['b']);
});

test('committed expected JSON exactly matches the pinned web oracle', async () => {
  assert.ok(oracle?.verifyExpected, 'M7.2 committed oracle verifier is missing');
  await oracle.verifyExpected(fixture);
});
