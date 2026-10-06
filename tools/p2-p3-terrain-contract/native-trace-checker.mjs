// Future native trace checker. This file does not implement native state transitions.
import assert from 'node:assert/strict';
import { tiles } from './native-receipt-cases.mjs';

export function exactCoverageGaps(rectangles, domain) {
  const [west, north, east, south] = domain;
  assert.ok([west, north, east, south].every(Number.isFinite) && west < east && south < north);
  const clipped = rectangles.map(([w, n, e, s]) => [Math.max(w, west), Math.min(n, north), Math.min(e, east), Math.max(s, south)])
    .filter(([w, n, e, s]) => w < e && s < n);
  const xs = [...new Set([west, east, ...clipped.flatMap(row => [row[0], row[2]])])].sort((a, b) => a - b);
  const ys = [...new Set([south, north, ...clipped.flatMap(row => [row[3], row[1]])])].sort((a, b) => a - b);
  const gaps = [];
  // An arrangement cell is either fully covered or fully uncovered; sparse
  // point sampling cannot certify coverage and would miss narrow boundary gaps.
  for (let x = 0; x + 1 < xs.length; x++) for (let y = 0; y + 1 < ys.length; y++) {
    if (!clipped.some(([w, n, e, s]) => w <= xs[x] && e >= xs[x + 1] && s <= ys[y] && n >= ys[y + 1]))
      gaps.push([xs[x], ys[y + 1], xs[x + 1], ys[y]]);
  }
  return gaps;
}

export function validateObservations(observations, specification) {
  assert.equal(observations.length, specification.steps.length, specification.id);
  for (let i = 0; i < observations.length; i++) {
    const { action, expected } = specification.steps[i], actual = observations[i];
    assert.deepEqual(actual, expected, `${specification.id}:${action}:literal observation mismatch`);
    for (const field of ['gpuReadyKeys', 'drawKeys', 'protectedKeys', 'releasedKeys'])
      assert.equal(new Set(actual[field]).size, actual[field].length, `${action}:${field}:duplicate`);
    assert.equal(actual.drawBounds.length, actual.drawKeys.length, `${action}:missing draw bounds`);
    assert.equal(actual.drawResourceKeys.length, actual.drawKeys.length, `${action}:missing draw resource`);
    for (const [index, draw] of actual.drawKeys.entries()) {
      assert.ok(tiles[draw], `${action}:unknown geographic draw`);
      assert.ok(actual.gpuReadyKeys.includes(actual.drawResourceKeys[index]), `${action}:draw without GPU resource`);
    }
    if (actual.drawKeys.length)
      assert.deepEqual(exactCoverageGaps(actual.drawBounds, specification.domain), [], `${action}:coverage gap`);
    if (actual.releasedKeys.length) assert.equal(actual.receiptAccepted, true, `${action}:unpin before accepted receipt`);
  }
}

export function validateActualNativeTrace(trace, specification) {
  assert.equal(trace.schema, 'pandoeditor-p2-p3-terrain-native-receipt-trace');
  assert.equal(trace.version, 1);
  assert.equal(trace.execution, 'actual-native');
  assert.equal(trace.scenarioId, specification.id);
  assert.match(trace.nativeCommit, /^[a-f0-9]{40}$/);
  assert.match(trace.nativeBinarySha256, /^[a-f0-9]{64}$/);
  assert.ok(typeof trace.runId === 'string' && trace.runId.length > 0);
  validateObservations(trace.observations, specification);
  // These fields are provenance declarations, not independent execution proof.
  // The gate owner must authenticate binary/run/log artifacts externally.
}
