#!/usr/bin/env python3
"""Compute the exact pre-1914 Messtischblatt sheet crossings for the 1–26 candidate.

This is a grid/source inventory, NOT a historical-raster comparison. In
particular an edition published after 1914 must not be silently accepted
even if its original survey was earlier.
"""
import json
from pathlib import Path

from pyproj import Transformer
from shapely.geometry import LineString, MultiLineString, GeometryCollection, box, shape, mapping
from shapely.ops import transform, unary_union

ROOT = Path(__file__).resolve().parents[1]
BORDER = ROOT / "tools/historical-library/land-borders"
STEM = "german-denmark-1914-north-schleswig-001-026"
GEOJSON = BORDER / f"{STEM}.geojson"
OUT = BORDER / f"{STEM}.sheets.json"
OUT_FOOTPRINTS = BORDER / f"{STEM}.sheets.geojson"
OUT_PARTS = BORDER / f"{STEM}.sheet-segments.geojson"

# German Messtischblatt system, post-1937 row+column catalog notation.
# Grid start row 01 NORTH at 55deg54N, not 56degN.
# Verified against four public SLUB georeferenced WCS sheet footprints:
# df_dk_0010001_0717/0718/0719 cover approx 55.2–55.3N;
# df_dk_0010001_0619 covers approx 55.3–55.4N.
# https://search.kartenforum.slub-dresden.de/vk20/_search
GRID_NORTH = 55.9
GRID_WEST = 5.0 + 50.0 / 60.0
HEIGHT_DEG = 6.0 / 60.0
WIDTH_DEG = 10.0 / 60.0

# This is an index of *catalogued editions*, NOT a claim that those
# editions have been raster-accessed or georeferenced.
EDITION_CATALOG = {
    "0619": dict(title="Hügum", issueYear=1878, surveyYear=1878,
                 objectId="71051510", slubMapId="10006020"),
    "0717": dict(title="Hvidding-Ufer", issueYear=1880, surveyYear=1878,
                 objectId="71051521", slubMapId="10006009"),
    "0718": dict(title="Hvidding", issueYear=1880, surveyYear=1878,
                 objectId="71051522", slubMapId="10006008"),
    "0719": dict(title="Spandet", issueYear=1880, surveyYear=1878,
                 objectId="71051523", slubMapId="10006007"),
}
CATALOG_URL = "https://commons.wikimedia.org/wiki/Module:Messtischblatt/data"
TRANSFORMER = Transformer.from_crs("EPSG:4326", "EPSG:25832", always_xy=True).transform


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def lines_only(geom):
    if geom.is_empty:
        return []
    if isinstance(geom, LineString):
        return [geom] if len(geom.coords) > 1 and geom.length > 1e-10 else []
    if isinstance(geom, (MultiLineString, GeometryCollection)):
        result = []
        for g in geom.geoms:
            result.extend(lines_only(g))
        return result
    return []


def main():
    fc = read_json(GEOJSON)
    features = fc.get("features", [])
    if len(features) != 1 or features[0]["geometry"]["type"] != "LineString":
        raise RuntimeError("Expected exactly one LineString candidate")
    feature = features[0]
    if feature["properties"].get("doNotTreatAsFinal") is not True:
        raise RuntimeError("Do not certify unreviewed historical border")
    line = shape(feature["geometry"])
    metric = transform(TRANSFORMER, line)
    if not line.is_simple:
        raise RuntimeError("Candidate line self-intersects")

    minx, miny, maxx, maxy = line.bounds
    col_a = max(1, int((minx - GRID_WEST) / WIDTH_DEG) + 1)
    col_b = max(1, int((maxx - GRID_WEST) / WIDTH_DEG) + 1)
    row_a = max(1, int((GRID_NORTH - maxy) / HEIGHT_DEG) + 1)
    row_b = max(1, int((GRID_NORTH - miny) / HEIGHT_DEG) + 1)

    records = []
    polygons = []
    segments = []
    for row in range(row_a, row_b + 1):
        north = GRID_NORTH - (row - 1) * HEIGHT_DEG
        south = north - HEIGHT_DEG
        for col in range(col_a, col_b + 1):
            west = GRID_WEST + (col - 1) * WIDTH_DEG
            east = west + WIDTH_DEG
            square = box(west, south, east, north)
            parts = lines_only(line.intersection(square))
            if not parts:
                continue
            subparts_m = [transform(TRANSFORMER, p) for p in parts]
            length = sum(p.length for p in subparts_m)
            if length < 0.001:
                continue
            sheet_id = f"{row:02d}{col:02d}"
            catalog = EDITION_CATALOG.get(sheet_id)
            issue_year = catalog.get("issueYear") if catalog else None
            eligible = issue_year is not None and issue_year < 1914
            if issue_year is None:
                state = "catalog-edition-not-identified"
            elif eligible:
                state = "pre-1914-edition-catalogued-raster-not-reviewed"
            else:
                state = "post-1914-edition-not-eligible"
            record = {
                "sheetId": sheet_id,
                "title": catalog.get("title") if catalog else None,
                "publicationYear": issue_year,
                "surveyYear": catalog.get("surveyYear") if catalog else None,
                "publicationBeforeReferenceDate": eligible,
                "sourceReviewStatus": state,
                "candidateLengthKm": round(length / 1000.0, 5),
                "extentLonLat": [round(west, 9), round(south, 9),
                                 round(east, 9), round(north, 9)],
                "geometryType": "MultiLineString",
                "sourceCatalog": CATALOG_URL,
                "deutscheFotothek": (
                    "https://www.deutschefotothek.de/documents/obj/"
                    + catalog["objectId"] if catalog else None
                ),
                "slubGeoreferencedMapId": catalog.get("slubMapId") if catalog else None,
                "slubWmsCapabilities": (
                    "https://wms.kartenforum.slub-dresden.de/map/"
                    + catalog["slubMapId"]
                    + "?SERVICE=WMS&VERSION=1.3.0&REQUEST=GetCapabilities"
                    if catalog else None
                ),
            }
            records.append(record)
            polygons.append({"type": "Feature", "properties": record,
                             "geometry": mapping(square)})
            segments.append({"type": "Feature", "properties": {
                                 "sheetId": sheet_id,
                                 "publicationBeforeReferenceDate": eligible,
                                 "candidateLengthKm": record["candidateLengthKm"]},
                             "geometry": mapping(MultiLineString(parts))})

    if not records:
        raise RuntimeError("No Messtischblatt coverage computed")
    sum_length_m = sum(r["candidateLengthKm"] * 1000 for r in records)
    # Segments exactly on a sheet boundary could appear in two sheets;
    # fail closed rather than hiding overlap or a gap.
    delta_m = sum_length_m - metric.length
    if abs(delta_m) > 2:
        raise RuntimeError(
            f"Sheet coverage is not a partition: error {delta_m:.3f}m")
    has_outdated = any(not r["publicationBeforeReferenceDate"] for r in records)

    report = {
        "schemaVersion": 1,
        "referenceDate": "1914-07-31",
        "candidateId": feature["id"],
        "candidateStatus": "working-master-needs-historical-map-review",
        "comparisonMethod": "WGS84 6arcmin x 10arcmin grid; projected line lengths EPSG:25832",
        "referenceGrid": {
            "northLatitude": GRID_NORTH, "westLongitude": GRID_WEST,
            "heightDegrees": HEIGHT_DEG, "widthDegrees": WIDTH_DEG
        },
        "historicalRasterComparisonPerformed": False,
        "catalog": CATALOG_URL,
        "sheetCount": len(records),
        "candidateLengthKm": round(metric.length / 1000, 5),
        "sheetLengthSumKm": round(sum_length_m / 1000, 5),
        "sheetCoverageDifferenceM": round(delta_m, 3),
        "anyUnavailablePre1914Edition": has_outdated,
        "sheets": records,
        "warning": ("Corrected sheet-grid origin against original SLUB 1:25,000 WCS footprints; "
                    "geometric coverage and pre-1914 catalog dates alone do not validate the boundary."),
    }
    write_json(OUT, report)
    write_json(OUT_FOOTPRINTS, {
        "type": "FeatureCollection",
        "name": STEM + "-messtischblatt-footprints",
        "features": polygons})
    write_json(OUT_PARTS, {
        "type": "FeatureCollection",
        "name": STEM + "-messtischblatt-clipped-line",
        "features": segments})
    print(json.dumps({
        "candidateLengthKm": report["candidateLengthKm"],
        "sheetLengthSumKm": report["sheetLengthSumKm"],
        "sheetCoverageDifferenceM": report["sheetCoverageDifferenceM"],
        "sheets": [{
            "sheetId": r["sheetId"], "candidateLengthKm": r["candidateLengthKm"],
            "sourceReviewStatus": r["sourceReviewStatus"]
        } for r in records],
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
