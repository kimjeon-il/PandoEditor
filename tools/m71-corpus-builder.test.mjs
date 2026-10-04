import assert from 'node:assert/strict';
import {mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {test} from 'node:test';
import {geometryStats, selectRiverFeature, selectLakeFeature} from './build-m71-world-corpus.mjs';

test('stats keep ring hierarchy and a raw dateline jump', () => {
  const stats = geometryStats({type: 'Polygon', coordinates: [
    [[179, 70], [-179, 70], [-179, 80], [179, 70]],
    [[179.5, 72], [-179.5, 72], [179.5, 72]],
  ]});
  assert.deepEqual([stats.polygonCount, stats.ringCount, stats.holeCount, stats.coordinateCount], [1, 2, 1, 7]);
  assert.equal(stats.maxLongitudeJump, 359);
  assert.deepEqual(stats.bounds, [ -179.5, 70, 179.5, 80 ]);
});

test('river selection uses position count then stable ID then source index', () => {
  const line = (id, n) => ({id, geometry: {type: 'LineString', coordinates: Array.from({length: n}, (_, i) => [i, i])}});
  const features = [line('z', 3), line('b', 4), line('a', 4), line('a', 4), {id: 'x', geometry: {type: 'Point', coordinates: [0, 0]}}];
  assert.equal(selectRiverFeature(features), features[2]);
});

test('lake selection orders hole count before position count', () => {
  const polygon = (id, rings) => ({id, geometry: {type: 'Polygon', coordinates: rings}});
  const manyPositions = polygon('large', [Array.from({length: 10}, (_, i) => [i, 0])]);
  const withHole = polygon('hole', [[[0, 0], [1, 0], [0, 0]], [[0, 0], [.5, 0], [0, 0]]]);
  assert.equal(selectLakeFeature([manyPositions, withHole]), withHole);
});

test('wrong source checkout or blob cannot change an existing output', () => {
  const root = mkdtempSync(join(tmpdir(), 'm71-bad-source-'));
  const out = mkdtempSync(join(tmpdir(), 'm71-output-'));
  try {
    mkdirSync(join(root, 'assets/data/hydro'), {recursive: true});
    writeFileSync(join(root, '.world-map-commit'), 'wrong\n');
    for (const name of ['countries-ne-5.1.1.geojson', 'hydro/rivers_base.geojson', 'hydro/lakes_base.geojson']) {
      writeFileSync(join(root, 'assets/data', name), '{}');
    }
    writeFileSync(join(out, 'sentinel'), 'untouched');
    for(const [commit, expected] of [['wrong', /source commit mismatch/],
      ['c0bd31d13dc8495593d78cf51f7cc195de7c9469', /source blob mismatch/]]) {
      writeFileSync(join(root, '.world-map-commit'), commit+'\n');
      const run = spawnSync(process.execPath, [fileURLToPath(new URL('./build-m71-world-corpus.mjs', import.meta.url)),
        '--world-map-root', root, '--out', out], {encoding: 'utf8'});
      assert.notEqual(run.status, 0);
      assert.match(run.stderr, expected);
      assert.equal(readFileSync(join(out, 'sentinel'), 'utf8'), 'untouched');
    }
  } finally { rmSync(root, {recursive: true, force: true}); rmSync(out, {recursive: true, force: true}); }
});
