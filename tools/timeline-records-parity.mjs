#!/usr/bin/env node
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { timelineCases, timelineContext } from '../tests/fixtures/timeline-records-cases.mjs';

if (process.argv.length !== 4) throw new Error('Usage: node tools/timeline-records-parity.mjs NATIVE_TEST_BINARY WEB_TIMELINE_MODULE');
const [binary, modulePath] = process.argv.slice(2).map(resolvePath => resolve(resolvePath));
const { normalizeTimelineRecords } = await import(pathToFileURL(modulePath));
assert.equal(typeof normalizeTimelineRecords, 'function');
const cases = timelineCases().filter(row => row.native !== false);
const expected = cases.map(row => {
  let verdict = 'OK';
  try { normalizeTimelineRecords(row.input, timelineContext(row.context)); }
  catch (error) { verdict = error.code || 'UNEXPECTED'; }
  assert.equal(verdict, row.expected, `Web fixture: ${row.name}`);
  return `${row.name}\t${verdict}`;
}).join('\n') + '\n';
const native = execFileSync(binary, { encoding: 'utf8', maxBuffer: 2 * 1024 * 1024, stdio: ['ignore', 'pipe', 'pipe'] });
assert.equal(native.replace(/\r\n/g, '\n'), expected);
console.log(`${cases.length}/${cases.length} web/native timeline validation verdicts match.`);
