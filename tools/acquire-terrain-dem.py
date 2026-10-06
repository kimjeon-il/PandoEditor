#!/usr/bin/env python3
"""Acquire immutable published DEM assets, never generate terrain.

The production inventory retains the existing PhysicalDataStore schema. The
separate provenance ledger records every Git blob, actual byte count and SHA256.
Existing cache files are verified read-only; corrupt originals are never replaced.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile
import time
import urllib.request

SOURCE_COMMIT = "c3c18d167dae2dd9639844e5174dd68f745c6832"
WEB_COMMIT = "5649c307da24d0965d63bc8c00206aed9b9d3438"
REPOSITORY = "kimjeon-il/world-map-terrain-v0.13.0"
RAW_BASE = f"https://raw.githubusercontent.com/{REPOSITORY}/{SOURCE_COMMIT}/"
TREE_URL = f"https://api.github.com/repos/{REPOSITORY}/git/trees/{SOURCE_COMMIT}?recursive=1"
DATASET = "terrain-dem"
DATASET_VERSION = "c3c18d1"
MANIFEST_PATH = "terrain/v0.13.3/manifest.json"
TINT_PATH = "terrain/v0.13.3/tint.webp"
MANIFEST_SHA256 = "6ae3b1f21bbcb3708428d8bf9c5b7edf75b4721ed242e9deab65b22ac385dca7"
TINT_SHA256 = "1ae4c70e05494917c11d3c6b32869db61bf4f9e7e4b0239f9b4bd2b9bec582f3"
AGGREGATE_SHA256 = "622a6d272bdda6b507a1f4ba49794fe9ef41aca06f29d8b38e24114caa50d023"
RUNTIME_BYTES = 854965132
RUNTIME_COUNT = 1282
PUBLIC_BASE = "https://kimjeon-il.github.io/world-map-terrain-v0.13.0/"
TILE_PATTERN = re.compile(r"^terrain/v0\.13\.0/[0-5]/[0-9]+-[0-9]+\.webp$")


class VerificationError(ValueError):
    pass


def verify_stream(stream, item, output=None, expected_sha256=None):
    """Verify full actual bytes against the fixed Git tree, optionally SHA256."""
    size = item["size"]
    if not isinstance(size, int) or isinstance(size, bool) or size <= 0:
        raise VerificationError(f"{item['path']}: invalid expected size")
    git_hash = hashlib.sha1(b"blob " + str(size).encode("ascii") + b"\0")
    digest = hashlib.sha256()
    count = 0
    prefix = bytearray()
    while True:
        block = stream.read(256 * 1024)
        if not block:
            break
        count += len(block)
        if count > size:
            raise VerificationError(f"{item['path']}: oversized response {count} > {size}")
        if len(prefix) < 64:
            prefix.extend(block[:64-len(prefix)])
        git_hash.update(block); digest.update(block)
        if output is not None:
            output.write(block)
    if count != size:
        raise VerificationError(f"{item['path']}: empty/truncated response {count} != {size}")
    if item["path"].endswith(".webp") and not (prefix[:4] == b"RIFF" and prefix[8:12] == b"WEBP"):
        raise VerificationError(f"{item['path']}: response is not WebP (HTML/error payload)")
    if git_hash.hexdigest() != item["sha"]:
        raise VerificationError(f"{item['path']}: Git blob mismatch")
    sha256 = digest.hexdigest()
    if expected_sha256 is not None and sha256 != expected_sha256:
        raise VerificationError(f"{item['path']}: SHA256 mismatch")
    return {"path": item["path"], "bytes": count, "gitBlob": git_hash.hexdigest(), "sha256": sha256}


def open_url(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers={
        "User-Agent": "PandoEditor-pinned-terrain-acquisition/1", "Accept-Encoding": "identity"}), timeout=90)


def acquire_asset(item, destination, url, retries=2, expected_sha256=None):
    destination = Path(destination)
    if destination.exists():
        with destination.open("rb") as stream:
            result = verify_stream(stream, item, expected_sha256=expected_sha256)
        return dict(result, reused=True)
    destination.parent.mkdir(parents=True, exist_ok=True)
    errors = []
    for attempt in range(retries+1):
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=destination.parent, prefix=destination.name+".", suffix=".part", delete=False) as output:
                temporary = Path(output.name)
                with open_url(url) as response:
                    if "html" in response.headers.get("Content-Type", "").lower():
                        raise VerificationError(f"{item['path']}: HTML Content-Type")
                    result = verify_stream(response, item, output, expected_sha256)
            # The verified file is published atomically and exclusively.
            if destination.exists():
                raise VerificationError(f"{item['path']}: destination appeared during acquisition")
            os.link(temporary, destination)
            return dict(result, reused=False)
        except (OSError, ValueError) as error:
            errors.append(str(error))
            if attempt < retries:
                time.sleep(min(2**attempt, 4))
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
    raise VerificationError(f"{item['path']}: failed after {retries+1} attempts: " + " | ".join(errors))


def runtime_entries(tree):
    if tree.get("sha") != SOURCE_COMMIT or tree.get("truncated") is not False:
        raise VerificationError("tree source pin mismatch or truncated tree")
    selected = []
    seen = set()
    for item in tree.get("tree", []):
        path = item.get("path", "")
        if not (TILE_PATTERN.fullmatch(path) or path in (MANIFEST_PATH, TINT_PATH)):
            continue
        if path in seen or item.get("type") != "blob" or not re.fullmatch(r"[0-9a-f]{40}", item.get("sha", "")):
            raise VerificationError(f"{path}: duplicate/non-blob/invalid Git identity")
        if not isinstance(item.get("size"), int) or isinstance(item.get("size"), bool) or item["size"] <= 0:
            raise VerificationError(f"{path}: missing/invalid Git size")
        seen.add(path); selected.append(item)
    required = {MANIFEST_PATH, TINT_PATH}
    for i in range(6):
        columns = math.ceil(1350*2**i / 1024); rows = math.ceil(675*2**i / 1024)
        required.update(f"terrain/v0.13.0/{i}/{c}-{r}.webp" for r in range(rows) for c in range(columns))
    if seen != required:
        raise VerificationError("runtime paths mismatch: missing=" + json.dumps(sorted(required-seen)) + "; unexpected=" + json.dumps(sorted(seen-required)))
    if len(selected) != RUNTIME_COUNT or sum(item["size"] for item in selected) != RUNTIME_BYTES:
        raise VerificationError("fixed runtime count/size mismatch")
    return sorted(selected, key=lambda item: item["path"])


def validate_manifest(manifest):
    required = {"version": "0.13.3", "representation": "dem-relief-v1", "crs": "EPSG:4326",
                "extent": [-180, -90, 180, 90], "gutter": 1, "tileFormat": "lossless WebP RGBA",
                "registration": "cell-center", "sourceGridOrigin": [-180, 90],
                "assetsSha256": AGGREGATE_SHA256,
                "urlTemplate": PUBLIC_BASE+"terrain/v0.13.0/{level}/{column}-{row}.webp"}
    for field, value in required.items():
        if manifest.get(field) != value:
            raise VerificationError(f"manifest {field} mismatch")
    if manifest.get("channels") != {"r": "encoded elevation high byte", "g": "encoded elevation low byte", "b": "precomputed hillshade", "a": "255"}:
        raise VerificationError("manifest channel contract mismatch")
    elevation = manifest.get("elevation", {})
    for field, value in {"decode": "R*256+G-12000", "biasMeters": 12000, "spacingMeters": 1, "validEncodedRange": [0, 65535]}.items():
        if elevation.get(field) != value:
            raise VerificationError(f"manifest elevation {field} mismatch")
    resolution = manifest.get("sourceResolutionDegrees", [])
    if len(resolution) != 2 or any(abs(float(value)-1/120) > 1e-12 for value in resolution):
        raise VerificationError("manifest source resolution mismatch")
    tint = manifest.get("tint", {})
    for field, value in {"url": PUBLIC_BASE+TINT_PATH, "width": 4096, "height": 2048, "sha256": TINT_SHA256}.items():
        if tint.get(field) != value:
            raise VerificationError(f"manifest tint {field} mismatch")
    levels = manifest.get("levels", [])
    if len(levels) != 6:
        raise VerificationError("manifest must contain 6 LODs")
    for i, level in enumerate(levels):
        expected = {"id": i, "width": 1350*2**i, "height": 675*2**i, "tileSize": 1024,
                    "columns": math.ceil(1350*2**i/1024), "rows": math.ceil(675*2**i/1024)}
        if level != expected:
            raise VerificationError(f"manifest LOD {i} grid mismatch")
    return manifest


def aggregate_hash(levels, rows):
    digest = hashlib.sha256()
    try:
        for level in levels:
            for row in range(level["rows"]):
                for column in range(level["columns"]):
                    relative = f"{level['id']}/{column}-{row}.webp"
                    digest.update(relative.encode("utf-8"))
                    digest.update(bytes.fromhex(rows["terrain/v0.13.0/"+relative]["sha256"]))
        digest.update(bytes.fromhex(rows[TINT_PATH]["sha256"]))
    except (KeyError, ValueError) as error:
        raise VerificationError(f"aggregate missing/invalid actual file digest: {error}") from error
    return digest.hexdigest()


def write_new_json(path, value):
    """Never silently replace previous evidence or source inventory."""
    payload = (json.dumps(value, ensure_ascii=False, indent=2)+"\n").encode("utf-8")
    path = Path(path)
    if path.exists():
        if path.read_bytes() != payload:
            raise VerificationError(f"evidence already exists with different bytes: {path}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(payload)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--tree-json", type=Path, help="Saved official fixed-tree API response (all bytes still verified)")
    parser.add_argument("--workers", type=int, choices=range(1, 5), default=4)
    parser.add_argument("--retries", type=int, choices=range(0, 4), default=2)
    args = parser.parse_args(argv)
    started = time.time()
    tree = json.loads(args.tree_json.read_bytes()) if args.tree_json else json.load(open_url(TREE_URL))
    items = runtime_entries(tree)
    write_new_json(args.evidence/"source-tree.json", tree)
    root = args.cache/DATASET/DATASET_VERSION
    by_path = {item["path"]: item for item in items}
    manifest_row = acquire_asset(by_path[MANIFEST_PATH], root/MANIFEST_PATH, RAW_BASE+MANIFEST_PATH,
                                args.retries, MANIFEST_SHA256)
    manifest = validate_manifest(json.loads((root/MANIFEST_PATH).read_bytes()))
    verified = {MANIFEST_PATH: manifest_row}; failures = []
    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        futures = {executor.submit(acquire_asset, item, root/item["path"], RAW_BASE+item["path"], args.retries,
                                   TINT_SHA256 if item["path"] == TINT_PATH else None): item["path"]
                   for item in items if item["path"] != MANIFEST_PATH}
        for future in as_completed(futures):
            path = futures[future]
            try:
                verified[path] = future.result()
                if len(verified) % 50 == 0 or len(verified) == RUNTIME_COUNT:
                    print(json.dumps({"verified": len(verified), "bytes": sum(row["bytes"] for row in verified.values())}), flush=True)
            except Exception as error:
                failures.append({"path": path, "error": str(error)})
    missing = sorted(set(by_path)-set(verified))
    if failures or missing:
        report = {"schema": "pandoeditor-terrain-acquisition-failure", "sourceCommit": SOURCE_COMMIT,
                  "verifiedCount": len(verified), "missing": missing, "failures": sorted(failures, key=lambda row: row["path"])}
        write_new_json(args.evidence/f"failure-{time.time_ns()}.json", report)
        raise VerificationError(json.dumps(report, ensure_ascii=False))
    aggregate = aggregate_hash(manifest["levels"], verified)
    if aggregate != AGGREGATE_SHA256:
        raise VerificationError(f"aggregate mismatch: {aggregate} != {AGGREGATE_SHA256}")
    rows = [verified[item["path"]] for item in items]
    inventory = {"schema": "pandoeditor-physical-inventory", "version": 1,
                 "dataset": DATASET, "datasetVersion": DATASET_VERSION, "sourceCommit": SOURCE_COMMIT,
                 "baseUrl": RAW_BASE, "assets": [{"path": row["path"], "bytes": row["bytes"], "sha256": row["sha256"]} for row in rows]}
    # Acquisition timing/reuse are run evidence, not immutable content identity.
    ledger = {"schema": "pandoeditor-terrain-provenance", "version": 1, "sourceRepository": REPOSITORY,
              "sourceCommit": SOURCE_COMMIT, "webContractCommit": WEB_COMMIT,
              "treeTruncated": False, "fileCount": len(rows), "totalBytes": sum(row["bytes"] for row in rows),
              "aggregateAlgorithm": "manifest levels; row then column; UTF8 L/C-R.webp + binary tile SHA256; binary tint SHA256 last; no delimiter/manifest",
              "aggregateSha256": aggregate, "tintSha256": verified[TINT_PATH]["sha256"],
              "manifestSha256": verified[MANIFEST_PATH]["sha256"],
              "assets": [{key: row[key] for key in ("path", "bytes", "gitBlob", "sha256")} for row in rows]}
    write_new_json(args.evidence/"physical-inventory-terrain-dem-c3c18d1.json", inventory)
    write_new_json(args.evidence/"provenance-ledger.json", ledger)
    summary = {"sourceCommit": SOURCE_COMMIT, "fileCount": len(rows), "totalBytes": ledger["totalBytes"],
               "aggregateSha256": aggregate, "reusedCount": sum(row["reused"] for row in rows),
               "seconds": time.time()-started, "cacheRoot": str(root), "missing": []}
    write_new_json(args.evidence/f"run-{time.time_ns()}.json", summary)
    print(json.dumps(summary), flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
