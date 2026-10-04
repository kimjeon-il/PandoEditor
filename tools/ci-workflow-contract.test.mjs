import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {test} from 'node:test';

const workflow = new URL('../.github/workflows/m5-validation.yml', import.meta.url);
const source = readFileSync(workflow, 'utf8');
const modules = source.match(/modules:\s*['"]([^'"]+)['"]/)?.[1].trim().split(/\s+/) ?? [];

test('M5 validation installs Qt ShaderTools required by the application build', () => {
  assert.ok(modules.includes('qtshadertools'));
});

test('M5 validation installs Qt Image Formats required by the terrain WebP regression', () => {
  assert.ok(modules.includes('qtimageformats'));
});
