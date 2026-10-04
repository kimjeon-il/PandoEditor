#!/usr/bin/env node
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { timelineStorageCases } from '../tests/fixtures/timeline-storage-cases.mjs';

if (process.argv.length !== 3) throw new Error('Usage: node tools/generate-timeline-storage-tests.mjs OUTPUT_HEADER');
const text = value => JSON.stringify(value);
const list = values => `{${values.join(',')}}`;
const date = value => value === null ? 'std::nullopt' : `std::optional<std::string>{${text(value)}}`;
const ref = value => `{${text(value.id)},${value.version}u}`;
const common = row => `${text(row.id)},${text(row.entityId)},{${date(row.validFrom)},${date(row.validTo)}}`;
const point = value => list(value.map(number => String(number)));
const line = value => list(value.map(point));
const polygon = value => list(value.map(line));
function geometry(value) {
  let points = '{}', lines = '{}', polygons = '{}';
  switch (value.type) {
    case 'Point': points = list([point(value.coordinates)]); break;
    case 'MultiPoint': points = line(value.coordinates); break;
    case 'LineString': lines = list([line(value.coordinates)]); break;
    case 'MultiLineString': lines = polygon(value.coordinates); break;
    case 'Polygon': polygons = list([polygon(value.coordinates)]); break;
    case 'MultiPolygon': polygons = list(value.coordinates.map(polygon)); break;
    default: throw new Error(`Unsupported fixture geometry: ${value.type}`);
  }
  return `std::make_shared<const Geometry>(Geometry{${text(value.type)},${points},${lines},${polygons}})`;
}
const rows = timelineStorageCases().filter(row => row.native !== false).map(row => {
  const records = row.input.records;
  const recordValue = `{${records.schemaVersion}u,${list(records.lifetimes.map(r => `{${common(r)}}`))},`
    + `${list(records.geometryBindings.map(r => `{${common(r)},${ref(r.geometryRef)}}`))},`
    + `${list(records.parentRelations.map(r => `{${common(r)},${text(r.parentId)},${text(r.coverageMode)}}`))}}`;
  const geometries = list(row.input.geometries.map(r => `{${ref(r)},${geometry(r.geojson)}}`));
  return `{${text(row.name)},${text(row.expected)},{${row.input.schemaVersion}u,${recordValue},${geometries}},`
    + `${list(row.entities.map(e => `{${text(e.id)},${text(e.entityKind)}}`))}}`;
});
const output = resolve(process.argv[2]);
mkdirSync(dirname(output), { recursive: true });
writeFileSync(output, '// Generated from tests/fixtures/timeline-storage.json; do not edit.\n'
  + 'static std::vector<StorageFixture> storageFixtures() { return {\n'
  + rows.map(row => `  ${row}`).join(',\n') + '\n}; }\n');
