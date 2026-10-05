#!/usr/bin/env python3
"""Validate and pack only the approved, immutable external fixture inventory."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import tarfile

CHUNK_BYTES = 8 * 1024 * 1024
BRANCH = 'refs/heads/codex/m97-web-editing-parity'


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def git(root, *args):
    try:
        return subprocess.check_output(['git', '-C', str(root), *args], stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as error:
        raise ValueError('Git validation failed: ' + ' '.join(args)) from error


def safe_path(value):
    require(isinstance(value, str) and value and '\\' not in value, 'Invalid relative path')
    parts = value.split('/')
    require(not value.startswith('/') and all(p not in ('', '.', '..') and not p.startswith('.') for p in parts), 'Unsafe relative path')
    require(str(PurePosixPath(value)) == value and re.fullmatch(r'[A-Za-z0-9_./-]+', value), 'Noncanonical relative path')
    return parts


def pack(inventory_path, roots, output, context, application_root):
    inventory_path, output = Path(inventory_path), Path(output)
    require(not output.exists() and not output.is_symlink(), 'Output must not already exist')
    raw_inventory = inventory_path.read_bytes()
    inventory = json.loads(raw_inventory)
    require(inventory.get('schema') == 'm975-external-regression-fixture-recovery' and inventory.get('version') == 1, 'Wrong inventory schema')
    pins = inventory['pins']
    require(set(pins) == {'fullHydroAndHistorical', 'originalFullCountries'}, 'Unexpected source pins')
    require(len(set(pins.values())) == 2 and all(re.fullmatch('[a-f0-9]{40}', p) for p in pins.values()), 'Invalid source commits')
    require(set(roots) == set(pins.values()), 'Checkout roots must match both pins')
    labels = {pins['fullHydroAndHistorical']: 'full-hydro-web', pins['originalFullCountries']: 'river-country-web'}
    roots = {p: Path(r).resolve(strict=True) for p, r in roots.items()}
    heads = {p: git(r, 'rev-parse', 'HEAD').decode().strip() for p, r in roots.items()}
    require(all(heads[p] == p for p in roots), 'Source checkout HEAD mismatch')
    allowed_context = {'sha', 'runId', 'runAttempt', 'repository', 'ref', 'workflow'}
    require(set(context) == allowed_context, 'Unexpected workflow context fields')
    require(re.fullmatch('[a-f0-9]{40}', context['sha']) and re.fullmatch('[0-9]+', context['runId']) and re.fullmatch('[0-9]+', context['runAttempt']), 'Invalid workflow identity')
    require(context['ref'] == BRANCH and re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', context['repository']), 'Unexpected application branch/repository')
    application_head = git(application_root, 'rev-parse', 'HEAD').decode().strip()
    require(application_head == context['sha'], 'Application checkout HEAD mismatch')
    rows = inventory['files']
    require(isinstance(rows, list) and len(rows) == inventory['fileCount'] == 11, 'Expected exactly 11 inventory files')
    require(sum(row['bytes'] for row in rows) == inventory['totalBytes'], 'Inventory total mismatch')
    entries, seen, receipts = [], set(), []
    for row in rows:
        commit, relative, artifact = row['commit'], row['path'], row['artifactPath']
        require(commit in roots and row['repository'] == 'kimjeon-il/Pando', 'Unexpected fixture repository/commit')
        parts = safe_path(relative)
        safe_path(artifact)
        require(relative.startswith('assets/data/') and artifact == labels[commit] + '/' + relative, 'Artifact/source path mismatch')
        require(artifact not in seen, 'Duplicate artifact path')
        seen.add(artifact)
        require(row['mode'] == '100644', 'Only regular nonexecutable tracked files are allowed')
        require(type(row['bytes']) is int and 0 < row['bytes'] <= 16 * 1024 * 1024, 'Invalid source byte length')
        tree = git(roots[commit], 'ls-tree', '-z', 'HEAD', '--', relative)
        expected_tree = f"100644 blob {row['gitBlobSha1']}\t{relative}\0".encode()
        require(tree == expected_tree, 'Tracked tree path/blob/mode mismatch: ' + relative)
        file = roots[commit]
        for i, component in enumerate(parts):
            file = file / component
            info = file.lstat()
            require(not stat.S_ISLNK(info.st_mode), 'Symlink source component: ' + relative)
            require(stat.S_ISREG(info.st_mode) if i == len(parts) - 1 else stat.S_ISDIR(info.st_mode), 'Nonregular source path: ' + relative)
        require(not info.st_mode & 0o111 and info.st_size == row['bytes'], 'Working file mode/size mismatch: ' + relative)
        data = file.read_bytes()
        require(len(data) == row['bytes'], 'Source size changed during read')
        blob = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
        require(blob == row['gitBlobSha1'], 'Working source Git blob mismatch: ' + relative)
        sha = digest(data)
        require(row['sha256'] is None or sha == row['sha256'], 'Source SHA256 mismatch: ' + relative)
        entries.append((artifact, data))
        receipts.append({'path': artifact, 'sourcePath': relative, 'repository': row['repository'], 'commit': commit, 'mode': '100644', 'bytes': len(data), 'gitBlobSha1': blob, 'sha256': sha})
    # All sources have been checked before output exists. The retained verified
    # bytes, not a second filesystem read, are what enter the tar stream.
    stream = io.BytesIO()
    with tarfile.open(fileobj=stream, mode='w', format=tarfile.USTAR_FORMAT) as archive:
        for name, data in sorted(entries):
            info = tarfile.TarInfo(name)
            info.size, info.mode, info.uid, info.gid, info.mtime = len(data), 0o644, 0, 0, 0
            archive.addfile(info, io.BytesIO(data))
    archive_bytes = stream.getvalue()
    output.mkdir(parents=True)
    chunks = []
    for number, start in enumerate(range(0, len(archive_bytes), CHUNK_BYTES)):
        data = archive_bytes[start:start + CHUNK_BYTES]
        name = f'part-{number:03d}.bin'
        (output / name).write_bytes(data)
        chunks.append({'index': number, 'path': name, 'bytes': len(data), 'sha256': digest(data)})
    receipt = {'schema': 'm975-source-fixture-recovery-artifact', 'version': 1,
               'application': {**context, 'head': application_head}, 'sourceHeads': heads,
               'pins': pins, 'inventorySha256': digest(raw_inventory), 'packerSha256': digest(Path(__file__).read_bytes()),
               'fileCount': len(entries), 'sourceBytes': sum(len(data) for _, data in entries),
               'files': sorted(receipts, key=lambda r: r['path']), 'chunkBytes': CHUNK_BYTES,
               'archive': {'format': 'ustar', 'bytes': len(archive_bytes), 'sha256': digest(archive_bytes)}, 'chunks': chunks}
    (output / 'manifest.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['inventory', 'application-root', 'hydro-root', 'country-root', 'output']:
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    inventory = json.loads(args.inventory.read_bytes())
    require(inventory['fileCount'] == 11 and inventory['totalBytes'] == 30878542, 'Approved closure size/count mismatch')
    expected_pins = {'fullHydroAndHistorical': 'c0bd31d13dc8495593d78cf51f7cc195de7c9469', 'originalFullCountries': '53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47'}
    require(inventory['pins'] == expected_pins, 'Approved immutable pins changed')
    require(os.environ.get('GITHUB_ACTIONS') == 'true', 'Artifact packing requires the authorized workflow')
    context = {key: os.environ[env] for key, env in [('sha', 'GITHUB_SHA'), ('runId', 'GITHUB_RUN_ID'), ('runAttempt', 'GITHUB_RUN_ATTEMPT'), ('repository', 'GITHUB_REPOSITORY'), ('ref', 'GITHUB_REF'), ('workflow', 'GITHUB_WORKFLOW')]}
    receipt = pack(args.inventory, {expected_pins['fullHydroAndHistorical']: args.hydro_root, expected_pins['originalFullCountries']: args.country_root}, args.output, context, args.application_root)
    require(len(receipt['chunks']) == 4, 'Approved archive must yield four bounded chunks')
    print(json.dumps({'files': receipt['fileCount'], 'sourceBytes': receipt['sourceBytes'], 'chunks': len(receipt['chunks']), 'archiveSha256': receipt['archive']['sha256']}))


if __name__ == '__main__':
    main()
