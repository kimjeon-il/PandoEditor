import { mkdirSync, writeFileSync, readFileSync, readdirSync } from 'node:fs';
import { resolve, relative } from 'node:path';
import { execFileSync } from 'node:child_process';
import { sourceIdentity, sha256 } from './provenance.mjs';

export function prepareNativeBuild({ appRoot, buildRoot, targets, cmake = 'cmake', configureArgs = [] }) {
  const before = sourceIdentity(appRoot);
  const query = resolve(buildRoot, '.cmake/api/v1/query'); mkdirSync(query, { recursive: true });
  writeFileSync(resolve(query, 'codemodel-v2'), '');
  execFileSync(cmake, ['-S', appRoot, '-B', buildRoot, '-DBUILD_TESTING=ON', ...configureArgs], { stdio: 'inherit' });
  const reply = resolve(buildRoot, '.cmake/api/v1/reply');
  const indexName = readdirSync(reply).filter(name => name.startsWith('index-')).sort().at(-1);
  const index = JSON.parse(readFileSync(resolve(reply, indexName), 'utf8'));
  const model = JSON.parse(readFileSync(resolve(reply, index.reply['codemodel-v2'].jsonFile), 'utf8'));
  if (resolve(model.paths.source) !== resolve(appRoot)) throw new Error('CMake source root mismatch');
  const configuration = model.configurations.find(row => row.name === 'Release') ?? model.configurations[0];
  const selected = [...new Set(targets)];
  for (const name of selected) if (!configuration.targets.some(row => row.name === name)) throw new Error(`CMake target missing: ${name}`);
  if (selected.length) execFileSync(cmake, ['--build', buildRoot, '--config', configuration.name || 'Release', '--parallel', '2', '--target', ...selected], { stdio: 'inherit' });
  const after = sourceIdentity(appRoot);
  if (before.fingerprint !== after.fingerprint) throw new Error('App sources changed during the native build');
  const entries = {};
  for (const name of selected) {
    const record = configuration.targets.find(row => row.name === name);
    const target = JSON.parse(readFileSync(resolve(reply, record.jsonFile), 'utf8'));
    if (target.type !== 'EXECUTABLE' || !target.artifacts?.length) throw new Error(`Not an executable: ${name}`);
    const binary = resolve(buildRoot, target.artifacts[0].path);
    entries[name] = { path: relative(buildRoot, binary).replaceAll('\\', '/'), sha256: sha256(readFileSync(binary)) };
  }
  const receipt = { schema: 'web-app-parity-build', version: 1, source: after, platform: process.platform,
    cmake: execFileSync(cmake, ['--version'], { encoding: 'utf8' }).split('\n')[0],
    configuration: configuration.name, cacheSha256: sha256(readFileSync(resolve(buildRoot, 'CMakeCache.txt'))), targets: entries };
  writeFileSync(resolve(buildRoot, 'parity-build-receipt.json'), JSON.stringify(receipt, null, 2) + '\n');
  return receipt;
}
