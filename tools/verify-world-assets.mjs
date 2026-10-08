import { readFileSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { validateWorldDataset, verifyWorldFiles } from './lib/world-dataset-contract.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(process.argv[2] || resolve(here, '../assets/world'));
try {
  const manifest = validateWorldDataset(JSON.parse(readFileSync(resolve(root, 'manifest.json'), 'utf8')));
  const verified = verifyWorldFiles(root, manifest);
  console.log('World assets verified: ' + verified.length + ' entries; ' + manifest.origin.mode);
} catch (error) {
  console.error('World asset verification failed: ' + error.message);
  process.exitCode = 1;
}
