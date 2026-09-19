// Usage: node tools/verify-web-import-goldens.mjs /path/to/pinned/world-map [--write]
// Read-only by default. Updating expected files requires an explicit --write.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const fixtureDir = path.join(root, 'tests/fixtures/web-import');
const manifest = JSON.parse(fs.readFileSync(path.join(fixtureDir, 'provenance.json'), 'utf8'));
const checkout = process.argv[2];
if (!checkout) throw new Error(`Provide ${manifest.repository} checked out at ${manifest.commit}.`);
if (process.argv.slice(3).some(arg => arg !== '--write')) throw new Error('Only --write is accepted after the checkout path.');
for (const [relative, expected] of Object.entries(manifest.sourceSha256)) {
  const actual = createHash('sha256').update(fs.readFileSync(path.join(checkout, relative))).digest('hex');
  assert.equal(actual, expected, `Pinned source mismatch: ${relative}`);
}
const { migrateProjectToCurrent } = await import(pathToFileURL(path.resolve(checkout, 'assets/js/modules/project-migrations.js')));
for (const name of manifest.fixtures) {
  const input = JSON.parse(fs.readFileSync(path.join(fixtureDir, `${name}.input.json`), 'utf8'));
  const actual = migrateProjectToCurrent(input);
  const expectedPath = path.join(fixtureDir, `${name}.expected.json`);
  if (process.argv.includes('--write')) fs.writeFileSync(expectedPath, JSON.stringify(actual, null, 2) + '\n');
  else assert.deepEqual(actual, JSON.parse(fs.readFileSync(expectedPath, 'utf8')), `Oracle mismatch: ${name}`);
  console.log(`${name}: original-source oracle ${process.argv.includes('--write') ? 'written' : 'verified'}`);
}
