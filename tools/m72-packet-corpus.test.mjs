import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

const fixture = new URL('../tests/fixtures/world-rendering/', import.meta.url);
function features(name) {
  return JSON.parse(readFileSync(new URL(name, fixture))).features;
}
function encode(feature) {
  const geometry = feature.geometry;
  let parts;
  if (geometry.type === 'Polygon') parts = [geometry.coordinates];
  else if (geometry.type === 'MultiPolygon') parts = geometry.coordinates;
  else if (geometry.type === 'LineString') parts = [geometry.coordinates];
  else if (geometry.type === 'MultiLineString') parts = geometry.coordinates;
  else throw new Error(`unsupported probe type: ${geometry.type}`);
  const id = String(feature.id);
  const polygon = geometry.type === 'Polygon' || geometry.type === 'MultiPolygon';
  const words = [id, geometry.type, parts.length];
  for (const part of parts) {
    if (polygon) {
      words.push(part.length);
      for (const ring of part) {
        words.push(ring.length);
        for (const [lon, lat] of ring) words.push(lon, lat);
      }
    } else {
      words.push(part.length);
      for (const [lon, lat] of part) words.push(lon, lat);
    }
  }
  return words.join(' ');
}
test('all M7.1 real country, hydro and synthetic geometries prepare typed packets', () => {
  const executable = process.env.M72_PACKET_PROBE;
  assert.ok(executable, 'M72_PACKET_PROBE is required');
  const selected = [...features('countries.geojson'), ...features('hydro-river.geojson'),
    ...features('hydro-lake.geojson'), ...features('sentinels.geojson')];
  const input = `${selected.length}\n${selected.map(encode).join('\n')}\n`;
  const result = spawnSync(executable, [], { input, encoding: 'utf8', maxBuffer: 8 * 1024 * 1024 });
  assert.equal(result.status, 0, result.stderr || result.error?.message);
  const lines = result.stdout.trim().split('\n');
  assert.equal(lines.length, 17);
  for (const line of lines) assert.ok(Number(line.split(' ')[1]) > 0, line);
});
