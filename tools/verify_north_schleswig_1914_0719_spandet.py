#!/usr/bin/env python3
"""Stage 3: review Spandet (0719) along 8.515 km of the 1914 candidate.

Creates NON-FINAL candidate GeoJSON, map-source diagnostics and temporary
1878-survey/1880-publication SLUB WMS comparison panels.
No independently digitized historical border, no automatic master edit.
Uses the established stage-2 geometry/projection functions.
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

import verify_north_schleswig_1914_0718_east as prior

ROOT = Path(__file__).resolve().parents[1]
BORDER = ROOT / "tools/historical-library/land-borders"
BASE = "german-denmark-1914-north-schleswig-001-026"
STEM = BASE + ".0719-spandet-stage3"
REPORT = BORDER / (STEM + ".json")
GEO = BORDER / (STEM + ".geojson")
PREVIEW = ROOT / "tools/historical-library/review-output/north-schleswig-1914-001-026/0719-spandet"
PERIOD = "1914-07-31"
SOURCES = {
    "0718": ("10006008", "df_dk_0010001_0718", "Hvidding", 1880),
    "0719": ("10006007", "df_dk_0010001_0719", "Spandet", 1880),
    "0619": ("10006020", "df_dk_0010001_0619", "Hügum", 1878),
}
WIDTH_CSS = prior.SCREEN_WIDTH
ZOOM = prior.SCREEN_MAX_ZOOM
STOP_CSS = prior.SCREEN_STOP_PX


def length_segment(line, start, finish):
    return substring(line, max(0, start), min(line.length, finish))


def point_distance(a, b):
    return Point(a).distance(Point(b))


def source_metadata():
    report = prior.common.load(BORDER / (BASE + ".slub-discovery.json"))
    for sheet, (map_id, layer, _, year) in SOURCES.items():
        rows = report["matches"].get(sheet) or []
        if len(rows) != 1:
            raise RuntimeError(f"Need exactly one catalog match for {sheet}")
        entry = rows[0]["metadata"]
        if entry.get("title") != SOURCES[sheet][2] or entry.get("map_scale") != 25000:
            raise RuntimeError(f"Unexpected scale or sheet title for {sheet}")
        if not str(entry["map_id"]).endswith("id-" + map_id):
            raise RuntimeError(f"Unmatched map id for {sheet}")
        if not entry.get("time_period_start", "").startswith(str(year)):
            raise RuntimeError(f"Unmatched publication year for {sheet}")
        if not entry.get("has_georeference"):
            raise RuntimeError(f"Missing georeferencing for {sheet}")
    return report


def wms_image(session, sheet, bbox, width, height):
    map_id, layer, name, publication = SOURCES[sheet]
    west, south, east, north = bbox
    url = "https://wms.kartenforum.slub-dresden.de/map/" + map_id
    params = {
        "SERVICE": "WMS", "REQUEST": "GetMap", "VERSION": "1.3.0",
        "LAYERS": layer, "STYLES": "", "CRS": "EPSG:4326",
        "BBOX": f"{south},{west},{north},{east}",
        "WIDTH": width, "HEIGHT": height,
        "FORMAT": "image/png", "TRANSPARENT": "true",
    }
    response = session.get(url, params=params, timeout=90, headers={
        "User-Agent": "PandoEditor-HistoricalGIS-0719-Review/1.0"})
    response.raise_for_status()
    img = Image.open(io.BytesIO(response.content))
    img.load()
    if img.size != (width, height):
        raise RuntimeError(f"Invalid 0719 WMS size for {sheet}: {img.size}")
    rgba = img.convert("RGBA")
    band_extrema = rgba.getextrema()
    if max(hi-lo for lo, hi in band_extrema[:3]) < 20 or rgba.getchannel("A").getbbox() is None:
        raise RuntimeError(f"Blank historical raster for {sheet}")
    return rgba, {
        "sheetId": sheet,
        "catalogPublicationYear": publication,
        "wmsMapId": map_id,
        "wmsLayer": layer,
        "wmsPngSha256": hashlib.sha256(response.content).hexdigest(),
        "wmsImageBytes": len(response.content),
        "httpStatus": response.status_code,
    }


def bbox_for(line, sheets, margin_lon=0.009, margin_lat=0.007):
    west, south, east, north = transform(prior.common.TO_LL, line).bounds
    west -= margin_lon
    south -= margin_lat
    east += margin_lon
    north += margin_lat
    if len(sheets) == 1:
        # The modern canonical sheet grid is not the period map's precise edge,
        # but prevents accidental duplicate adjacent-sheet downloads.
        if sheets[0] == "0719":
            west, south, east, north = max(west, 8.8333333333), max(south, 55.2), min(east, 9), min(north, 55.3)
        else:
            raise ValueError("Single sheet viewport should be 0719 only")
    return [west, south, east, north]


def draw_preview(session, line, name, title, sheets, width=1580, height=1050):
    bbox = bbox_for(line, sheets)
    originals = [wms_image(session, sheet, bbox, width, height) for sheet in sheets]
    combined = Image.new("RGBA", (width, height), (255,255,255,0))
    for raster, metadata in originals:
        combined.alpha_composite(raster)
    flat = Image.new("RGB", (width, height), "white")
    flat.paste(combined, mask=combined.getchannel("A"))
    original_file = f"{name}-source.jpg"
    flat.save(PREVIEW / original_file, "JPEG", quality=78, optimize=True)

    out = Image.new("RGB", (width, height+72), "white")
    out.paste(flat, (0,34))
    d = ImageDraw.Draw(out)
    d.rectangle((0,0,width,33), fill=(28,39,50))
    d.text((12,8), title+"  |  provisional 1914 candidate / period sheets", fill="white",
           font=prior.common.nice_font(17))
    w, s, e, n = bbox
    projected = [
        ((x-w)/(e-w)*(width-1), 34+(n-y)/(n-s)*(height-1))
        for x,y in transform(prior.common.TO_LL,line).coords
    ]
    if len(projected)<2:
        raise RuntimeError(f"Too few sample pixels in {name}")
    d.line(projected, fill="white", width=8)
    d.line(projected, fill=(219,29,48), width=4)
    for index, (x,y) in enumerate([projected[0], projected[-1]]):
        d.ellipse((x-7,y-7,x+7,y+7), fill=(255,212,13), outline=(28,32,36), width=2)
        d.text((x+10,y-19),"START" if index==0 else "END",
               font=prior.common.nice_font(13),fill=(136,15,15),
               stroke_width=2,stroke_fill="white")
    d.rectangle((0,height+34,width,height+72), fill=(28,39,50))
    d.text((12,height+46),
           "RED: modern parish-based candidate, NOT an independently digitized historical frontier.",
           fill="white",font=prior.common.nice_font(15))
    overlay_file = f"{name}-overlay.jpg"
    out.save(PREVIEW/overlay_file,"JPEG",quality=80,optimize=True)
    return {
        "panel": name, "title": title, "sourceSheets": sheets,
        "candidateLengthKm": round(line.length/1000,5),
        "bboxLonLat": [round(v,8) for v in bbox],
        "sourceJpeg": original_file, "overlayJpeg": overlay_file,
        "sourceWms": [metadata for _,metadata in originals],
        "overlaySha256": hashlib.sha256((PREVIEW/overlay_file).read_bytes()).hexdigest(),
        "historicBoundaryVisuallyReviewed": False
    }


def main():
    source_metadata()
    source = prior.common.load(BORDER / (BASE + ".sheet-segments.geojson"))
    west = prior.extract_sheet(source, "0718")
    center = prior.extract_sheet(source, "0719")
    east = prior.extract_sheet(source, "0619")
    last_stage2 = prior.common.load(BORDER/(BASE+".0718-east-stage2.geojson"))
    previous = transform(prior.common.TO_M, LineString(
        last_stage2["features"][0]["geometry"]["coordinates"]))
    if not (west.is_simple and center.is_simple and east.is_simple):
        raise RuntimeError("Master source partitions are not simple")
    if not 8400 < center.length < 8700:
        raise RuntimeError("0719 candidate extent is unexpectedly different")
    gap_prev_m = point_distance(west.coords[-1],center.coords[0])
    gap_stage2_m = point_distance(previous.coords[-1],center.coords[0])
    gap_next_m = point_distance(center.coords[-1],east.coords[0])
    if max(gap_prev_m,gap_stage2_m,gap_next_m)>0.01:
        raise RuntimeError("Candidate sheet seam disconnected")
    # Ensure exact app/web clipping, no accidental historical replacement.
    complete = prior.common.load(BORDER/(BASE+".geojson"))["features"][0]
    if complete["properties"].get("doNotTreatAsFinal") is not True:
        raise RuntimeError("Original reconstruction no longer provisional")
    master = transform(prior.common.TO_M, LineString(complete["geometry"]["coordinates"][0])) if (
        complete["geometry"]["type"]=="MultiLineString") else transform(
        prior.common.TO_M,LineString(complete["geometry"]["coordinates"]))
    endpoint_to_master=[master.distance(Point(c)) for c in (center.coords[0],center.coords[-1])]
    if max(endpoint_to_master)>0.01:
        raise RuntimeError("Stage 3 candidate not on 1–26 working master")
    # Non-destructive single-sheet source clip.
    candidate = transform(prior.common.TO_LL,center)
    geo = {
        "type":"FeatureCollection",
        "name":STEM+"-provisional",
        "features":[{
            "type":"Feature",
            "id":"land-border:deu-dnk:1914:north-schleswig:0719-spandet-stage3",
            "properties":{
                "referenceDate":PERIOD, "sheetId":"0719",
                "sourcePublicationYear":1880,
                "status":"provisional-modern-parish-only",
                "historicalMapTraced":False,
                "doNotTreatAsFinal":True,
                "lengthKm":round(center.length/1000,5),
                "canonicalSource":BASE+".sheet-segments.geojson"
            },
            "geometry":mapping(candidate)
        }]
    }
    PREVIEW.mkdir(parents=True,exist_ok=True)
    prior.common.dump(GEO,geo)
    session = requests.Session()
    # Three panels instead of marker-by-marker tracing. Seam checks are separate.
    d = center.length/3
    chunks=[length_segment(center,d*i,d*(i+1)) for i in range(3)]
    west_join=LineString(
        list(length_segment(west, west.length-650,west.length).coords)
        + list(length_segment(center,0,650).coords)[1:])
    east_join=LineString(
        list(length_segment(center,center.length-650,center.length).coords)
        + list(length_segment(east,0,650).coords)[1:])
    if not (west_join.is_simple and east_join.is_simple):
        raise RuntimeError("Non-simple border at map sheet transition")
    visuals=[
        draw_preview(session,center,"0719-overview","Spandet 0719 complete",["0719"],width=1800,height=1250),
        draw_preview(session,chunks[0],"0719-west","Spandet 0719 western third",["0719"],width=1650,height=1150),
        draw_preview(session,chunks[1],"0719-middle","Spandet 0719 central third",["0719"],width=1650,height=1150),
        draw_preview(session,chunks[2],"0719-east","Spandet 0719 eastern third",["0719"],width=1650,height=1150),
        draw_preview(session,west_join,"0718-0719-west-seam","0718 Hvidding -> 0719 Spandet",["0718","0719"],width=1450,height=1000),
        draw_preview(session,east_join,"0719-0619-north-seam","0719 Spandet -> 0619 Hugum",["0719","0619"],width=1450,height=1000)
    ]
    report={
        "schemaVersion":1, "workUnit":"1914-German-Danish-border/0719-Spandet/stage-3",
        "referenceDate":PERIOD,
        "mapSource":{
            "sheetId":"0719",
            "title":"Spandet",
            "scaleDenominator":25000,
            "surveyYear":1878,
            "catalogPublicationYear":1880,
            "lastCartographicRevisionDate":"unverified",
            "deutscheFotothekUrl":"https://www.deutschefotothek.de/documents/obj/71051523",
            "slubWmsId":"10006007",
            "slubWmsLayer":SOURCES["0719"][1],
        },
        "screenReviewPolicy":{
            "mapCssWidth":WIDTH_CSS,
            "webFlatZoomMax":ZOOM,
            "independentHistoryVisualTolerancePx":STOP_CSS,
            "independentHistoricalLineDigitized":False,
            "historicLineDisplacementPx":None,
            "thresholdPassed":None
        },
        "candidateStatus":"provisional-only-do-not-promote",
        "historicalCartographicReviewPerformed":False,
        "historicalMapSourceRasterAccessible":True,
        "geometrySourceModified":False,
        "geometry":{
            "candidateLengthKm":round(center.length/1000,5),
            "startLonLat":list(candidate.coords[0]),
            "endLonLat":list(candidate.coords[-1]),
            "startMatches0718DistanceM":round(gap_prev_m,6),
            "matchesStage2EndpointDistanceM":round(gap_stage2_m,6),
            "endMatches0619DistanceM":round(gap_next_m,6),
            "startToWorkingMasterDistanceM":round(endpoint_to_master[0],6),
            "endToWorkingMasterDistanceM":round(endpoint_to_master[1],6),
            "isSimple":bool(center.is_simple),
            "westSheetSeamIsSimple":bool(west_join.is_simple),
            "eastSheetSeamIsSimple":bool(east_join.is_simple),
            "candidateVsOwnSimplification":prior.screen_geometry(center),
            "sourceTopologyOnly":True
        },
        "images":visuals,
        "githubActionsRunId":os.environ.get("GITHUB_RUN_ID"),
        "artifactDir":str(PREVIEW.relative_to(ROOT)),
        "historicalMapObservations":{
            "largeRouteMismatch":"not-yet-visually-reviewed",
            "riverAndParishFollowing":"not-yet-visually-reviewed",
            "boundaryStoneLabels":"not-yet-visually-reviewed",
            "historicalDateAndTerritoryReview":"not-yet-verified"
        },
        "unresolved":[
            "Visual comparison of original mapped international frontier and current candidate across three panels.",
            "Historical publication/survey dates alone do not establish final map revision as of July 1914.",
            "The independent 1914 historic boundary has not been digitized; the 0.5px historical threshold is untested.",
            "No candidate coordinates may be promoted solely because internal joins and LOD checks pass.",
            "Confirm eastern sheet-transition map content around 55.300N before 0619 verification."
        ]
    }
    prior.common.dump(REPORT,report)
    print(json.dumps({
        "stage":3,
        "lengthKm":report["geometry"]["candidateLengthKm"],
        "joinPrevM":gap_stage2_m,"joinNextM":gap_next_m,
        "cssScreenLength":report["geometry"]["candidateVsOwnSimplification"]["candidateDisplayedLengthCssPx"],
        "panels":[x["panel"] for x in visuals],
        "status":report["candidateStatus"],
    },indent=2,ensure_ascii=False))
    if os.environ.get("GIS_EMIT_LOG_THUMBNAIL")=="1":
        for panel in visuals:
            img=Image.open(PREVIEW/panel["overlayJpeg"])
            img.thumbnail((1200,920))
            tmp=io.BytesIO()
            img.save(tmp,"JPEG",quality=65,optimize=True)
            name=panel["panel"].upper().replace("-","_")
            print(f"===BEGIN_STAGE3_SPANDET_{name}_JPEG===")
            print(base64.b64encode(tmp.getvalue()).decode("ascii"))
            print(f"===END_STAGE3_SPANDET_{name}_JPEG===")


if __name__=="__main__":
    main()
