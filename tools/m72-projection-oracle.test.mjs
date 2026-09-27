import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

test('flat world offsets follow pinned web oracle', () => {
  const executable = process.env.M72_PROJECTION_PROBE;
  assert.ok(executable, 'M72_PROJECTION_PROBE is required');
  const expected = JSON.parse(readFileSync(new URL('../tests/fixtures/web-m72/expected.json', import.meta.url)));
  const rows = execFileSync(executable, ['--flat-offsets'], { encoding: 'utf8' }).trim().split('\n');
  const radians = rows.map(row => row.split(',').map(degrees => Number(degrees) * Math.PI / 180));
  assert.equal(rows.length, 3);
  for (const [index, name] of ['center', 'rightVisible', 'noOverlapFallback'].entries()) {
    assert.equal(radians[index].length, expected.flatWorldOffsets[name].length, name);
    radians[index].forEach((value, position) =>
      assert.ok(Math.abs(value - expected.flatWorldOffsets[name][position]) < 1e-12, `${name}[${position}]`));
  }
});
