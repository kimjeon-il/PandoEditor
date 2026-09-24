#!/usr/bin/env python3
"""Generate a tiny v4/v5 hydro dataset with the web build's binary encoding.

The encode_* routines and binary layouts follow world-map/tools/build-hydro-tiles.py
at c0bd31d. This generator avoids the production builder's GIS dependencies.
"""
from __future__ import annotations

import gzip
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "tests/fixtures/web-hydro"
OLD = ROOT / "v0.13.0"
NEW = ROOT / "v0.13.1"
STAGES = [(8, 4), (16, 8), (32, 16), (64, 32)]


def uvar(value: int) -> bytes:
    out = bytearray()
    while value >= 0x80:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    out.append(value)
    return bytes(out)


def svar(value: int) -> bytes:
    return uvar((value << 1) ^ (value >> 31))


def line(points: list[list[float]]) -> bytes:
    out = bytearray(uvar(len(points)))
    previous_x = previous_y = 0
    for index, (lon, lat) in enumerate(points):
        x, y = round(lon * 1_000_000), round(lat * 1_000_000)
        out += svar(x if index == 0 else x - previous_x)
        out += svar(y if index == 0 else y - previous_y)
        previous_x, previous_y = x, y
    return bytes(out)


def geometry(value: dict) -> bytes:
    kind, coords = value["type"], value["coordinates"]
    if kind == "LineString":
        return uvar(1) + line(coords)
    if kind == "MultiLineString":
        return uvar(len(coords)) + b"".join(line(part) for part in coords)
    polygons = [coords] if kind == "Polygon" else coords
    return uvar(len(polygons)) + b"".join(
        uvar(len(rings)) + b"".join(line(ring) for ring in rings)
        for rings in polygons
    )


def width_profile(feature: dict) -> bytes:
    if feature["category"] != "river":
        return b""
    parts = [feature["geometry"]["coordinates"]] if feature["geometry"]["type"] == "LineString" else feature["geometry"]["coordinates"]
    out = bytearray(uvar(len(parts)))
    for widths, points in zip(feature["widths"], parts):
        assert len(widths) == len(points)
        values = [round(width * 1000) for width in widths]
        out += uvar(len(values)) + uvar(values[0])
        for before, after in zip(values, values[1:]):
            out += svar(after - before)
    return bytes(out)


def bounds(value: dict) -> list[float]:
    def flatten(coordinates):
        if len(coordinates) == 2 and isinstance(coordinates[0], (float, int)):
            yield coordinates
        else:
            for item in coordinates:
                yield from flatten(item)
    points = list(flatten(value["coordinates"]))
    return [min(p[0] for p in points), min(p[1] for p in points),
            max(p[0] for p in points), max(p[1] for p in points)]


def asset(path: Path, url: str) -> dict:
    data = path.read_bytes()
    return {"url": url, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def gz(data: bytes) -> bytes:
    return gzip.compress(data, compresslevel=9, mtime=0)


def feature(fid: int, logical: int, category: str, stage: int, name: str,
            coordinates: dict, widths=None, flags=0, fragment=0, fragments=1) -> dict:
    return {"fid": fid, "logicalFid": logical, "awId": f"fixture:{logical}",
            "category": category, "stage": stage, "name": name, "geometry": coordinates,
            "widths": widths, "flags": flags, "fragmentIndex": fragment,
            "fragmentCount": fragments, "bounds": bounds(coordinates),
            "layerId": "rivers_hydro" if category == "river" else "lakes_natural_earth",
            "sourceId": str(fid * 100), "source": "fixture",
            "systemId": f"system:{logical}", "role": "mainstem" if category == "river" else ""}


FEATURES = [
    feature(1, 1, "river", 0, "Normal", {"type": "LineString", "coordinates": [[0, 0], [1, 0.5], [2, 1]]}, [[0.8, 1.2, 1.6]]),
    feature(2, 2, "river", 1, "Border", {"type": "MultiLineString", "coordinates": [[[10, 0], [11, 1]], [[11, 1], [12, 2]]]}, [[0.5, 0.9], [0.9, 1.3]], flags=1),
    feature(3, 3, "lake", 2, "Simple", {"type": "Polygon", "coordinates": [[[20, 0], [22, 0], [22, 2], [20, 2], [20, 0]]]}),
    feature(4, 4, "lake", 3, "Hole", {"type": "Polygon", "coordinates": [[[30, 0], [34, 0], [34, 4], [30, 4], [30, 0]], [[31, 1], [31, 3], [33, 3], [33, 1], [31, 1]]]}),
    feature(5, 5, "river", 2, "Split", {"type": "LineString", "coordinates": [[40, 0], [41, 1]]}, [[0.6, 0.8]], fragment=0, fragments=2),
    feature(6, 5, "river", 2, "Split", {"type": "LineString", "coordinates": [[41, 1], [42, 2]]}, [[0.8, 1.0]], fragment=1, fragments=2),
]


def build() -> None:
    (OLD / "shards").mkdir(parents=True, exist_ok=True)
    NEW.mkdir(parents=True, exist_ok=True)
    shard = bytearray()
    packs = []
    tiles = {}
    logical = {}
    metadata = []
    for pack_id, item in enumerate(FEATURES):
        shape = geometry(item["geometry"])
        widths = width_profile(item)
        kind = {"LineString": 1, "MultiLineString": 2, "Polygon": 3, "MultiPolygon": 4}[item["geometry"]["type"]]
        record = struct.pack("<IIBBBBHHf4iII", item["fid"], item["logicalFid"],
                             1 if item["category"] == "river" else 2,
                             item["stage"], kind, item["flags"], item["fragmentIndex"],
                             item["fragmentCount"], item["widths"][0][0] if widths else 1.0,
                             *(round(v * 1_000_000) for v in item["bounds"]),
                             len(shape), len(widths)) + shape + widths
        packed = gz(struct.pack("<4sHHI", b"AWHF", 4, item["stage"], 1) + record)
        offset = len(shard)
        shard += packed
        packs.append((pack_id, 0, offset, len(packed), item["stage"]))
        logical.setdefault(item["logicalFid"], []).append(pack_id)
        columns, rows = STAGES[item["stage"]]
        x = max(0, min(columns - 1, int((item["bounds"][0] + 180) / 360 * columns)))
        y = max(0, min(rows - 1, int((90 - item["bounds"][3]) / 180 * rows)))
        tiles.setdefault((item["stage"], x, y), []).append(pack_id)
        metadata.append({key: item[key] for key in ("fid", "logicalFid", "awId", "name",
            "layerId", "category", "stage", "flags", "fragmentIndex", "fragmentCount",
            "bounds", "systemId", "role")})
        metadata[-1]["bounds"] = [round(v * 1_000_000) for v in item["bounds"]]
    shard_path = OLD / "shards/s0.bin"
    shard_path.write_bytes(shard)
    index = bytearray(struct.pack("<4sHHIII", b"AWI4", 4, 0, len(tiles), len(logical), len(packs)))
    for (stage, x, y), ids in sorted(tiles.items()):
        index += struct.pack("<BHHH", stage, x, y, len(ids))
        index += b"".join(struct.pack("<I", pack_id) for pack_id in ids)
    for logical_fid, ids in sorted(logical.items()):
        index += struct.pack("<IH", logical_fid, len(ids))
        index += b"".join(struct.pack("<I", pack_id) for pack_id in ids)
    for pack_id, shard_id, offset, size, stage in packs:
        index += struct.pack("<IHII B", pack_id, shard_id, offset, size, stage)
    index_path = OLD / "index.bin.gz"
    index_path.write_bytes(gz(index))
    detail_path = OLD / "metadata-detail.json.gz"
    detail_path.write_bytes(gz(json.dumps({"version": 5, "features": [
        {"fid": f["fid"], "sourceId": f["sourceId"], "source": f["source"]}
        for f in FEATURES]}, separators=(",", ":")).encode()))
    core_path = NEW / "metadata-core.json.gz"
    core_path.write_bytes(gz(json.dumps({"version": 5, "features": metadata},
                                        separators=(",", ":")).encode()))
    manifest = {
        "version": "0.13.1", "dataset": "fixture", "schema": "pandolab-water-shards-v5",
        "crs": "EPSG:4326",
        "stages": [{"id": i, "minZoom": zoom, "columns": grid[0], "rows": grid[1]}
                   for i, (zoom, grid) in enumerate(zip((6, 6.7, 7, 7.5), STAGES))],
        "index": {**asset(index_path, "../v0.13.0/index.bin.gz"),
                  "tileCount": len(tiles), "logicalFeatureCount": len(logical)},
        "metadata": {"version": 5, "featureCount": len(FEATURES),
                     "core": asset(core_path, "metadata-core.json.gz"),
                     "detail": {**asset(detail_path, "../v0.13.0/metadata-detail.json.gz"), "lazy": True}},
        "shards": [{"id": 0, **asset(shard_path, "../v0.13.0/shards/s0.bin"), "packs": len(packs)}],
        "layers": [{"id": "rivers_hydro", "category": "river", "locked": True},
                   {"id": "lakes_natural_earth", "category": "lake", "locked": True}],
    }
    (NEW / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    build()
