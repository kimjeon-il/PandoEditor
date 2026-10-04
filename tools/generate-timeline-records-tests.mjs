#!/usr/bin/env node
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { timelineCases } from '../tests/fixtures/timeline-records-cases.mjs';

const output = process.argv[2];
if (!output || process.argv.length !== 3) throw new Error('Usage: node tools/generate-timeline-records-tests.mjs OUTPUT_HEADER');
const text = value => JSON.stringify(value);
const date = value => value === null ? 'std::nullopt' : `std::optional<std::string>{${text(value)}}`;
const common = row => `${text(row.id)},${text(row.entityId)},{${date(row.validFrom)},${date(row.validTo)}}`;
const geometry = ref => `{${text(ref.id)},${ref.version}u}`;
const list = rows => `{${rows.join(',')}}`;
const fixtures = timelineCases().filter(row => row.native !== false).map(row => {
  const input = row.input;
  const data = `{${input.schemaVersion}u,${list(input.lifetimes.map(r => `{${common(r)}}`))},`
    + `${list(input.geometryBindings.map(r => `{${common(r)},${geometry(r.geometryRef)}}`))},`
    + `${list(input.parentRelations.map(r => `{${common(r)},${text(r.parentId)},${text(r.coverageMode)}}`))}}`;
  const entities = list(row.context.entities.map(e => `{${text(e.id)},${text(e.entityKind)}}`));
  return `{${text(row.name)},${text(row.expected)},${data},${entities},${list(row.context.geometryRefs.map(geometry))}}`;
});
const content = '// Generated from tests/fixtures/timeline-records.json; do not edit.\n'
  // A single aggregate containing every fixture crashes the Windows MinGW
  // compiler. Separate construction keeps exactly the same fixture values.
  + fixtures.map((f, index) => `static TimelineFixture timelineFixture${index}() { return ${f}; }`).join('\n')
  + '\nstatic std::vector<TimelineFixture> timelineFixtures() {\n  std::vector<TimelineFixture> out;\n'
  + `  out.reserve(${fixtures.length});\n`
  + fixtures.map((_, index) => `  out.push_back(timelineFixture${index}());`).join('\n')
  + '\n  return out;\n}\n';
mkdirSync(dirname(resolve(output)), { recursive: true });
writeFileSync(output, content);
