#!/usr/bin/env python3
"""Audit the provisional 1914 German–Danish boundary, stones 1–26.

An audit is NOT a certification against a period map. Modern parish/ejerlav
boundaries are cross-checks only, not replacements for 1865/1914 evidence.
"""
import argparse
import datetime as dt
import json
import math
from pathlib import Path

import requests
from pyproj import Transformer
from shapely.geometry import LineString, Point, shape
from shapely.ops import substring, transform

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tools/historical-library/sources/german-empire-1914/north-schleswig-001-026"
BORDER = ROOT / "tools/historical-library/land-borders"
PREFIX = "german-denmark-1914-north-schleswig-001-026"
EJERLAV_PATH = SOURCE / "seem-by-ejerlav-current.geojson"
EJERLAV_META_PATH = SOURCE / "seem-by-ejerlav-current.provenance.json"
EJERLAV_URL = "https://demo.geoinfo.dk/server/rest/services/Matriklen_Hele_DK/MapServer/6/query"
EJERLAV_CODE = "1300755"
TO_METRIC = Transformer.from_crs("EPSG:4326", "EPSG:25832", always_xy=True).transform


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def fetch_ejerlav():
    params = {
        "where": "ejerlavskode = '1300755'",
        "outFields": "OBJECTID,ejerlavskode,ejerlavsnavn,lokalid,virkningfra",
        "returnGeometry": "true",
        "outSR": "4326",
        "f": "geojson",
    }
    response = requests.get(
        EJERLAV_URL,
        params=params,
        headers={"User-Agent": "PandoEditor-HistoricalGIS/1.0"},
        timeout=120,
    )
    response.raise_for_status()
    data = response.json()
    features = data.get("features")
    if not isinstance(features, list) or len(features) != 1:
        raise RuntimeError(f"Expected one Seem By ejerlav polygon, received {len(features) if isinstance(features, list) else 'invalid'}")
    properties = features[0].get("properties", {})
    if str(properties.get("ejerlavskode")) != EJERLAV_CODE:
        raise RuntimeError(f"Unexpected cadastral code: {properties!r}")
    poly = shape(features[0]["geometry"])
    if poly.is_empty or poly.geom_type not in ("Polygon", "MultiPolygon"):
        raise RuntimeError("Seem By cadastral geometry missing or invalid")
    # Store the source response without passing it off as a historical polygon.
    write_json(EJERLAV_PATH, data)
    write_json(EJERLAV_META_PATH, {
        "dataset": "Matriklen - ejerlav (modern cadastral boundaries)",
        "historicalBoundary": False,
        "sourceUrl": EJERLAV_URL,
        "request": params,
        "fetchedAtUtc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "ejerlavCode": EJERLAV_CODE,
        "ejerlavName": properties.get("ejerlavsnavn"),
        "warning": "Modern cadastral geometry; must not establish the 1914 border without a period map.",
    })
    return data


def stat_distances(line, boundary, length_km):
    start = max(0.0, line.length - length_km * 1000)
    tail = substring(line, start, line.length)
    count = max(2, int(math.ceil(tail.length / 100)) + 1)
    distances = sorted(tail.interpolate(i / (count - 1), normalized=True).distance(boundary)
                       for i in range(count))
    return {
        "lastKm": length_km,
        "sampleSpacingApproxM": round(tail.length / (count - 1), 2),
        "samples": count,
        "medianDistanceM": round(distances[len(distances) // 2], 2),
        "p90DistanceM": round(distances[min(len(distances) - 1, int((count - 1) * 0.9))], 2),
        "maximumDistanceM": round(distances[-1], 2),
        "within25mFraction": round(sum(d <= 25 for d in distances) / count, 4),
        "within100mFraction": round(sum(d <= 100 for d in distances) / count, 4),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fetch-seem-ejerlav", action="store_true")
    parser.add_argument("--require-seem-ejerlav", action="store_true")
    args = parser.parse_args()

    if args.fetch_seem_ejerlav:
        fetch_ejerlav()

    geo = read_json(BORDER / f"{PREFIX}.geojson")
    diagnostic = read_json(BORDER / f"{PREFIX}.diagnostics.json")
    collected = read_json(SOURCE / "collection-summary.json")
    features = geo.get("features", [])
    if len(features) != 1:
        raise RuntimeError("Expected exactly one provisional border feature")
    feature = features[0]
    if feature.get("properties", {}).get("doNotTreatAsFinal") is not True:
        raise RuntimeError("Provisional boundary must not be marked as final")
    if feature.get("geometry", {}).get("type") != "LineString":
        raise RuntimeError("Expected one continuous LineString")

    line_ll = shape(feature["geometry"])
    line = transform(TO_METRIC, line_ll)
    if not line.is_simple:
        raise RuntimeError("Self-intersecting border candidate")
    length_km = line.length / 1000
    if abs(length_km - diagnostic["chosenLengthKm"]) > 0.05:
        raise RuntimeError("Generated border length diverges from diagnostics")
    if len(line_ll.coords) != diagnostic["coordinateCount"]:
        raise RuntimeError("Generated border vertex count diverges from diagnostics")

    stone_1 = None
    moved_ids = {2, 11, 22, 28, 30, 31}
    marker_rows = []
    for node in collected.get("candidateNodes", []):
        tags = node.get("tags", {})
        label = " ".join(str(tags.get(k, "")) for k in ("alt_name", "inscription", "name"))
        import re
        m = re.search(r"(?:nr\.?|no\.?)\s*(\d{1,3})([a-zA-Z]?)", label, flags=re.I)
        if not m:
            continue
        number = int(m.group(1))
        suffix = m.group(2).lower()
        if number == 1 and not suffix:
            stone_1 = node
        location = transform(TO_METRIC, Point(node["lon"], node["lat"]))
        marker_rows.append({
            "stone": str(number) + suffix,
            "osmNodeId": node["id"],
            "currentPositionLonLat": [node["lon"], node["lat"]],
            "distanceFromCandidateM": round(location.distance(line), 2),
            "positionalControlAllowed": number == 1 and not suffix,
            "reason": ("Restored original location claimed by Grænseforeningen" if number == 1 and not suffix
                       else "Relocated/display stone; do not snap the historic line"),
        })
    if stone_1 is None:
        raise RuntimeError("Missing stone 1 in collected source nodes")

    start = transform(TO_METRIC, Point(stone_1["lon"], stone_1["lat"]))
    start_error = line.coords[0][0] - start.x, line.coords[0][1] - start.y
    if math.hypot(*start_error) > 0.1:
        raise RuntimeError("Start is no longer anchored to original stone 1 reference")

    end_ll = diagnostic["endpointGelsA"]
    end = transform(TO_METRIC, Point(*end_ll))
    if end.distance(Point(line.coords[-1])) > 0.1:
        raise RuntimeError("Candidate endpoint diverges from recorded Gels Å reference")

    out = {
        "schemaVersion": 1,
        "scope": "German Empire–Denmark 1914, boundary stones 1–26",
        "referenceDate": feature["properties"].get("referenceDate"),
        "candidateStatus": "working-master-needs-historical-map-review",
        "historicalMapOverlayReviewed": False,
        "commissionProtocolUrl": "https://da.wikisource.org/wiki/Freden_i_Wien_(1864)_Gr%C3%A6nsereguleringskommissionen",
        "textRule": {
            "stone1to22": "South boundary of Vester Vedsted, Ribe and Seem parishes, following a large water ditch",
            "stone22to26": "Eastern PARISH boundary of Seem By (historical wording: 'den østlige Sognegrænse af Seem By'); verify modern parish and cadastral boundaries, do not equate them a priori",
            "terminal": "Gels Å at Gelsbro, stone 26 sector",
        },
        "geometry": {
            "lengthKm": round(length_km, 3),
            "vertexCount": len(line_ll.coords),
            "simple": line_ll.is_simple,
            "startMatchesStone1M": round(math.hypot(*start_error), 4),
            "endMatchesRecordedGelsAReferenceM": round(end.distance(Point(line.coords[-1])), 4),
        },
        "modernMarkerChecks": marker_rows,
        "modernCadastralCheck": {
            "source": EJERLAV_URL,
            "ejerlavCode": EJERLAV_CODE,
            "historicalEvidence": False,
            "status": "not_collected",
        },
        "outstanding": [
            "Georeference and inspect pre-1914 Prussian 1:25,000 sheets (1877–1912).",
            "Georeference and inspect Danish high/low målebordsblade in Danish-side coverage.",
            "Locate original stone 22 and assess Seem By vs modern Seem Sogn, especially the terminal arc.",
            "Check the western coastline/stone 1 against contemporary (1914) Ribe dike construction.",
            "Do not propagate into final German Empire polygon until the disputed stretches are reviewed.",
        ],
    }
    # Separate the historical/legal parish-boundary hypothesis from the
    # 'Seem By, Seem' modern cadastral subdivision. Only the former is
    # explicitly called a *Sognegrænse* in the 1865 protocol.
    dagi = read_json(SOURCE / "dagi-current-parishes-sector.geojson")
    seem = [f for f in dagi.get("features", [])
            if f.get("properties", {}).get("navn") == "Seem"]
    if len(seem) != 1:
        raise RuntimeError(f"Expected one modern Seem parish, got {len(seem)}")
    seem_boundary = transform(TO_METRIC, shape(seem[0]["geometry"]).boundary)
    out["modernParishCheck"] = {
        "source": "https://demo.geoinfo.dk/server/rest/services/DAGI_Hele_DK/MapServer/1/query",
        "name": "Seem",
        "historicalEvidence": False,
        "status": "measured_against_2024_current_parish_only",
        "lastSections": [stat_distances(line, seem_boundary, km)
                         for km in (1, 2, 3, 5, 8)],
    }

    if EJERLAV_PATH.exists():
        source_poly = read_json(EJERLAV_PATH)
        cadastral = shape(source_poly["features"][0]["geometry"])
        boundary = transform(TO_METRIC, cadastral.boundary)
        out["modernCadastralCheck"] = {
            "source": EJERLAV_URL,
            "ejerlavCode": EJERLAV_CODE,
            "historicalEvidence": False,
            "status": "measured_not_historically_verified",
            "ejerlavName": source_poly["features"][0].get("properties", {}).get("ejerlavsnavn"),
            "lastSections": [stat_distances(line, boundary, km) for km in (1, 2, 3, 5, 8)],
        }
    elif args.require_seem_ejerlav:
        raise RuntimeError("Seem By polygon required but no cached source is present")

    write_json(BORDER / f"{PREFIX}.audit.json", out)
    print(json.dumps({
        "status": out["candidateStatus"],
        "geometry": out["geometry"],
        "markerChecks": len(marker_rows),
        "cadastralStatus": out["modernCadastralCheck"]["status"],
        "output": str(BORDER / f"{PREFIX}.audit.json"),
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
