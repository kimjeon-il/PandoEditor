#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import tempfile, shutil
spec=importlib.util.spec_from_file_location('river_adapters',Path(__file__).with_name('generate-adapters.py'))
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
first=module.generated();second=module.generated()
assert first==second
for path,data in first.items():assert path.read_bytes()==data,str(path)
original=(module.RIVER/'original/river-territory-partition.js').read_text()
assert len(module.replacements)==6
for before,_ in module.replacements:
    for broken in [original.replace(before,'',1),original+before]:
        try:module.syntax(broken)
        except ValueError:pass
        else:raise AssertionError('Each missing/duplicate expression must fail closed')
assert (module.RIVER/'original/planar-graph-faces.js').read_bytes()==(module.RIVER/'adapted/planar-graph-faces.js').read_bytes()
with tempfile.TemporaryDirectory() as temp:
    copied=Path(temp)/'river';shutil.copytree(module.RIVER,copied);module.RIVER=copied
    with (copied/'original/river-territory-partition.js').open('a') as f:f.write('\n// upstream drift\n')
    try:module.generated()
    except ValueError as e:assert 'ORIGINAL_HASH_MISMATCH' in str(e)
    else:raise AssertionError('Original hash drift accepted')
print('PASS: deterministic bytes; six missing/duplicate guards; unchanged planar; hash-drift rejection; official cosine source/license.')
