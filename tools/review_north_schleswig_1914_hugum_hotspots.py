#!/usr/bin/env python3
"""Targeted visual screen-tolerance audit of 1878 Hügum historic map.

Review ONLY (1) Hårup meanders and (2) Gels Å / Flads Å confluence;
no blanket river re-digitisation. Draw a 0.5 CSS-pixel corridor in the
actual 2560 CSS px / flatZoom=64 equirectangular projection. Distinguish
this VISUAL corridor from independently measured 1914 historic-line error.

Outputs metadata+provisional status to git; map images to temporary CI artifact.
"""
from __future__ import annotations
import base64
import hashlib
import io
import json
import os
from pathlib import Path

import requests
from PIL import Image, ImageDraw, ImageFont
from shapely.geometry import LineString, box, shape
from shapely.ops import transform

import verify_north_schleswig_1914_0619_hugum as previous

ROOT=Path(__file__).resolve().parents[1]
LIB=ROOT/"tools/historical-library"
B=LIB/"land-borders"
STEM="german-denmark-1914-north-schleswig-001-026"
OUTPUT=B/(STEM+".0619-hugum-stage4.hotspots.json")
PREVIEW=LIB/"review-output/north-schleswig-1914-001-026/0619-hugum-hotspots"
MAP_WIDTH_CSS=2560
MAX_FLAT_ZOOM=64
TOLERANCE_CSS=0.5
DEG_PER_CSS_PX=360/(MAP_WIDTH_CSS*MAX_FLAT_ZOOM)
CORRIDOR_DEG=DEG_PER_CSS_PX*TOLERANCE_CSS
LOCATIONS={
    "harup-meander":{
        "bboxLonLat":[8.8995,55.3076,8.9120,55.3152],
        "theme":"Hårup middle-stage 1878 river meander discrepancy",
        "action":"Look for large meanders actually outside ±0.5 CSS px corridor. Later-period evidence needed before correction.",
    },
    "gels-flads-confluence":{
        "bboxLonLat":[8.8775,55.3143,8.8995,55.3265],
        "theme":"Gels Å / Flads Å confluence and north-bank continuation",
        "action":"Locate 1878 confluence branch geometry; do not infer 1914 junction or Flads Å bank by snapping to modern centerline.",
    },
}

def read(path):
    return json.loads(path.read_text(encoding="utf-8"))

def write(path,value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")

def font(size):
    try:return ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",size)
    except OSError:return ImageFont.load_default()

def screen_trace(poly, bbox, width, height, y_offset=40):
    w,s,e,n=bbox
    return [
        ((lon-w)/(e-w)*(width-1), y_offset+(n-lat)/(n-s)*(height-1))
        for lon,lat in poly.coords
    ]

def roi_map(session,box_name,details,line_ll):
    bbox=details["bboxLonLat"]
    # A period-map WMS image; the geographic bbox is held constant between
    # original and overlay files.
    w,s,e,n=bbox
    width,height=(1780,1160) if box_name=="harup-meander" else (1800,1180)
    url="https://wms.kartenforum.slub-dresden.de/map/10006020"
    params={
        "SERVICE":"WMS","VERSION":"1.3.0","REQUEST":"GetMap",
        "LAYERS":"df_dk_0010001_0619","STYLES":"","CRS":"EPSG:4326",
        "BBOX":f"{s},{w},{n},{e}",
        "WIDTH":width,"HEIGHT":height,
        "FORMAT":"image/png","TRANSPARENT":"true"
    }
    response=session.get(url,params=params,timeout=95,headers={
        "User-Agent":"PandoEditor-HugumExceptions-1914/1.0"})
    response.raise_for_status()
    image=Image.open(io.BytesIO(response.content)).convert("RGBA")
    if image.size!=(width,height):
        raise RuntimeError("Invalid 1878 original image dimensions")
    if image.getchannel("A").getbbox() is None:
        raise RuntimeError("Missing 1878 original raster")
    original=Image.new("RGB",(width,height),"white")
    original.paste(image,mask=image.getchannel("A"))
    orig_path=PREVIEW/(box_name+"-1878-source.jpg")
    original.save(orig_path,format="JPEG",quality=82,optimize=True)

    view=box(*bbox)
    display_lines=line_ll.intersection(view)
    if display_lines.is_empty:raise RuntimeError("Candidate is outside selected hotspot "+box_name)
    # Projected baseline: degrees are proportional to CSS px for
    # equirectangular map. Buffer radius in degrees is precisely 0.5 CSS px.
    # The corridor checks whether dark lines appear visually outside the
    # tolerated displacement band; it is NOT a historic 1914 accuracy test.
    corridor=line_ll.buffer(CORRIDOR_DEG,cap_style=2,join_style=2).intersection(view)
    corridor_layer=Image.new("RGBA",(width,height+78),(0,0,0,0))
    shade=ImageDraw.Draw(corridor_layer,"RGBA")
    shapes=[corridor] if corridor.geom_type=="Polygon" else (
        list(corridor.geoms) if corridor.geom_type=="MultiPolygon" else [])
    def pathpts(points):
        return [((lon-w)/(e-w)*(width-1),40+(n-lat)/(n-s)*(height-1)) for lon,lat in points]
    for poly in shapes:
        if poly.is_empty:continue
        outer=pathpts(poly.exterior.coords)
        if len(outer)>2:
            shade.polygon(outer,fill=(250,208,50,52))
            shade.line(outer+[outer[0]],fill=(173,101,25,170),width=2)
        for inner in poly.interiors:
            pts=pathpts(inner.coords)
            if len(pts)>2:shade.polygon(pts,fill=(0,0,0,0))
    painted=Image.new("RGB",(width,height+78),"white")
    painted.paste(original,(0,40))
    painted=Image.alpha_composite(painted.convert("RGBA"),corridor_layer).convert("RGB")
    d=ImageDraw.Draw(painted)
    d.rectangle((0,0,width,39),fill=(22,38,55))
    d.text((14,9),details["theme"],font=font(17),fill="white")
    # Draw all line parts inside the selected bbox; never join separate cuts
    # with invented straight lines.
    parts=[display_lines] if display_lines.geom_type=="LineString" else list(display_lines.geoms)
    for part in parts:
        if part.geom_type!="LineString" or len(part.coords)<2:continue
        pts=pathpts(part.coords)
        d.line(pts,fill=(255,255,255),width=7)
        d.line(pts,fill=(206,31,57),width=4)
    d.rectangle((0,height+40,width,height+78),fill=(22,38,55))
    d.text((14,height+51),
           "RED: modern candidate; YELLOW: +/-0.5 CSS px at 2560 CSSpx / flatZoom64 (not historic validation).",
           font=font(16),fill="white")
    out_path=PREVIEW/(box_name+"-tolerance-overlay.jpg")
    painted.save(out_path,format="JPEG",quality=83,optimize=True)
    return {
        "bboxLonLat":bbox,
        "historicalMap":"0619 Hügum, mapped/published 1878, WMS 10006020",
        "panelSizePx":[width,height],
        "originalPngSha256":hashlib.sha256(response.content).hexdigest(),
        "overlayJpgSha256":hashlib.sha256(out_path.read_bytes()).hexdigest(),
        "originalImageName":orig_path.name,"overlayImageName":out_path.name,
        "candidateCorridorHalfWidthCssPx":TOLERANCE_CSS,
        "candidateCorridorRadiusDegrees":CORRIDOR_DEG,
        "historicalLineDigitized":False,
        "georeferencingResidualAvailable":False,
        "manualReviewStatus":"needs-visual-inspection",
        "todo":details["action"],
    }

def main():
    data=read(B/(STEM+".0619-hugum-stage4.geojson"))
    line=shape(data["features"][0]["geometry"])
    if line.geom_type!="LineString" or not line.is_simple or len(line.coords)!=790:
        raise RuntimeError("Input 0619 source candidate not recognized")
    if data["features"][0]["properties"].get("doNotTreatAsFinal") is not True:
        raise RuntimeError("Candidate is no longer provisional")
    PREVIEW.mkdir(parents=True,exist_ok=True)
    session=requests.Session()
    panels={key:roi_map(session,key,value,line) for key,value in LOCATIONS.items()}
    output={
        "schemaVersion":1,
        "referenceDate":"1914-07-31",
        "workUnit":"German-Danish border, Hügum Hårup and Flads Å confluence exceptions only",
        "referenceSheet":"0619 Hügum 1878 1:25,000",
        "dataProvenance":"https://www.deutschefotothek.de/documents/obj/71051510",
        "periodMapOriginalReviewEnabled":True,
        "knownHistoricMapPublicationYears":[1878],
        "otherPublishedRevision":{
            "publication":"Reichsamt für Landesaufnahme, Das Reichsamt für Landesaufnahme und seine Kartenwerke (Berlin, 1931), List A",
            "sheet":"Hügum No. 21",
            "reportedFirstEditionYear":1880,
            "reportedLastSubstantialRevision":"B 1916",
            "sourceUrl":"https://doczz.net/doc/5885424/das-reichsamt-f%C3%BCr-landesaufnahme-und-seine-kartenwerke",
            "lastRevisionAfterReferenceDate":True,
            "sourceYearConflict":"SLUB catalog records a Hügum 1878 publication, while the 1931 Reichsamt list gives the original publication 1880. Differences of publication/edition metadata are not reconciled.",
            "note":"A 1916 revised edition may help retrospective analysis but cannot establish the mid-1914 river route by itself. A pre-1914 intermediate edition has not been obtained.",
        },
        "nearContemporarySeries":{
            "publisher":"Danish Datafordeler",
            "catalog":"Preussiske målebordsblade 1877–1912, 1:25,000",
            "access":"https://datafordeler.dk/dataoversigt/historiske-kort-og-data/preussiske-maalebordsblade-wms/",
            "apiKeyRequired":True,
            "later0619SheetAcquired":False,
        },
        "rendering":{
            "mapContentWidthCssPx":MAP_WIDTH_CSS,
            "flatZoom":MAX_FLAT_ZOOM,
            "viewToleranceCssPx":TOLERANCE_CSS,
            "toleranceDegrees":CORRIDOR_DEG,
            "physicalNote":"Displacement is in equal-angle 2D screen-space; it is not a metre-scale legal accuracy threshold.",
            "historicalReferenceDigitized":False,
            "measuredDifferenceToHistorical1914CssPx":None,
            "targetPassed":None,
        },
        "panels":panels,
        "primaryConcerns":{
            "harup":"Difference between 1878 mapped channel and modern working geometry may be visually significant. Later reference date still missing.",
            "confluence":"Position and side/bank rule of Flads Å after confluence must not be inferred from present river centerline.",
        },
        "masterUnmodified":True,
        "historicBorderCertified":False,
        "runId":os.environ.get("GITHUB_RUN_ID"),
        "rasterFilesCommittedToGit":False,
        "next":"After manual inspection, classify only visible >0.5 CSSpx bends as targeted follow-ups. Defer geometry correction until a pre-1914 reference is validated.",
    }
    write(OUTPUT,output)
    print(json.dumps({
        "sheet":"Hügum 0619",
        "hotspots":list(panels.keys()),
        "renderThresholdDegrees":CORRIDOR_DEG,
        "original1880Revision1916Status":"revision postdates July 1914",
        "runId":output["runId"],
        "historic1914ReferenceAcquired":False,
        "output":str(OUTPUT),
    },ensure_ascii=False,indent=2))
    if os.environ.get("GIS_LOG_HOTSPOT_THUMBNAIL")=="1":
        for key,details in panels.items():
            img=Image.open(PREVIEW/details["overlayImageName"])
            img.thumbnail((1300,950))
            out=io.BytesIO()
            img.save(out,"JPEG",quality=68,optimize=True)
            tag=key.upper().replace("-","_")
            print(f"===BEGIN_HUGUM_HOTSPOT_{tag}_JPEG===")
            print(base64.b64encode(out.getvalue()).decode("ascii"))
            print(f"===END_HUGUM_HOTSPOT_{tag}_JPEG===")

if __name__=="__main__":
    main()
