import test from 'node:test';
import assert from 'node:assert/strict';
import { compareProject } from './timeline-final-exchange.mjs';

const fixture = {
  territorialEntities: [{ id: 'A', properties: { metadata: { sequence: ['earlier', 'later'] } } }, { id: 'B' }],
  timelineRecords: { schemaVersion: 1,
    lifetimes: [{ id: 'life:A', entityId: 'A', validFrom: '1900-02', validTo: null }],
    geometryBindings: [{ id: 'binding:A', geometryRef: { id: 'shared', version: 2 } }],
    parentRelations: [{ id: 'parent:A', parentId: 'B', coverageMode: 'explicit' }] },
  geometries: [{ id: 'shared', version: 1, geometry: { coordinates: [[[0, 0], [1, 1]]] } },
    { id: 'shared', version: 2, geometry: { coordinates: [[[2, 2], [3, 3]]] } },
    { id: 'unreferenced', version: 1, geometry: { coordinates: [[[4, 4], [5, 5]]] } }],
};
test('identity row order is nonsemantic, including archive versions', () => {
  const actual = structuredClone(fixture);
  actual.territorialEntities.reverse(); actual.geometries.reverse();
  compareProject(actual, fixture, 'reordered');
});
for (const [name, mutate] of [
  ['nested metadata order', p => p.territorialEntities[0].properties.metadata.sequence.reverse()],
  ['endpoint precision', p => p.timelineRecords.lifetimes[0].validFrom = '1900-02-28'],
  ['past version coordinate', p => p.geometries[0].geometry.coordinates[0][0][0] += 0.000000001],
  ['unreferenced archive loss', p => p.geometries.pop()],
  ['duplicate record', p => p.timelineRecords.lifetimes.push(p.timelineRecords.lifetimes[0])],
  ['parent relation', p => p.timelineRecords.parentRelations[0].parentId = ''],
]) test(`rejects ${name} loss`, () => {
  const actual = structuredClone(fixture); mutate(actual);
  assert.throws(() => compareProject(actual, fixture, name));
});
