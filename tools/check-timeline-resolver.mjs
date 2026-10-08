#!/usr/bin/env node
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const [webRoot, binary] = process.argv.slice(2);
if (!webRoot || !binary) throw new Error('Usage: node tools/check-timeline-resolver.mjs WEB_ROOT NATIVE_PROBE');
const file = path => resolve(webRoot, path);
const fixture = JSON.parse(readFileSync(file('tests/fixtures/portability/timeline-resolution.json'), 'utf8'));
const project = JSON.parse(readFileSync(file(fixture.source), 'utf8'));
const { restoreTimelineStorage } = await import(pathToFileURL(file('assets/js/modules/timeline-storage.js')));
const { resolveWorld } = await import(pathToFileURL(file('assets/js/modules/timeline-resolver.js')));
const identities = project.territorialEntities;
const storage = restoreTimelineStorage({ schemaVersion: 1, records: project.timelineRecords,
  geometries: project.geometries }, identities.map(row => ({ id: row.id, entityKind: row.properties.entityKind })));
const observe = rows => rows.map(row => [row.id, row.geometryRef.id, row.geometryRef.version,
  row.parentId, row.rootId, row.ancestors]);
let failures = 0;
for (const row of fixture.cases) {
  const expected = JSON.stringify(row.expected);
  const web = JSON.stringify(observe(resolveWorld(identities, storage.records, storage.geometries, row.month).entities));
  const native = spawnSync(binary, ['--resolve-file', file(fixture.source), row.month], { encoding: 'utf8' });
  let app = null;
  if (native.status === 0) app = JSON.stringify(observe(JSON.parse(native.stdout).entities));
  if (web !== expected || app !== expected) {
    failures += 1;
    console.error(JSON.stringify({ month: row.month, expected: row.expected,
      web: JSON.parse(web), app: app && JSON.parse(app), nativeError: native.stderr.trim() }));
  }
}
console.log(`${fixture.cases.length} timeline golden months, ${failures} mismatches`);
if (failures) process.exitCode = 1;
