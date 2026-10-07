import assert from 'node:assert/strict';
import test from 'node:test';
import { mkdirSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { containedFile, sourceIdentity, verifyContractPin, verifyBuildReceipt, verifyCTestCommand, sha256 } from './provenance.mjs';

function repository(t) {
  const root = mkdtempSync(join(tmpdir(), 'pando-parity-'));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  const git = args => execFileSync('git', args, { cwd: root, stdio: 'pipe' });
  git(['init']); writeFileSync(join(root, 'source.txt'), 'original'); git(['add', 'source.txt']);
  git(['-c', 'user.name=Parity Test', '-c', 'user.email=parity@example.invalid', 'commit', '-m', 'fixture']);
  return root;
}
test('source identity includes tracked edits and untracked file content', t => {
  const root = repository(t), first = sourceIdentity(root);
  assert.equal(first.clean, true);
  writeFileSync(join(root, 'source.txt'), 'changed');
  assert.notEqual(sourceIdentity(root).fingerprint, first.fingerprint);
  writeFileSync(join(root, 'new.txt'), 'one'); const second = sourceIdentity(root);
  writeFileSync(join(root, 'new.txt'), 'two');
  assert.notEqual(sourceIdentity(root).fingerprint, second.fingerprint);
});
test('wrong commits, contract bytes and path traversal cannot pass provenance', t => {
  const root = repository(t), identity = sourceIdentity(root);
  assert.throws(() => containedFile(root, '../escape'), /Unsafe/);
  assert.throws(() => verifyContractPin(root, { webContractCommit: identity.head, sha256: { 'source.txt': sha256('tampered') } }), /hash mismatch/);
  assert.throws(() => verifyContractPin(root, { webContractCommit: 'wrong', sha256: {} }), /pin/);
});
test('a Windows CRLF checkout preserves pinned text while changed content still fails', t => {
  const root = repository(t);
  const git = args => execFileSync('git', args, { cwd: root, stdio: 'pipe' });
  git(['config', 'core.autocrlf', 'true']);
  mkdirSync(join(root, 'tests/fixtures/portability'), { recursive: true });
  writeFileSync(join(root, 'tests/fixtures/portability/index.json'), '{}\n');
  writeFileSync(join(root, 'lines.json'), '{"value":1}\n');
  git(['add', 'lines.json', 'tests/fixtures/portability/index.json']);
  git(['-c', 'user.name=Parity Test', '-c', 'user.email=parity@example.invalid', 'commit', '-m', 'lines']);
  const pin = { webContractCommit: sourceIdentity(root).head, sha256: {
    'tests/fixtures/portability/index.json': sha256('{}\n'), 'lines.json': sha256('{"value":1}\n') } };
  writeFileSync(join(root, 'lines.json'), '{"value":1}\r\n');
  assert.equal(verifyContractPin(root, pin).files.length, 2);
  writeFileSync(join(root, 'lines.json'), '{"value":2}\r\n');
  assert.throws(() => verifyContractPin(root, pin), /Candidate contract changed/);
});
test('stale source and substituted native binary cannot pass a build receipt', t => {
  const root = repository(t); writeFileSync(join(root, 'probe'), 'built');
  const receipt = { schema: 'web-app-parity-build', source: sourceIdentity(root), targets: { probe: { path: 'probe', sha256: sha256('built') } } };
  assert.equal(verifyBuildReceipt(root, root, receipt, 'probe'), join(root, 'probe'));
  writeFileSync(join(root, 'probe'), 'other');
  assert.throws(() => verifyBuildReceipt(root, root, receipt, 'probe'), /mismatch/);
});

test('CTest must execute the receipt binary, including environment based probes', () => {
  assert.deepEqual(verifyCTestCommand(['/build/probe'], ['/build/probe']), ['/build/probe']);
  assert.deepEqual(verifyCTestCommand(['cmake','-E','env','PROBE=/build/probe','node','test.mjs'], ['/build/probe']), ['cmake','-E','env','PROBE=/build/probe','node','test.mjs']);
  assert.throws(() => verifyCTestCommand(['/other/probe'], ['/build/probe']), /verified binary/);
  assert.throws(() => verifyCTestCommand(['/build/probe-stale'], ['/build/probe']), /verified binary/);
});
