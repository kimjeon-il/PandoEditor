"""Compile an isolated diagnostic using read-only existing build products.

This is not an accepted clean native CI build. Integration must add an ordinary
CMake target to rebuild every dependency on the final exact commit.
"""
import argparse
import json
import pathlib
import shlex
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--build', type=pathlib.Path, required=True)
parser.add_argument('--output', type=pathlib.Path, required=True)
parser.add_argument('--ninja', type=pathlib.Path, required=True)
args = parser.parse_args()
build, output = args.build.resolve(), args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
source = pathlib.Path(__file__).resolve().with_name('native-probe.cpp')
commands = json.loads((build / 'compile_commands.json').read_text())
original = next(row for row in commands if row['file'].endswith('/tests/m974_boundary_probe.cpp'))
compile_command = shlex.split(original['command'])
old_object = compile_command[compile_command.index('-o') + 1]
new_object, binary = output / 'native-probe.o', output / 'native-probe'
compile_command[compile_command.index('-o') + 1] = str(new_object)
compile_command[compile_command.index('-c') + 1] = str(source)
# The shared tests helper includes a test-local header.
compile_command.insert(1, '-I' + str(source.parents[2] / 'tests'))
subprocess.run(compile_command, cwd=build, check=True)
listing = subprocess.check_output([str(args.ninja.resolve()), '-C', str(build), '-t', 'commands', 'app/m974_boundary_probe'], text=True)
link_line = listing.strip().splitlines()[-1]
# Ninja's normal link rule is ': && c++ ... && :'. Execute the actual argv only.
link_command = shlex.split(link_line)
assert link_command[:2] == [':', '&&'] and link_command[-2:] == ['&&', ':'], link_line
link_command = link_command[2:-2]
assert old_object in link_command, 'Expected original probe object in actual link command'
link_command[link_command.index(old_object)] = str(new_object)
link_command[link_command.index('-o') + 1] = str(binary)
subprocess.run(link_command, cwd=build, check=True)
(output / 'local-build-provenance.json').write_text(json.dumps({'source': str(source), 'existingBuild': str(build), 'compile': compile_command, 'link': link_command, 'cleanExactCommitCIBuild': False}, indent=2))
print(binary)
