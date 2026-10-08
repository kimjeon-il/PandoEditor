#!/usr/bin/env python3
"""Stage 4: 1878 Hügum 0619 map review for the post-stone-26 Gels Å reach.

Only original-source access, modern candidate topology, and temporary map
panels are verified automatically. This script never edits the 1–26 master,
never certifies the historic watercourse, and never infers historic 0.5px
accuracy from simplification of the same modern candidate.
"""
import base64
import hashlib
import io
import json
import math
import os
from pathlib import Path

import requests
from PIL import Image, ImageDraw
from pyproj import Transformer
from shapely.geometry import LineString, Point, mapping, shape
from shapely.ops import substring, transform
import verify_north_schleswig_1914_0719_spandet as prior

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "tools/historical-library/land-borders"
STEM = "german-denmark-1914-north-schleswig-001-026"
PREFIX = STEM + ".0619-hugum-stage4"
REPORT = DATA / (PREFIX + ".json")
SEGMENT = DATA / (PREFIX + ".geojson")
PREVIEW = ROOT / "tools/historical-library/review-output/north-schleswig-1914-001-026/0619-hugum"
PERIOD = "1914-07-31"
DISPLAY_WIDTH_CSS = 2560
DISPLAY_MAX_FLAT_ZOOM = 64
STOP_THRESHOLD_CSS_PX = 0.5
METRIC = Transformer.from_crs("EPSG:4326", "EPSG:25832", always_xy=True).transform
WGS84 = Transformer.from_crs("EPSG:25832", "EPSG:4326", always_xy=True).transform
OFFICIAL_RIVER_REGULATIVE = ("https://www.esbjerg.dk/Files/Filer/Energi%20og%20"
    "milj%C3%B8/Vandl%C3%B8b%20og%20s%C3%B8er/"
    "vandl%C3%B8bsregulativer/Gels%C3%A5%20nedre%20del%20ved%20%C3%85rup.pdf")


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def write(path, obj):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def sheet_line(sheet, fc):
    rows=[f for f in fc["features"] if f.get("properties",{}).get("sheetId")==sheet]
    if len(rows)!=1 or rows[0]["geometry"]["type"]!="MultiLineString":
        raise RuntimeError(f"Invalid sheet {sheet} candidate")
    parts=rows[0]["geometry"]["coordinates"]
    if len(parts)!=1:
        raise RuntimeError(f"Sheet {sheet} unexpectedly non-contiguous")
    return transform(METRIC, LineString(parts[0]))


def getmap(session, sheet, bbox, width, height):
    # Use existing 0719 WMS source provenance, including 0619 metadata.
    return prior.wms_image(session, sheet, bbox, width, height)


def bbox_for(line, pad_lon=.0075, pad_lat=.005):
    w,s,e,n=transform(WGS84,line).bounds
    bbox=[w-pad_lon,s-pad_lat,e+pad_lon,n+pad_lat]
    if not (bbox[0]<bbox[2] and bbox[1]<bbox[3]):
        raise RuntimeError("Invalid geographic panel bounds")
    return bbox


def tile(session, name, line, sheets, title, width=1700, height=1150,
         explicit_bbox=None):
    bounds=list(explicit_bbox or bbox_for(line))
    layers=[getmap(session, sheet, bounds, width, height) for sheet in sheets]
    picture=Image.new("RGBA",(width,height),(255,255,255,0))
    for raster, meta in layers:
        picture.alpha_composite(raster)
    original=Image.new("RGB",(width,height),"white")
    original.paste(picture,mask=picture.getchannel("A"))
    originalFile=f"{name}-1878-source.jpg"
    original.save(PREVIEW/originalFile,"JPEG",quality=80,optimize=True)

    painted=Image.new("RGB",(width,height+72),"white")
    painted.paste(original,(0,35))
    draw=ImageDraw.Draw(painted)
    draw.rectangle((0,0,width,34),fill=(28,44,58))
    draw.text((14,9),title+" | 1914 border candidate, not historical trace",
              font=prior.prior.common.nice_font(18),fill="white")
    w,s,e,n=bounds
    pts=[((x-w)/(e-w)*(width-1),35+(n-y)/(n-s)*(height-1))
         for x,y in transform(WGS84,line).coords]
    if len(pts)<2:
        raise RuntimeError("Too few map points")
    draw.line(pts,fill="white",width=8)
    draw.line(pts,fill=(220,39,42),width=4)
    for i,point in enumerate((pts[0],pts[-1])):
        x,y=point
        draw.ellipse((x-8,y-8,x+8,y+8),fill=(255,222,16),outline="black",width=2)
        draw.text((x+12,y-22),"START" if i==0 else "END",
                  font=prior.prior.common.nice_font(16),fill=(116,13,24),
                  stroke_width=2,stroke_fill="white")
    draw.rectangle((0,height+35,width,height+72),fill=(28,44,58))
    draw.text((14,height+46),
        "RED: modern candidate river. Historic 1878 centreline/bank not independently digitized.",
        font=prior.prior.common.nice_font(16),fill="white")
    overlayFile=f"{name}-overlay.jpg"
    painted.save(PREVIEW/overlayFile,"JPEG",quality=80,optimize=True)
    return {
      "panel":name,"title":title,"sourceSheetIds":list(sheets),
      "extentLonLat":[round(z,9) for z in bounds],
      "sourceImage":originalFile,"overlayImage":overlayFile,
      "originalWms":[meta for image,meta in layers],
      "overlaySha256":hashlib.sha256((PREVIEW/overlayFile).read_bytes()).hexdigest(),
      "candidateLengthKm":round(line.length/1000,5),
      "independentHistoricalCartographyComparisonPerformed":False
    }


def main():
    sheets=read(DATA/(STEM+".sheet-segments.geojson"))
    prev=sheet_line("0719",sheets)
    current=sheet_line("0619",sheets)
    post=read(DATA/(STEM+".stone26-transition.provisional.geojson"))["features"][1]
    post_line=transform(METRIC,shape(post["geometry"]))
    master=read(DATA/(STEM+".geojson"))["features"][0]
    if not master["properties"].get("doNotTreatAsFinal"):
        raise RuntimeError("Working master cannot become final")
    if not post["properties"]["doNotTreatAsFinal"]:
        raise RuntimeError("Stone 26 provisional reach unexpectedly finalized")
    if not prev.is_simple or not current.is_simple or not post_line.is_simple:
        raise RuntimeError("Working source geometry self-intersects")
    if not 5800<current.length<6200:
        raise RuntimeError(f"Unexpected 0619 candidate length {current.length}")
    joining=Point(prev.coords[-1]).distance(Point(current.coords[0]))
    post_join=Point(post_line.coords[-1]).distance(Point(current.coords[-1]))
    line_to_post=max(current.distance(Point(current.coords[0])),
                     post_line.distance(Point(current.coords[0])))
    if max(joining,post_join,line_to_post)>0.01:
        raise RuntimeError("0619 candidate seam or post26 river no longer aligns")

    source_report=read(DATA/(STEM+".slub-discovery.json"))
    historic=source_report["matches"]["0619"]
    if len(historic)!=1:
        raise RuntimeError("Hügum source ambiguous")
    metadata=historic[0]["metadata"]
    if metadata["title"]!="Hügum" or metadata["map_scale"]!=25000:
        raise RuntimeError("Original map source metadata changed")
    if not metadata.get("time_period_start","").startswith("1878"):
        raise RuntimeError("Hügum catalog issue year changed")

    PREVIEW.mkdir(parents=True,exist_ok=True)
    session=requests.Session()
    d=current.length/3
    thirds=[substring(current,i*d,min((i+1)*d,current.length)) for i in range(3)]
    near_prev=substring(prev,max(0,prev.length-550),prev.length)
    near_current=substring(current,0,min(550,current.length))
    seam=LineString(list(near_prev.coords)+list(near_current.coords)[1:])
    if not seam.is_simple: raise RuntimeError("Historical 0719–0619 seam self-intersecting")
    end_near=substring(current,max(0,current.length-1250),current.length)
    panels=[
      tile(session,"0619-overview",current,["0619"],"Hügum 1878 | entire post-26 sheet",width=1850,height=1250),
      tile(session,"0619-east",thirds[0],["0619"],"Hügum downstream candidate, eastern third"),
      tile(session,"0619-middle",thirds[1],["0619"],"Hügum downstream candidate, central third"),
      tile(session,"0619-west",thirds[2],["0619"],"Hügum downstream candidate, western third"),
      tile(session,"0719-0619-join",seam,["0719","0619"],"Spandet 1880/Hügum 1878 sheet join",
           width=1550,height=1050),
      tile(session,"0619-confluence",end_near,["0619"],"Gels Å / Flads Å confluence",
           width=1800,height=1200,explicit_bbox=[8.878,55.313,8.899,55.327]),
    ]
    clipped=transform(WGS84,current)
    write(SEGMENT,{
      "type":"FeatureCollection",
      "name":"Spandet→Hügum continued Gels Å 0619 candidate, provisional",
      "features":[{
        "type":"Feature",
        "id":"candidate:1914:german-denmark:post26-gelsa:0619",
        "properties":{
            "referenceDate":PERIOD,
            "sourceSheet":"0619 Hügum",
            "historicalSourceYear":1878,
            "source":"Working modern parish/river boundary, sheet segment",
            "status":"overlay-prepared-not-yet-reviewed",
            "doNotTreatAsFinal":True,
            "independentlyDigitizedHistoricCourse":False,
            "lengthKm":round(current.length/1000,5)
        },
        "geometry":mapping(clipped)
      }]
    })
    px=prior.prior.screen_geometry(current)
    report={
        "schemaVersion":1,"referenceDate":PERIOD,
        "workUnit":"1914-German-Danish-border/post-26-Gels-Aa/0619-Hugum/stage-4",
        "sourceSheet":{
            "id":"0619","title":"Hügum",
            "surveyYear":1878,"catalogPublicationYear":1878,
            "mapScale":25000,
            "sourcePermalink":"https://www.deutschefotothek.de/documents/obj/71051510",
            "sourceWmsUrl":"https://wms.kartenforum.slub-dresden.de/map/10006020",
            "sourceWmsLayer":"df_dk_0010001_0619",
            "lastMapRevisionYearVerified":False,
        },
        "sourceHierarchy":{
            "legalBorder":"1865 international border regulation Art I §§2–3: Gels Å from marker26 to Flads Å confluence, then along the RIGHT BANK of Flads Å to stone27.",
            "protocol":"https://da.wikisource.org/wiki/Freden_i_Wien_(1864)_Gr%C3%A6nsereguleringskommissionen",
            "municipalModernRiverRegulation":OFFICIAL_RIVER_REGULATIVE,
            "modernRegulationReachMeters":6400,
            "modernRegulationReferencePoints":"west edge of Gelsbro bridge to confluence, not the historical stone26 hinge",
            "modernRegulationNaturalStatePhrase":True,
            "modernRegulationAdoptionEpoch":"post-1923; not 1914 evidence",
            "laterWaterworksPotentiallyRelevant":True,
            "independentSourceOnMeanders":"https://graenseforeningen.dk/leksikon/gels-aa"
        },
        "geometry":{
            "sheetLengthKm":round(current.length/1000,5),
            "post26EntireCandidateLengthKm":round(post_line.length/1000,5),
            "post26RemainderBefore0619Km":round((post_line.length-current.length)/1000,5),
            "sourceVertexCount":len(clipped.coords),
            "startLonLat":list(clipped.coords[0]),
            "endLonLat":list(clipped.coords[-1]),
            "west0719ToNorth0619JoiningGapM":round(joining,6),
            "0619EndToPost26EndpointGapM":round(post_join,6),
            "sourceGeometrySimple":bool(current.is_simple),
            "combinedSheetSeamSimple":bool(seam.is_simple),
            "preservedCanonicalMaster":True,
            "renderDisplayEstimate":px,
        },
        "historicVisualReviewPerformed":False,
        "historicSourceLineDigitized":False,
        "historicDeviationMeasuredCssPx":None,
        "historicStopCriterionPassed":None,
        "fladsAaConfluenceHistoricallyValidated":False,
        "point27OrFladsAaRightBankAudited":False,
        "riverChannelModifiedInMaster":False,
        "status":"provisional:pre1914-raster-panels-generated-needs-human-review",
        "images":panels,
        "actionRunId":os.environ.get("GITHUB_RUN_ID"),
        "nextActions":[
            "Inspect high-level Gels Å meanders in 1878 Hügum raster, no unnecessary per-bend microtracing.",
            "Verify candidate ends at the Flads Å confluence shown in period map.",
            "Record only visible discrepancies >0.5 CSSpx at 2560xflatZoom64 and all historical/topological exceptions.",
            "If no independently digitized historic line, DO NOT claim 0.5 CSSpx historical validation.",
            "Do not extend border west of confluence until the 1865 treaty's RIGHT BANK of Flads Å is checked."
        ],
        "artifactPath":str(PREVIEW.relative_to(ROOT))
    }
    write(REPORT,report)
    print(json.dumps({
        "sheet":"0619 Hügum",
        "km":report["geometry"]["sheetLengthKm"],
        "post26Km":report["geometry"]["post26EntireCandidateLengthKm"],
        "seamGapMeters":joining,
        "confluenceEndpointGapMeters":post_join,
        "renderLengthCssPx":px["candidateDisplayedLengthCssPx"],
        "vertices":px["candidateVertexCount"],
        "simplifiedVertices":px["illustrativeSimplifiedVertexCount"],
        "previewCount":len(panels),
        "run":report["actionRunId"],
        "historicVerified":False
    },ensure_ascii=False,indent=2))
    if os.environ.get("GIS_LOG_MAP_THUMBNAILS")=="1":
        for panel in panels:
            img=Image.open(PREVIEW/panel["overlayImage"])
            img.thumbnail((1200,900))
            buffer=io.BytesIO()
            img.save(buffer,"JPEG",quality=64,optimize=True)
            tag=panel["panel"].upper().replace("-","_")
            print(f"===BEGIN_HUGUM4_{tag}_JPEG===")
            print(base64.b64encode(buffer.getvalue()).decode("ascii"))
            print(f"===END_HUGUM4_{tag}_JPEG===")


if __name__=="__main__":
    main()
