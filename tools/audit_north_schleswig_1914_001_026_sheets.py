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

# German Messtischblatt system, post-1937 row+column catalog notation:
# NW corner 56N / 5deg50E; 6 arcminutes high and 10 arcminutes wide.
# https://de.wikipedia.org/wiki/Messtischblatt
GRID_NORTH = 56.0
GRID_WEST = 5.0 + 50.0 / 60.0
HEIGHT_DEG = 6.0 / 60.0
WIDTH_DEG = 10.0 / 60.0

# This is an index of *catalogued editions*, NOT a claim that those
# editions have been raster-accessed or georeferenced.
EDITION_CATALOG = {
    "0817": dict(title="Kirkeby", issueYear=1880, surveyYear=1878,
                 objectId="71051540"),
    "0818": dict(title="Bröns", issueYear=1919, surveyYear=1878,
                 objectId="71051541"),
    "0819": dict(title="Arrild", issueYear=1880, surveyYear=1878,
                 objectId="71051542"),
    "0719": dict(title="Spandet", issueYear=1880, surveyYear=1878,
                 objectId="71051523"),
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
        "warning": ("Geometric sheet coverage is not historic boundary evidence; "
                    "1919 Bröns edition must not verify a 1914 border."),
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
