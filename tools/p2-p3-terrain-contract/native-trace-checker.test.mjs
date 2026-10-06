import test from 'node:test';
import assert from 'node:assert/strict';
import { cases, tiles } from './native-receipt-cases.mjs';
import { exactCoverageGaps, validateObservations, validateActualNativeTrace } from './native-trace-checker.mjs';

for (const specification of cases) {
  test(`proposed trace checker accepts authored literal case: ${specification.id}`, () => {
    validateObservations(structuredClone(specification.steps.map(step => step.expected)), specification);
  });
}

test('checker rejects495 independently altered scope/readiness/draw/bounds/resources/protection/release observations', () => {
  let rejected = 0;
  for (const specification of cases) for (let i = 0; i < specification.steps.length; i++) {
    const fresh = () => structuredClone(specification.steps.map(step => step.expected));
    for (const field of Object.keys(fresh()[i].scope)) {
      const rows = fresh(); rows[i].scope[field]++;
      assert.throws(() => validateObservations(rows, specification)); rejected++;
    }
    for (const field of ['gpuReadyKeys', 'drawKeys', 'protectedKeys', 'releasedKeys']) {
      const rows = fresh(); rows[i][field].push('corrupt-resource');
      assert.throws(() => validateObservations(rows, specification)); rejected++;
    }
    const bounds = fresh();
    if (bounds[i].drawBounds.length) bounds[i].drawBounds[0][0] += 0.000001;
    else bounds[i].drawBounds.push([-180, 90, 180, -90]);
    assert.throws(() => validateObservations(bounds, specification)); rejected++;
    const resources = fresh(); resources[i].drawResourceKeys.push('corrupt-resource');
    assert.throws(() => validateObservations(resources, specification)); rejected++;
    const rows = fresh(); rows[i].receiptAccepted = !rows[i].receiptAccepted;
    assert.throws(() => validateObservations(rows, specification)); rejected++;
  }
  assert.equal(rejected, 495);
  assert.equal(rejected, cases.reduce((sum, row) => sum + row.steps.length, 0) * 15);
});

test('stale receipt cases cannot release fallback or change inventory', () => {
  for (const specification of cases.filter(row => row.id.startsWith('reject-stale-'))) {
    const before = specification.steps[0].expected, after = specification.steps[1].expected;
    assert.deepEqual(after.drawKeys, before.drawKeys);
    assert.deepEqual(after.gpuReadyKeys, before.gpuReadyKeys);
    assert.deepEqual(after.protectedKeys, before.protectedKeys);
    assert.deepEqual(after.releasedKeys, []);
    assert.equal(after.receiptAccepted, false);
  }
});

test('full target handoff releases old detail but keeps mandatory base pinned', () => {
  const specification = cases.find(row => row.id === 'partial-replacement-unpin-only-after-present');
  const firstWest = specification.steps.find(row => row.action === 'PRESENT_NORTHWEST').expected;
  assert.ok(firstWest.protectedKeys.includes('1/old-west'));
  const completeWest = specification.steps.find(row => row.action === 'PRESENT_COMPLETE_WEST').expected;
  assert.deepEqual(completeWest.releasedKeys, ['1/old-west']);
  const complete = specification.steps.at(-1).expected;
  assert.ok(complete.protectedKeys.includes('0/w') && complete.protectedKeys.includes('0/e'));
  assert.ok(!complete.drawKeys.includes('0/w') && !complete.drawKeys.includes('0/e'));
});

test('exact rectangle coverage catches1e-9 slivers that coarse point samples miss', () => {
  assert.deepEqual(exactCoverageGaps([tiles['0/w'], tiles['0/e']], [-180, 90, 180, -90]), []);
  const width = 1e-9;
  assert.deepEqual(exactCoverageGaps([[-180, 90, 0, -90], [width, 90, 180, -90]], [-180, 90, 180, -90]), [[0, 90, width, -90]]);
  assert.deepEqual(exactCoverageGaps([[-180, 90, 180, 0.001], [-180, 0, 180, -90]], [-180, 90, 180, -90]), [[-180, 0.001, 180, 0]]);
});

test('canonical edges/poles/dateline copies and overlap do not create false gaps', () => {
  assert.deepEqual(exactCoverageGaps([tiles['2/nw'], tiles['2/sw'], tiles['2/ne'], tiles['2/se']], [-180, 90, 180, -90]), []);
  assert.deepEqual(exactCoverageGaps([tiles['0/e'], tiles['0/w@360']], [170, 90, 190, -90]), []);
  assert.deepEqual(exactCoverageGaps([tiles['0/e']], [170, 90, 190, -90]), [[180, 90, 190, -90]]);
  assert.deepEqual(exactCoverageGaps([tiles['0/w'], tiles['0/e'], tiles['1/old-west']], [-180, 90, 180, -90]), []);
});

test('authored observations alone cannot be labeled actual native execution', () => {
  const specification = cases[0];
  assert.throws(() => validateActualNativeTrace({
    schema: 'pandoeditor-p2-p3-terrain-native-receipt-trace', version: 1,
    execution: 'synthetic-checker-self-test', scenarioId: specification.id,
    observations: specification.steps.map(row => row.expected),
  }, specification), /actual-native/);
});
