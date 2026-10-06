// Authored proposed native observations, NOT native output or an implementation.
// Synthetic dyadic tiles isolate coverage/state behavior from real DEM decoding.
export const scope = Object.freeze({ sourceEpoch: 7, windowEpoch: 9, contextEpoch: 11,
  projectGeneration: 13, viewGeneration: 17, maskGeneration: 19, candidateSequence: 23, requestSequence: 29 });
export const tiles = Object.freeze({
  '0/w': [-180, 90, 0, -90], '0/e': [0, 90, 180, -90],
  '1/old-west': [-180, 90, 0, -90],
  '2/nw': [-180, 90, 0, 0], '2/sw': [-180, 0, 0, -90],
  '2/ne': [0, 90, 180, 0], '2/se': [0, 0, 180, -90],
  '0/w@360': [180, 90, 360, -90], '0/e@360': [360, 90, 540, -90],
});
const base = ['0/w', '0/e'];
const quadrants = ['2/nw', '2/sw', '2/ne', '2/se'];
const snapshot = (gpuReadyKeys, drawKeys, protectedKeys, releasedKeys = [], receiptAccepted = false) =>
  ({ scope: { ...scope }, gpuReadyKeys: [...gpuReadyKeys].sort(), drawKeys,
    drawBounds: drawKeys.map(key => [...tiles[key]]), drawResourceKeys: drawKeys.map(key => key.split('@')[0]),
    protectedKeys: [...protectedKeys].sort(), releasedKeys: [...releasedKeys].sort(), receiptAccepted });
export const cases = Object.freeze([
  {
    id: 'cold-detail-before-base', domain: [-180, 90, 180, -90],
    steps: [
      { action: 'CPU_READY_DETAIL', expected: snapshot([], [], []) },
      { action: 'SUBMIT_DETAIL', expected: snapshot(quadrants, [], quadrants) },
      { action: 'SUBMIT_WEST_BASE', expected: snapshot([...quadrants, '0/w'], [], [...quadrants, '0/w']) },
      { action: 'SUBMIT_EAST_BASE', expected: snapshot([...quadrants, ...base], [], [...quadrants, ...base]) },
      { action: 'PRESENT_CURRENT_CANDIDATE', expected: snapshot([...quadrants, ...base], quadrants, [...quadrants, ...base], [], true) },
    ],
  },
  {
    id: 'partial-replacement-unpin-only-after-present', domain: [-180, 90, 180, -90],
    steps: [
      { action: 'INITIAL_DISPLAY', expected: snapshot([...base, '1/old-west'], [...base, '1/old-west'], [...base, '1/old-west'], [], true) },
      { action: 'CPU_READY_NORTHWEST', expected: snapshot([...base, '1/old-west'], [...base, '1/old-west'], [...base, '1/old-west']) },
      { action: 'SUBMIT_NORTHWEST', expected: snapshot([...base, '1/old-west', '2/nw'], [...base, '1/old-west'], [...base, '1/old-west', '2/nw']) },
      { action: 'PRESENT_NORTHWEST', expected: snapshot([...base, '1/old-west', '2/nw'], [...base, '1/old-west', '2/nw'], [...base, '1/old-west', '2/nw'], [], true) },
      { action: 'SUBMIT_SOUTHWEST', expected: snapshot([...base, '1/old-west', '2/nw', '2/sw'], [...base, '1/old-west', '2/nw'], [...base, '1/old-west', '2/nw', '2/sw']) },
      { action: 'PRESENT_COMPLETE_WEST', expected: snapshot([...base, '1/old-west', '2/nw', '2/sw'], [...base, '2/nw', '2/sw'], [...base, '2/nw', '2/sw'], ['1/old-west'], true) },
      { action: 'SUBMIT_EAST', expected: snapshot([...base, '1/old-west', ...quadrants], [...base, '2/nw', '2/sw'], [...base, ...quadrants]) },
      { action: 'PRESENT_COMPLETE_TARGET', expected: snapshot([...base, '1/old-west', ...quadrants], quadrants, [...base, ...quadrants], [], true) },
    ],
  },
  {
    id: 'zoom-out-replacement-last', domain: [-180, 90, 180, -90],
    steps: [
      { action: 'INITIAL_FINE_DISPLAY', expected: snapshot([...base, ...quadrants], quadrants, [...base, ...quadrants], [], true) },
      { action: 'SUBMIT_COARSE_WEST', expected: snapshot([...base, ...quadrants, '1/old-west'], quadrants, [...base, ...quadrants, '1/old-west']) },
      // Retained eastern fine tiles first, then ready coarser target west last.
      { action: 'PRESENT_COARSE_WEST', expected: snapshot([...base, ...quadrants, '1/old-west'], [...base, '2/ne', '2/se', '1/old-west'], [...base, '2/ne', '2/se', '1/old-west'], ['2/nw', '2/sw'], true) },
    ],
  },
  ...Object.keys(scope).map(field => ({
    id: `reject-stale-${field}`, domain: [-180, 90, 180, -90],
    steps: [
      { action: 'INITIAL_DISPLAY', expected: snapshot(base, base, base, [], true) },
      { action: 'STALE_PRESENT', receipt: { ...scope, [field]: scope[field] - 1 }, expected: snapshot(base, base, base) },
    ],
  })),
  {
    id: 'flat-dateline-replica-no-extra-texture-key', domain: [170, 90, 190, -90],
    steps: [{ action: 'PRESENT_CURRENT_CANDIDATE', expected: snapshot(base, ['0/e', '0/w@360'], base, [], true) }],
  },
]);
