import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {test} from 'node:test';

const workflow = new URL('../.github/workflows/m5-validation.yml', import.meta.url);

test('M5 validation installs Qt ShaderTools required by the application build', () => {
  const source = readFileSync(workflow, 'utf8');
  assert.match(source, /modules:\s*['"]qtshadertools['"]/);
});
