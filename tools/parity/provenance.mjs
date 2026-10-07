import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { readFileSync, existsSync, realpathSync } from 'node:fs';
import { resolve, relative, isAbsolute, sep } from 'node:path';

export const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
export const git = (root, args) => execFileSync('git', args, { cwd: root, stdio: ['ignore', 'pipe', 'pipe'], maxBuffer: 64 * 1024 * 1024 });
export function sourceIdentity(root) {
  const head = git(root, ['rev-parse', 'HEAD']).toString().trim();
  const status = git(root, ['status', '--porcelain=v1', '-z', '--untracked-files=all']).toString();
  const patch = git(root, ['diff', '--no-ext-diff', '--binary', 'HEAD']);
  const changedPaths = git(root, ['diff', '--name-only', '-z', 'HEAD']).toString().split('\0').filter(Boolean);
  const changed = changedPaths.map(path => ({ path, sha256: existsSync(resolve(root, path)) ? sha256(readFileSync(resolve(root, path))) : null }));
  const untracked = git(root, ['ls-files', '--others', '--exclude-standard', '-z']).toString().split('\0').filter(Boolean);
  const files = untracked.map(path => ({ path, sha256: sha256(readFileSync(resolve(root, path))) }));
  return { head, clean: status.length === 0, status, diffSha256: sha256(patch), changed, untracked: files,
    fingerprint: sha256(JSON.stringify({ head, patch: sha256(patch), changed, files })) };
}
export function containedFile(root, path) {
  if (typeof path !== 'string' || !path || path.includes('\\') || isAbsolute(path) || path.split('/').some(part => part === '..')) {
    throw new Error(`Unsafe evidence path: ${path}`);
  }
  const file = resolve(root, path), rel = relative(resolve(root), file);
  if (rel.startsWith('..') || isAbsolute(rel)) throw new Error(`Path outside root: ${path}`);
  if (existsSync(file)) {
    const real = realpathSync(file), base = realpathSync(root);
    if (!real.startsWith(base + sep)) throw new Error(`Symlink outside root: ${path}`);
  }
  return file;
}
export function verifyContractPin(webRoot, pin) {
  if (!/^[a-f0-9]{40}$/.test(pin.webContractCommit) || !pin.sha256 || !Object.keys(pin.sha256).length) throw new Error('Missing contract source pin');
  const files = [];
  for (const [path, expected] of Object.entries(pin.sha256)) {
    const file = containedFile(webRoot, path);
    const committed = git(webRoot, ['show', `${pin.webContractCommit}:${path}`]);
    if (sha256(committed) !== expected) throw new Error(`Pinned contract hash mismatch: ${path}`);
    if (sha256(readFileSync(file)) !== expected) throw new Error(`Candidate contract changed: ${path}`);
    files.push({ path, sha256: expected });
  }
  if (!files.some(row => row.path === 'tests/fixtures/portability/index.json')) throw new Error('Parity index is not approved by the source pin');
  return { commit: pin.webContractCommit, files };
}
export function verifyBuildReceipt(appRoot, buildRoot, receipt, target) {
  const current = sourceIdentity(appRoot);
  if (receipt.schema !== 'web-app-parity-build' || receipt.source.fingerprint !== current.fingerprint) throw new Error('Native build source mismatch');
  const entry = receipt.targets[target];
  if (!entry) throw new Error(`Native target absent from build receipt: ${target}`);
  const binary = containedFile(buildRoot, entry.path);
  if (sha256(readFileSync(binary)) !== entry.sha256) throw new Error(`Native binary hash mismatch: ${target}`);
  return binary;
}

export function verifyCTestCommand(command, binaries) {
  if (!Array.isArray(command) || !command.length) throw new Error('CTest command is missing');
  const normalize = value => process.platform === 'win32' ? value.replaceAll('\\', '/').toLowerCase() : value;
  const argumentsSet = new Set(command.flatMap(value => [normalize(value), normalize(value.slice(value.indexOf('=') + 1))]));
  for (const binary of binaries) if (!argumentsSet.has(normalize(binary))) throw new Error('CTest command does not use verified binary: ' + binary);
  return command;
}
