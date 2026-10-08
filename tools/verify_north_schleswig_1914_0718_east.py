#!/usr/bin/env python3
"""Second GIS unit: Hvidding 0718 eastern 5.94 km and its Spandet 0719 seam.

Use the existing western-stage helpers for the same source and projections.
This produces reversible candidate clips, screen-scale geometry statistics,
historical WMS image overlays and topology checks; it does NOT independently
digitize an 1880/1914 frontier or promote provisional geometry.
"""
import base64
import hashlib
import io
import json
import os
from pathlib import Path

import requests
from PIL import Image, ImageDraw
from shapely.geometry import LineString, Point, mapping
from shapely.ops import substring, transform
import verify_north_schleswig_1914_0718_west as common

ROOT = Path(__file__).resolve().parents[1]
BORDER = ROOT / "tools/historical-library/land-borders"
STEM = "german-denmark-1914-north-schleswig-001-026"
STAGE = STEM + ".0718-east-stage2"
OUTPUT = BORDER / (STAGE + ".json")
OUTPUT_GEO = BORDER / (STAGE + ".geojson")
ARTIFACT = ROOT / "tools/historical-library/review-output/north-schleswig-1914-001-026/0718-east"
WMS = {
    "0718": ("https://wms.kartenforum.slub-dresden.de/map/10006008", "df_dk_0010001_0718"),
    "0719": ("https://wms.kartenforum.slub-dresden.de/map/10006007", "df_dk_0010001_0719"),
}
SCREEN_WIDTH = 2560
SCREEN_MAX_ZOOM = 64
SCREEN_STOP_PX = 0.5
REFERENCE_DATE = "1914-07-31"


def extract_sheet(geodata, sheet_id):
    hits = [f for f in geodata["features"] if f.get("properties", {}).get("sheetId") == sheet_id]
    if len(hits) != 1 or hits[0]["geometry"]["type"] != "MultiLineString":
        raise ValueError("Expected exactly one sheet: " + sheet_id)
    coords = hits[0]["geometry"]["coordinates"]
    if len(coords) != 1 or len(coords[0]) < 2:
        raise ValueError("Non-contiguous sheet geometry: " + sheet_id)
    return transform(common.TO_M, LineString(coords[0]))


def distance(a, b):
    return Point(a).distance(Point(b))


def screen_geometry(line):
    s = SCREEN_WIDTH * SCREEN_MAX_ZOOM / 360.0
    coords = list(transform(common.TO_LL, line).coords)
    projected = LineString([(x * s, -y * s) for x, y in coords])
    simplified = projected.simplify(SCREEN_STOP_PX, preserve_topology=True)
    return {
        "widthCssPx": SCREEN_WIDTH,
        "flatZoom": SCREEN_MAX_ZOOM,
        "simplificationTargetCssPx": SCREEN_STOP_PX,
        "candidateDisplayedLengthCssPx": round(projected.length, 3),
        "candidateVertexCount": len(coords),
        "illustrativeSimplifiedVertexCount": len(simplified.coords),
        "candidateVersusOwnSimplificationHausdorffCssPx": round(
            projected.hausdorff_distance(simplified), 5),
        "independentHistoricReferenceScreenDifferenceCssPx": None,
        "note": ("Simplification is a display-efficiency experiment; it does NOT "
                 "compare the 1880 historical border with the modern-parish candidate."),
    }


def map_get(session, sheet, bbox, width, height):
    url, layer = WMS[sheet]
    west, south, east, north = bbox
    params = {
        "SERVICE": "WMS", "VERSION": "1.3.0", "REQUEST": "GetMap",
        "LAYERS": layer, "STYLES": "", "CRS": "EPSG:4326",
        "BBOX": f"{south},{west},{north},{east}",
        "WIDTH": width, "HEIGHT": height, "FORMAT": "image/png",
        "TRANSPARENT": "true",
    }
    r = session.get(url, params=params, timeout=85,
                    headers={"User-Agent": "PandoEditor-HistoricBorderStage2/1.0"})
    r.raise_for_status()
    im = Image.open(io.BytesIO(r.content))
    im.load()
    if im.size != (width, height):
        raise RuntimeError(f"WMS {sheet} returned invalid dimensions")
    rgba = im.convert("RGBA")
    extrema = rgba.getextrema()
    if max(hi - lo for lo, hi in extrema[:3]) < 20:
        raise RuntimeError(f"WMS {sheet} appears blank")
    return rgba, {
        "sheet": sheet, "mapId": url.rsplit("/", 1)[-1],
        "layer": layer, "contentType": r.headers.get("Content-Type"),
        "byteLength": len(r.content),
        "sha256": hashlib.sha256(r.content).hexdigest(),
    }


def clip_bounds(line, cross_seam=False):
    west, south, east, north = transform(common.TO_LL, line).bounds
    west -= 0.008
    east += 0.008
    south -= 0.008
    north += 0.008
    # Do not clamp the two-sheet seam to 0718's east edge.
    if not cross_seam:
        west, east = max(west, 8.6666666667), min(east, 8.8333333333)
    return [west, max(55.2, south), east, min(55.3, north)]


def preview(session, line, name, title, sheets=("0718",),
            width=1750, height=1100):
    bbox = clip_bounds(line, cross_seam="0719" in sheets)
    layers = [map_get(session, sheet, bbox, width, height) for sheet in sheets]
    base = Image.new("RGBA", (width, height), (255, 255, 255, 0))
    for image, _ in layers:
        base.alpha_composite(image)
    flat = Image.new("RGB", (width, height), "white")
    flat.paste(base, mask=base.getchannel("A"))
    source_name = f"{name}-original.jpg"
    flat.save(ARTIFACT / source_name, format="JPEG", quality=80, optimize=True)

    composed = Image.new("RGB", (width, height + 76), "white")
    composed.paste(flat, (0, 37))
    draw = ImageDraw.Draw(composed)
    draw.rectangle((0, 0, width, 36), fill=(30, 45, 61))
    draw.text((14, 10), title + " / original surveyed 1878, issue 1880",
              fill="white", font=common.nice_font(18))
    w, s, e, n = bbox
    coords = list(transform(common.TO_LL, line).coords)
    pixels = [((x-w)/(e-w)*(width-1), 37+(n-y)/(n-s)*(height-1))
              for x, y in coords]
    if len(pixels) > 1:
        draw.line(pixels, fill="white", width=8)
        draw.line(pixels, fill=(214, 25, 40), width=4)
    for k, (x, y) in enumerate((pixels[0], pixels[-1])):
        draw.ellipse([x-7,y-7,x+7,y+7], fill=(255,217,0), outline=(24,25,25), width=2)
        draw.text((x+11,y-20), "START" if k==0 else "END", fill=(105,0,0),
                  font=common.nice_font(14), stroke_width=2, stroke_fill="white")
    draw.rectangle((0, height+37, width, height+76), fill=(30,45,61))
    draw.text((14, height+49),
              "RED: CURRENT parish-based CANDIDATE. Historic line NOT independently traced.",
              fill="white", font=common.nice_font(17))
    overlay_name = f"{name}-candidate-overlay.jpg"
    composed.save(ARTIFACT / overlay_name, format="JPEG",
                  quality=81, optimize=True)
    return {
        "name": name,
        "sheets": list(sheets),
        "extentLonLat": [round(x, 8) for x in bbox],
        "sourceImage": source_name,
        "candidateOverlayImage": overlay_name,
        "sourceWmsResponses": [metadata for _, metadata in layers],
        "overlaySha256": hashlib.sha256((ARTIFACT / overlay_name).read_bytes()).hexdigest(),
        "geometryLengthKm": round(line.length / 1000, 5),
        "humanVisualReviewCompleted": False,
    }


def main():
    candidates = common.load(BORDER / (STEM + ".sheet-segments.geojson"))
    raw_0718 = extract_sheet(candidates, "0718")
    raw_0719 = extract_sheet(candidates, "0719")
    west = substring(raw_0718, 0, raw_0718.length / 2)
    east = substring(raw_0718, raw_0718.length / 2, raw_0718.length)
    first_0719 = substring(raw_0719, 0, min(900, raw_0719.length))
    prior = common.load(BORDER / (STEM + ".0718-west-stage1.geojson"))
    prior_west = transform(common.TO_M, LineString(prior["features"][0]["geometry"]["coordinates"]))
    if not (raw_0718.is_simple and east.is_simple and raw_0719.is_simple):
        raise RuntimeError("One of the existing working candidate lines self-intersects")
    if not 5900 < east.length < 6000:
        raise RuntimeError("Unexpected eastern-half length")
    gap_west_m = distance(prior_west.coords[-1], east.coords[0])
    gap_next_m = distance(east.coords[-1], raw_0719.coords[0])
    max_dev_west_m = prior_west.hausdorff_distance(west)
    if max(gap_west_m, gap_next_m, max_dev_west_m) > 0.01:
        raise RuntimeError(f"Candidate boundary seam invalid: {gap_west_m}, {gap_next_m}, {max_dev_west_m}")
    if abs(prior_west.length + east.length - raw_0718.length) > 0.01:
        raise RuntimeError("Clips fail to partition the 0718 working candidate")

    east_ll = transform(common.TO_LL, east)
    geo = {
        "type": "FeatureCollection",
        "name": STEM + "-0718-east-stage2-provisional",
        "features": [{
            "type": "Feature",
            "id": "land-border:deu-dnk:1914:north-schleswig:0718-east-stage2",
            "properties": {
                "referenceDate": REFERENCE_DATE,
                "sheet": "0718 Hvidding",
                "publicationYear": 1880,
                "status": "provisional-modern-parish-only",
                "doNotTreatAsFinal": True,
                "historicalMapTraced": False,
                "lengthKm": round(east.length / 1000, 5),
                "source": STEM + ".sheet-segments.geojson"
            },
            "geometry": mapping(east_ll)
        }]
    }
    common.dump(OUTPUT_GEO, geo)
    ARTIFACT.mkdir(parents=True, exist_ok=True)
    session = requests.Session()
    p1 = substring(east, 0, east.length / 2)
    p2 = substring(east, east.length / 2, east.length)
    last_0718 = substring(east, max(0, east.length - 900), east.length)
    seam = LineString(list(last_0718.coords) + list(first_0719.coords)[1:])
    if not seam.is_simple:
        raise RuntimeError("Cross-sheet seam intersects itself")
    photos = [
        preview(session, east, "0718-east-overview", "Stage 2: Hvidding EAST (50–100%)"),
        preview(session, p1, "0718-east-western-detail", "Stage 2: Hvidding eastern half, detail 1"),
        preview(session, p2, "0718-east-eastern-detail", "Stage 2: Hvidding eastern half, detail 2"),
        preview(session, seam, "0718-0719-join", "Stage 2: Hvidding / Spandet seam",
                sheets=("0718", "0719"), width=1600, height=1100),
    ]
    report = {
        "schemaVersion": 1,
        "workUnit": "1914-German-Danish-border/0718-east/stage-2",
        "referenceDate": REFERENCE_DATE,
        "historicalMap": {
            "sheet": "0718 Hvidding",
            "originalSurveyYear": 1878,
            "catalogPublicationYear": 1880,
            "possibleLaterCartographicRevisions": "not-dated",
            "resolution": "1:25,000",
            "originalMapId": "10006008",
            "originalPermalink": "https://www.deutschefotothek.de/documents/obj/71051522",
            "adjacentSheet": "0719 Spandet",
            "adjacentOriginalMapId": "10006007",
        },
        "verificationState": "raster-overlay-prepared / pending-visual-review",
        "originalRasterAccessible": True,
        "historicalBoundaryIndependentlyDigitized": False,
        "historicalScreenDeviationMeasured": False,
        "masterGeometryModified": False,
        "candidate": {
            "sheet0718Km": round(raw_0718.length / 1000, 5),
            "westHalfKm": round(west.length / 1000, 5),
            "eastHalfKm": round(east.length / 1000, 5),
            "startLonLat": list(east_ll.coords[0]),
            "endLonLat": list(east_ll.coords[-1]),
            "topology": {
                "westHalfJoiningGapM": round(gap_west_m, 6),
                "eastJoiningGapTo0719M": round(gap_next_m, 6),
                "previousWestStageHausdorffM": round(max_dev_west_m, 6),
                "lengthPartitionErrorM": round(
                    prior_west.length + east.length - raw_0718.length, 6),
                "eastHalfSimple": bool(east.is_simple),
                "combinedSeamSimple": bool(seam.is_simple),
                "sourceTopologyOnly": True,
            },
            "modernMappedBoundaryDitchCrossCheck": common.closest_named_ditch(east),
            "renderDisplayEstimate": screen_geometry(east),
        },
        "images": photos,
        "historicalVisualInspection": {
            "performed": False,
            "largeVisiblePathMismatch": "not-evaluated",
            "historicalOwnershipOrCourseMismatch": "not-evaluated",
            "revisionDate1887BorderStationIssue": "unresolved-following-stage1",
            "screenStopRule": ("Do not claim historical 0.5 CSS px deviation without an "
                               "independently interpreted period boundary."),
        },
        "reviewArtifactPath": str(ARTIFACT.relative_to(ROOT)),
        "githubActionsRunId": os.environ.get("GITHUB_RUN_ID"),
        "reviewRequired": [
            "Visually compare historical boundary marks and current red candidate in both details.",
            "Check whether historical border follows the same village/stream course.",
            "Inspect handover near 0719 Spandet; do not confuse polygon continuity with period-map proof.",
            "Review exceptions even when below 0.5 CSS px; stop meaningless subpixel refinements.",
            "Preserve date/revision uncertainty, notably rail facilities in maps catalogued as 1880.",
        ]
    }
    common.dump(OUTPUT, report)
    print(json.dumps({
        "stage": 2,
        "sourceSheet": "0718",
        "eastHalfKm": report["candidate"]["eastHalfKm"],
        "joinWestM": gap_west_m,
        "joinNextM": gap_next_m,
        "cssPathLengthAtMaxZoom": report["candidate"]["renderDisplayEstimate"]["candidateDisplayedLengthCssPx"],
        "panels": [x["name"] for x in photos],
        "runId": report["githubActionsRunId"],
        "status": report["verificationState"],
    }, indent=2, ensure_ascii=False))
    if os.environ.get("GIS_EMIT_LOG_THUMBNAIL") == "1":
        for p in photos:
            img = Image.open(ARTIFACT / p["candidateOverlayImage"])
            img.thumbnail((1250, 960))
            buffer = io.BytesIO()
            img.save(buffer, "JPEG", quality=67, optimize=True)
            name = p["name"].upper().replace("-", "_")
            print(f"===BEGIN_HVIDDING_STAGE2_{name}_JPEG===")
            print(base64.b64encode(buffer.getvalue()).decode("ascii"))
            print(f"===END_HVIDDING_STAGE2_{name}_JPEG===")


if __name__ == "__main__":
    main()
