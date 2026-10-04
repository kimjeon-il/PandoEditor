#!/usr/bin/env python3
"""Deterministically generate the six-expression syntax bridge.
--check never writes. Original vendor bytes and each expression count fail closed.
"""
from pathlib import Path
from hashlib import sha256
import argparse, json
ROOT = Path(__file__).resolve().parents[3]
RIVER = ROOT / 'assets/geometry/river'
PINS = {
 'river-territory-partition.js':'18b32eb7db238beaed99bce8bac980e547a48c785a7bd2071fa56fa63f51db24',
 'planar-graph-faces.js':'283da7701c21cb80e4fd9ef97e8f68e69a8d6f0ec9fc611ee8c23cab45d6c804',
 'polygon-geometry.js':'cc987c4076861a02a5d60720ebf536908175a9f1a605a86701ae4cf50c3a3fb5',
}
replacements = [
 ("""items.push({
        ...candidate,
        countryId,
        polygonIndex,
        sourcePolygonIndex,
        componentKey,
        partitionKind: 'river',
      })""", """items.push(__riverOwnDataMerge({}, candidate, {
        countryId,
        polygonIndex,
        sourcePolygonIndex,
        componentKey,
        partitionKind: 'river',
      }))"""),
 ("{ ...component, countryId, polygonIndex, sourcePolygonIndex, componentKey, partitionKind: 'original' }", "__riverOwnDataMerge({}, component, { countryId, polygonIndex, sourcePolygonIndex, componentKey, partitionKind: 'original' })"),
 ("{ ...segment, a, b, bounds: null }", "__riverOwnDataMerge({}, segment, { a, b, bounds: null })"),
 ("{ ...face, riverEdges: face.edgeIds.filter(id => activeRiverEdges.has(id)) }", "__riverOwnDataMerge({}, face, { riverEdges: face.edgeIds.filter(id => activeRiverEdges.has(id)) })"),
 ("{ ...row, geometryKey, areaM2: metricGeometryArea(row.geometry, workspace) }", "__riverOwnDataMerge({}, row, { geometryKey, areaM2: metricGeometryArea(row.geometry, workspace) })"),
 ("{ ...RIVER_TERRITORY_PARTITION_CONFIG, ...(config || {}) }", "__riverOwnDataMerge({}, RIVER_TERRITORY_PARTITION_CONFIG, (config || {}))"),
]

def syntax(source):
    for before, after in replacements:
        if source.count(before) != 1: raise ValueError('RIVER_SYNTAX_COUNT_MISMATCH: ' + before)
        source = source.replace(before, after)
    return source

def generated():
    originals = {name:(RIVER/'original'/name).read_bytes() for name in PINS}
    for name, data in originals.items():
        if sha256(data).hexdigest() != PINS[name]: raise ValueError('RIVER_ORIGINAL_HASH_MISMATCH: ' + name)
    outputs = {RIVER/'adapted/river-territory-partition.js':syntax(originals['river-territory-partition.js'].decode()).encode(),
               RIVER/'adapted/planar-graph-faces.js':originals['planar-graph-faces.js']}
    manifest={'webCommit':'53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47','originalSha256':PINS,
      'syntaxReplacements':[{'before':a,'after':b} for a,b in replacements],
      'generatedSha256':{str(path.relative_to(ROOT)):sha256(data).hexdigest() for path,data in outputs.items()}}
    outputs[RIVER/'provenance.json']=(json.dumps(manifest,indent=2)+'\n').encode()
    return outputs

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--check',action='store_true');args=parser.parse_args()
    for path,data in generated().items():
        if args.check:
            if not path.is_file() or path.read_bytes()!=data: raise SystemExit('GENERATED_DRIFT: '+str(path))
        else:
            path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    print('Verified guarded original sources, six replacements, unchanged planar module.')
