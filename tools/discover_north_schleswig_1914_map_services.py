#!/usr/bin/env python3
"""Discover public catalog metadata for the four 1–26 border map sheets.

No API secrets. Saves source metadata and public access observations only.
Does NOT download scans, infer pre-1914 revision from a survey date, or
certify candidate-border positions.
"""
from __future__ import annotations

import argparse
import datetime as dt
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
    "0719": ["Spandet"],
    "0817": ["Kirkeby"],
    "0818": ["Bröns", "Brons", "Brøns"],
    "0819": ["Arrild"],
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


def post_search(session, base, keyword):
    query = {
        "size": 8,
        "_source": FIELDS,
        "query": {"multi_match": {
            "query": keyword,
            "fields": ["title^4", "title_long^3", "keywords^2", "description"],
            "type": "best_fields",
            "operator": "or"
        }},
    }
    response = session.post(
        f"{base}/_search", json=query, headers=HEADERS, timeout=25)
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
        "queriedAtUtc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "historicRasterObtained": False,
        "historicRasterComparedWithLine": False,
        "searchMethod": "Public SLUB Kartenforum ElasticSearch index, multi_match over descriptive fields",
        "sourceSoftware": "https://github.com/slub/slub_web_kartenforum/blob/master/Build/src/util/apiEs.js",
        "indexedEditions": sheet_data["sheets"],
        "endpointObservations": [],
        "matches": {},
        "warnings": [
            "Catalog search matches are not evidence of original raster availability.",
            "1919 Bröns is ineligible as a 1914 reference without a verified older edition.",
            "Do not assume historical map coordinates equal the modern parish-based working candidate."
        ],
    }
    working = False
    for url in ENDPOINTS:
        record = {"searchUrl": url, "status": "not-reached"}
        for number, variants in KEYWORDS.items():
            for name in variants:
                try:
                    hits = post_search(session, url, name)
                    record["status"] = "searchable"
                    working = True
                    bucket = discovery["matches"].setdefault(number, [])
                    ids = {i.get("id") for i in bucket}
                    for hit in hits:
                        if hit.get("id") not in ids:
                            bucket.append({"searched": name, "endpoint": url, **hit})
                            ids.add(hit.get("id"))
                    if hits:
                        break
                except (requests.RequestException, ValueError) as exc:
                    record["status"] = "unavailable"
                    record["error"] = safe_error(exc)
                    break
            if record["status"] == "unavailable":
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
