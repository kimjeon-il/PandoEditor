#!/usr/bin/env python3
"""Discover public catalog metadata for the four 1–26 border map sheets.

No API secrets. Saves source metadata and public access observations only.
Does NOT download scans, infer pre-1914 revision from a survey date, or
certify candidate-border positions.
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from urllib.parse import urlparse

import requests

ROOT = Path(__file__).resolve().parents[1]
OUT = (ROOT / "tools/historical-library/land-borders/"
       "german-denmark-1914-north-schleswig-001-026.slub-discovery.json")
SHEETS = (ROOT / "tools/historical-library/land-borders/"
          "german-denmark-1914-north-schleswig-001-026.sheets.json")
ENDPOINTS = [
    "https://search.kartenforum.slub-dresden.de/vk20",
    "https://search-slub.pikobytes.de/vk20",
]
KEYWORDS = {
    "0619": ["Hügum", "Hugum"],
    "0717": ["Hvidding-Ufer"],
    "0718": ["Hvidding"],
    "0719": ["Spandet"],
}
FIELDS = [
    "map_id", "title", "title_long", "description", "time_period",
    "time_period_start", "time_period_end", "map_scale", "original_url",
    "online_resources", "permalink", "tms_url", "zoomify_url",
    "has_georeference", "geometry", "thumb_url",
]
HEADERS = {
    "User-Agent": "PandoEditor-HistoricalGIS/1.0 (historical map source audit)",
    "Accept": "application/json",
}


def safe_error(ex):
    return f"{type(ex).__name__}: {str(ex)[:260]}"


def stripped(record):
    source = {k: record.get(k) for k in FIELDS if k in record}
    geo = source.pop("geometry", None)
    if geo is not None:
        source["geometryType"] = geo.get("type") if isinstance(geo, dict) else type(geo).__name__
    # Publication status determined only with explicit date evidence, not text search.
    return source


def post_search(session, base, sheet):
    # SLUB index uses non-searchable source fields for textual metadata.
    # Follow the actual Kartenforum client: geo_shape geometry + date range.
    west, south, east, north = sheet["extentLonLat"]
    polygon = [[west, south], [east, south], [east, north],
               [west, north], [west, south]]
    query = {
        "size": 120,
        "_source": [f for f in FIELDS if f != "geometry"],
        "query": {"bool": {"filter": [
            {"geo_shape": {"geometry": {
                "relation": "intersects",
                "shape": {"type": "polygon", "coordinates": [polygon]},
            }}},
            {"range": {"time_period_start": {"lte": "1914-12-31"}}},
            {"range": {"time_period_end": {"gte": "1860-01-01"}}},
        ]}},
    }
    response = session.post(
        f"{base}/_search", json=query, headers=HEADERS, timeout=35)
    if not response.ok:
        raise requests.HTTPError(
            f"HTTP {response.status_code}: {response.text[:650]} for {base}", response=response)
    raw = response.json()
    matches = raw.get("hits", {}).get("hits", [])
    return [
        {"id": m.get("_id"), "score": m.get("_score"),
         "metadata": stripped(m.get("_source") or {})}
        for m in matches
    ]


def fold_ascii(value):
    import unicodedata
    return "".join(
        c for c in unicodedata.normalize("NFKD", str(value or "").lower()
                                        .replace("ø", "o"))
        if not unicodedata.combining(c)
    )


def main():
    argp = argparse.ArgumentParser()
    argp.add_argument("--strict", action="store_true",
                      help="Return nonzero if both public catalog endpoints are inaccessible")
    args = argp.parse_args()
    sheet_data = json.loads(SHEETS.read_text(encoding="utf-8"))
    session = requests.Session()

    discovery = {
        "schemaVersion": 1,
        "scope": "1–26 stones, 1914 German–Danish border",
        "referenceDate": "1914-07-31",
        "historicRasterObtained": False,
        "historicRasterComparedWithLine": False,
        "searchMethod": "Public SLUB Kartenforum ElasticSearch geo_shape and pre-1915 date filters (per official client)",
        "sourceSoftware": "https://github.com/slub/slub_web_kartenforum/blob/master/Build/src/util/apiEs.js",
        "indexedEditions": sheet_data["sheets"],
        "endpointObservations": [],
        "matches": {},
        "warnings": [
            "Catalog search matches are not evidence of original raster availability.",
            "All four index sheets have 1878–1880 catalog editions, but catalog dating does not verify raster content.",
            "Do not assume historical map coordinates equal the modern parish-based working candidate."
        ],
    }
    working = False
    for url in ENDPOINTS:
        record = {"searchUrl": url, "status": "not-reached"}
        for sheet in sheet_data["sheets"]:
            number = sheet["sheetId"]
            variants = KEYWORDS[number]
            try:
                hits = post_search(session, url, sheet)
                record["status"] = "searchable"
                working = True
                bucket = discovery["matches"].setdefault(number, [])
                candidates = discovery.setdefault("spatialContext", {})
                aliases = [fold_ascii(s) for s in variants]
                for hit in hits:
                    metadata = hit["metadata"]
                    title = fold_ascii(
                        " ".join(str(metadata.get(k) or "")
                                 for k in ("title", "title_long")))
                    if any(alias in title for alias in aliases):
                        bucket.append({"endpoint": url, **hit})
                if not bucket:
                    candidates[number] = hits[:15]
                record.setdefault("resultsPerSheet", {})[number] = {
                    "spatialHitsReturned": len(hits),
                    "exactTitleMatches": len(bucket)}
            except (requests.RequestException, ValueError) as exc:
                record["status"] = "unavailable"
                record["error"] = safe_error(exc)
                break
        discovery["endpointObservations"].append(record)
        if working:
            break

    OUT.write_text(json.dumps(discovery, ensure_ascii=False, indent=2) + "\n",
                   encoding="utf-8")
    print(json.dumps({
        "status": "catalog-searchable" if working else "catalog-not-accessible",
        "endpoints": discovery["endpointObservations"],
        "matchesPerSheet": {k: len(v) for k, v in discovery["matches"].items()},
        "historicRasterComparedWithLine": False,
        "output": str(OUT),
    }, ensure_ascii=False, indent=2))
    if args.strict and not working:
        raise SystemExit(2)


if __name__ == "__main__":
    main()
