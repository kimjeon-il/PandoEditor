#!/usr/bin/env python3
"""Reproduce the compact country subset from the immutable full GeoJSON."""
from pathlib import Path
from hashlib import sha256
import argparse,json
parser=argparse.ArgumentParser();parser.add_argument('country_geojson');parser.add_argument('--check',action='store_true');args=parser.parse_args()
fixture=Path(__file__).resolve().parents[3]/'tests/fixtures/web-m972-river'
pin=json.loads((fixture/'manifest.json').read_text())['countrySource']
data=Path(args.country_geojson).read_bytes()
if sha256(data).hexdigest()!=pin['sha256']:raise SystemExit('Original country source hash mismatch')
subset={'type':'FeatureCollection','features':[f for f in json.loads(data)['features'] if f['id'] in pin['selectedIds']]}
encoded=(json.dumps(subset,separators=(',',':'),ensure_ascii=False)+'\n').encode()
if sha256(encoded).hexdigest()!=pin['subsetSha256']:raise SystemExit('Country subset regeneration mismatch')
if args.check:
    if (fixture/'countries.json').read_bytes()!=encoded:raise SystemExit('Checked-in country subset differs')
else:(fixture/'countries.json').write_bytes(encoded)
print('Pinned country subset verified:',len(encoded),'bytes')
