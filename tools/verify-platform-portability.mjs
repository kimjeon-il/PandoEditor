import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { resolve } from 'node:path';
import { pathToFileURL, fileURLToPath } from 'node:url';

const [webRootArg, probeArg] = process.argv.slice(2);
assert.ok(webRootArg && probeArg, 'Usage: node tools/verify-platform-portability.mjs WEB_ROOT SELECTION_PROBE');
const webRoot = resolve(webRootArg), nativeProbe = resolve(probeArg);
const appRoot = fileURLToPath(new URL('..', import.meta.url));
const pin = JSON.parse(readFileSync(new URL('../docs/platform-portability-pin.json', import.meta.url)));
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
assert.match(pin.webContractCommit, /^[0-9a-f]{40}$/);
for (const [path, expected] of Object.entries(pin.sha256)) {
  const committed = execFileSync('git', ['show', `${pin.webContractCommit}:${path}`], { cwd: webRoot });
  assert.equal(hash(committed), expected, `Pinned Web contract: ${path}`);
  assert.equal(hash(readFileSync(resolve(webRoot, path))), expected, `Working Web contract: ${path}`);
}
const { verifyPlatformPortability } = await import(pathToFileURL(resolve(webRoot, 'tools/check-platform-portability.mjs')));
const report = verifyPlatformPortability(nativeProbe);
assert.equal(report.crossPlatformVerified, true);
report.webContractCommit = pin.webContractCommit;
report.webHead = execFileSync('git', ['rev-parse', 'HEAD'], { cwd: webRoot, encoding: 'utf8' }).trim();
report.appHead = execFileSync('git', ['rev-parse', 'HEAD'], { cwd: appRoot, encoding: 'utf8' }).trim();
report.appDiffSha256 = hash(execFileSync('git', ['diff', '--no-ext-diff', 'HEAD'], { cwd: appRoot }));
console.log(JSON.stringify(report, null, 2));
