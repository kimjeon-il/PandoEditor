#!/usr/bin/env python3
"""Investigate stone 26 at Gjelsbro and the unseparated Gels Å river border.

Only writes derived provisional segments and source-based diagnostic metadata.
Does NOT move or replace the 1–26 working master, claim to locate the historical
stone exactly, or certify a historically independent 1914 border.
"""
import argparse
import io
import hashlib
import json
import math
import os
from pathlib import Path

import requests
from PIL import Image, ImageDraw, ImageFont
from pyproj import Transformer
from shapely.geometry import LineString, Point, mapping, shape
from shapely.ops import transform

ROOT = Path(__file__).resolve().parents[1]
B = ROOT / "tools/historical-library/land-borders"
SRC = ROOT / "tools/historical-library/sources/german-empire-1914/north-schleswig-001-026"
STEM = "german-denmark-1914-north-schleswig-001-026"
AUDIT = B / (STEM + ".stone26-transition.audit.json")
PARTS = B / (STEM + ".stone26-transition.provisional.geojson")
VIEW = ROOT / "tools/historical-library/review-output/north-schleswig-1914-001-026/stone26-transition"
WMS = "https://wms.kartenforum.slub-dresden.de/map/10006007"
LAYER = "df_dk_0010001_0719"
TO_M = Transformer.from_crs("EPSG:4326", "EPSG:25832", always_xy=True).transform
TO_LL = Transformer.from_crs("EPSG:25832", "EPSG:4326", always_xy=True).transform
PROTOCOL = "https://da.wikisource.org/wiki/Freden_i_Wien_(1864)_Gr%C3%A6nsereguleringskommissionen"


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def write(path, obj):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def font(n=16):
    for filename in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                     "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"):
        try:
            return ImageFont.truetype(filename, n)
        except OSError:
            pass
    return ImageFont.load_default()


def heading_change(p1, p2, p3):
    d1 = (p2[0]-p1[0], p2[1]-p1[1])
    d2 = (p3[0]-p2[0], p3[1]-p2[1])
    return abs(math.degrees(math.atan2(
        d1[0]*d2[1]-d1[1]*d2[0],
        d1[0]*d2[0]+d1[1]*d2[1])))


def find_transition(line, river, bridge):
    pts = list(line.coords)
    acc = 0
    matches = []
    for i in range(1,len(pts)-1):
        acc += Point(pts[i-1]).distance(Point(pts[i]))
        if not (18_000 < acc < 22_000):
            continue
        pt=Point(pts[i])
        if pt.distance(river) > 8 or pt.distance(bridge) > 240:
            continue
        angle = heading_change(pts[i-1],pts[i],pts[i+1])
        if angle < 55:
            continue
        before = line.interpolate(max(0, acc-300)).distance(river)
        after = line.interpolate(min(line.length, acc+300)).distance(river)
        if before < 100 or after > 30:
            continue
        matches.append({
            "vertexIndex":i,
            "alongWorkingMasterKm":round(acc/1000,5),
            "turnAngleDegrees":round(angle,3),
            "sourceCurrentRiverDistanceM":round(pt.distance(river),3),
            "contemporaryBridgeCenterDistanceM":round(pt.distance(bridge),3),
            "riverDistance300mBeforeM":round(before,3),
            "riverDistance300mAfterM":round(after,3),
            "pointLonLat":list(transform(TO_LL,pt).coords[0]),
            "method":"Modern parish-line hinge and OSM Gels Å proximity; NOT a historical stone fix."
        })
    if not matches:
        raise RuntimeError("No plausible waterway-entry candidate hinge found")
    # Later river meanders can also make sharp turns within 300 m of the
    # approach. Take the FIRST credible waterway-entry hinge, preserving
    # alternative sharp bends for independent period-map cross-checking.
    selected = matches[0]
    selected["otherCloseByTurnCandidates"] = [
        {"vertexIndex":v["vertexIndex"],
         "alongWorkingMasterKm":v["alongWorkingMasterKm"]}
        for v in matches[1:]
    ]
    return selected


def project_coords(line,bbox,size):
    w,s,e,n=bbox
    return [((lon-w)/(e-w)*(size[0]-1), (n-lat)/(n-s)*(size[1]-1))
            for lon,lat in transform(TO_LL,line).coords]


def draw_sample(session,bbox,name,title,before,after,bridge,current):
    w,s,e,n=bbox
    width,height=1680,1080
    params={
      "SERVICE":"WMS","VERSION":"1.3.0","REQUEST":"GetMap",
      "LAYERS":LAYER,"STYLES":"","CRS":"EPSG:4326",
      "BBOX":f"{s},{w},{n},{e}",
      "WIDTH":width,"HEIGHT":height,"FORMAT":"image/png","TRANSPARENT":"true"
    }
    resp=session.get(WMS,params=params,timeout=80,headers={
        "User-Agent":"PandoEditor-HistoricalGIS-Gjelsbro26/1.0"})
    resp.raise_for_status()
    im=Image.open(io.BytesIO(resp.content)).convert("RGBA")
    if im.size!=(width,height):
        raise RuntimeError(f"Historic sheet unexpected size: {im.size}")
    if im.getchannel("A").getbbox() is None:
        raise RuntimeError("Original historic WMS has no map content")
    background=Image.new("RGB",(width,height),"white")
    background.paste(im,mask=im.getchannel("A"))
    original=VIEW/(name+"-source-1880.jpg")
    background.save(original,format="JPEG",quality=83,optimize=True)

    overlay=Image.new("RGB",(width,height+70),"white")
    overlay.paste(background,(0,35))
    d=ImageDraw.Draw(overlay)
    d.rectangle((0,0,width,34),fill=(28,42,54))
    d.text((14,8),title+" | provisional hinge, not stone-26 survey",
           font=font(19),fill="white")
    for geom,color in ((before,(214,37,51)),(after,(47,93,182))):
        xy=[(x,y+35) for x,y in project_coords(geom,bbox,(width,height))]
        if len(xy)<2: continue
        d.line(xy,fill="white",width=9)
        d.line(xy,fill=color,width=5)
    bx,by=transform(TO_LL,bridge).coords[0]
    x,y=(bx-w)/(e-w)*(width-1),(n-by)/(n-s)*(height-1)+35
    d.ellipse((x-9,y-9,x+9,y+9),fill=(22,150,154),outline="black",width=2)
    d.text((x+13,y-21),"modern bridge centre",font=font(17),fill=(0,100,105),
           stroke_width=2,stroke_fill="white")
    cx,cy=transform(TO_LL,current).coords[0]
    px,py=(cx-w)/(e-w)*(width-1),(n-cy)/(n-s)*(height-1)+35
    d.ellipse((px-9,py-9,px+9,py+9),fill=(255,224,14),outline="black",width=2)
    d.text((px+12,py-28),"provisional hinge",font=font(17),fill=(142,0,75),
           stroke_width=2,stroke_fill="white")
    d.rectangle((0,height+35,width,height+70),fill=(28,42,54))
    d.text((12,height+43),"RED: before bend; BLUE: Gels Aa reach; cyan: present bridge. No exact stone 26 fix.",
           font=font(17),fill="white")
    dest=VIEW/(name+"-review.jpg")
    overlay.save(dest,format="JPEG",quality=83,optimize=True)
    return {
        "name":name,"bboxLonLat":[round(v,8) for v in bbox],
        "wmsLayer":LAYER,"wmsImageSha256":hashlib.sha256(resp.content).hexdigest(),
        "wmsBytes":len(resp.content),
        "sourcePreview":original.name,"overlayPreview":dest.name,
        "overlaySha256":hashlib.sha256(dest.read_bytes()).hexdigest()
    }


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--raster-panels",action="store_true")
    args=parser.parse_args()

    master_fc=read(B/(STEM+".geojson"))
    features=master_fc["features"]
    if len(features)!=1 or features[0]["geometry"]["type"]!="LineString":
        raise RuntimeError("Original working master unexpectedly changed")
    master=shape(features[0]["geometry"])
    if len(master.coords)!=1343 or not master.is_simple:
        raise RuntimeError("Master point count or simplicity changed")
    if features[0]["properties"].get("doNotTreatAsFinal") is not True:
        raise RuntimeError("Cannot audit a boundary promoted to final")

    context=read(SRC/"osm-endpoint-context.json")
    river_features=[w for w in context["ways"]
                    if w.get("tags",{}).get("waterway")=="river"
                    and w.get("tags",{}).get("name")=="Gels Å"]
    bridge_features=[w for w in context["ways"]
                    if w.get("tags",{}).get("man_made")=="bridge"
                    and w.get("tags",{}).get("name")=="Gelsbro"]
    if len(river_features)!=1 or len(bridge_features)!=1:
        raise RuntimeError("Missing uniquely indexed modern river/bridge")
    river=transform(TO_M,LineString(river_features[0]["geometry"]))
    bridge_coords=bridge_features[0]["geometry"]
    bridge=transform(TO_M,Point(
        sum(p[0] for p in bridge_coords)/len(bridge_coords),
        sum(p[1] for p in bridge_coords)/len(bridge_coords)))
    line=transform(TO_M,master)
    hinge=find_transition(line,river,bridge)
    cut=hinge["vertexIndex"]
    if cut!=481:
        raise RuntimeError(f"Candidate cut unexpectedly shifted from source feature: {cut}")

    pre=LineString(list(master.coords)[:cut+1])
    post=LineString(list(master.coords)[cut:])
    if (len(pre.coords)+len(post.coords)-1)!=len(master.coords):
        raise RuntimeError("Split lost/duplicated a source vertex")
    if pre.coords[-1]!=post.coords[0]:
        raise RuntimeError("Derived endpoints do not match")
    from shapely.ops import transform as s_transform
    preM=s_transform(TO_M,pre);postM=s_transform(TO_M,post)
    if abs(preM.length+postM.length-line.length)>0.001:
        raise RuntimeError("Split length mismatch")
    if not (pre.is_simple and post.is_simple):
        raise RuntimeError("Sub-lines intersect themselves")

    river_end=Point(river.coords[-1])
    endpoint_distance=Point(line.coords[-1]).distance(river_end)
    if endpoint_distance>5:
        raise RuntimeError("Modern OSM Gels Å endpoint has drifted")
    features=[
        {
            "type":"Feature",
            "id":"candidate:deu-dnk:1914:stone1-to-stone26-provisional",
            "properties":{
                "referenceDate":"1914-07-31","status":"provisional-cut-at-modern-waterway-hinge",
                "historicalStone26ExactLocationConfirmed":False,
                "doNotTreatAsFinal":True,"lengthKm":round(preM.length/1000,5),
                "sourceMaster":STEM+".geojson","sourceVertexRangeInclusive":[0,cut],
            },
            "geometry":mapping(pre)
        },
        {
            "type":"Feature",
            "id":"candidate:deu-dnk:1914:gelsa-from-26-to-fladsa-provisional",
            "properties":{
                "referenceDate":"1914-07-31","status":"provisional-historical-river-course-unverified",
                "historicalStone26ExactLocationConfirmed":False,
                "historicalRiverCourseVerified":False,"doNotTreatAsFinal":True,
                "lengthKm":round(postM.length/1000,5),
                "sourceMaster":STEM+".geojson","sourceVertexRangeInclusive":[cut,len(master.coords)-1],
            },
            "geometry":mapping(post)
        }
    ]
    write(PARTS,{
        "type":"FeatureCollection",
        "name":"German-Danish 1914 working border split at modern river-entry candidate, NOT certified stone 26",
        "features":features})

    audit={
        "schemaVersion":1,
        "referenceDate":"1914-07-31",
        "githubActionsRunId":os.environ.get("GITHUB_RUN_ID"),
        "status":"provisional-geometry-split-ready-stone26-historical-fix-unverified",
        "primaryHistoricalProtocol":PROTOCOL,
        "historicalLaw":{
            "1to26":"Article I §2: southern parish boundaries to stone22, eastern Seem parish boundary to stone26 at Gjels Å near Gjelsbro.",
            "riverAfter26":"Article I §2: from stone26 Gels Å is border until it joins Flads Å.",
            "afterConfluence":"Article I §3: follows right bank of Flads Å and southern Obbekær boundary to stone27.",
        },
        "otherSources":[
            "https://graenseforeningen.dk/leksikon/gels-aa",
            "https://graenseforeningen.dk/om-graenselandet/genforeningssten/harreby",
            "https://www.deutschefotothek.de/documents/obj/71051523"
        ],
        "currentMasterUnmodified":True,
        "sourceMasterVertexCount":len(master.coords),
        "sourceMasterLengthKm":round(line.length/1000,5),
        "splitCandidate":hinge,
        "classification":"Modern parish/river topology change only; 1880 stone26 label requires independent raster inspection.",
        "twoDerivedParts":{
            "stone1through26Candidate":{
                "lengthKm":round(preM.length/1000,5),
                "vertexCount":len(pre.coords),
                "endLonLat":list(pre.coords[-1]),
            },
            "gelsARiverCandidate":{
                "lengthKm":round(postM.length/1000,5),
                "vertexCount":len(post.coords),
                "startLonLat":list(post.coords[0]),
                "endLonLat":list(post.coords[-1]),
            }
        },
        "sourceTopologyChecks":{
            "splitEndpointMismatchMeters":round(Point(preM.coords[-1]).distance(Point(postM.coords[0])),6),
            "lengthDifferenceMeters":round(line.length-(preM.length+postM.length),6),
            "modernBridgeToCandidateHingeMeters":round(bridge.distance(Point(preM.coords[-1])),3),
            "modernRiverDistanceAtCandidateHingeMeters":round(river.distance(Point(preM.coords[-1])),3),
            "modernGelsAEndToCandidateEndMeters":round(endpoint_distance,3),
            "bothPartsSimple":pre.is_simple and post.is_simple,
        },
        "historicalGeometryChangeApproved":False,
        "historicalMarkerLocationConfirmed":False,
        "historicalRiverMeanderAccuracyMeasured":False,
        "cartographicSourceScale":25000,
        "webScreenReferenceCssPx":2560,
        "webFlatMaxZoom":64,
        "sourceScreenDeviationVersusHistoricBorderPx":None,
        "historicalThresholdPassed":None,
        "continuationClassification":[
            "Stone 26 position vs 1880 map is the next cartographic review, not established by modern geometry.",
            "Do not use present road/bridge coordinate as original stone.",
            "Confluence with Flads Å is an independent source-based river-network assertion, not proven solely by an OSM way endpoint.",
            "Check early twentieth century channel modifications before final river-border cut.",
        ],
        "rasterPanels":[],
    }
    if args.raster_panels:
        VIEW.mkdir(parents=True,exist_ok=True)
        session=requests.Session()
        x,y=pre.coords[-1]
        widths=[
            ("Gjelsbro-overview", [8.9185,55.2888,8.936,55.2997]),
            ("stone26-closeup", [8.923,55.2934,8.932,55.2994]),
        ]
        for name,bbox in widths:
            audit["rasterPanels"].append(draw_sample(
                session,bbox,name,name,pre,post,bridge,Point(preM.coords[-1])))
    write(AUDIT,audit)
    if args.raster_panels and os.environ.get("GIS_LOG_MAP_THUMBNAILS") == "1":
        import base64
        for panel in audit["rasterPanels"]:
            img = Image.open(VIEW / panel["overlayPreview"])
            img.thumbnail((1280, 900))
            buf = io.BytesIO()
            img.save(buf, format="JPEG", quality=67, optimize=True)
            tag = panel["name"].upper().replace("-", "_")
            print(f"===BEGIN_STONE26_{tag}_JPEG===")
            print(base64.b64encode(buf.getvalue()).decode("ascii"))
            print(f"===END_STONE26_{tag}_JPEG===")
    print(json.dumps({
        "candidateCutVertex":cut,
        "candidateLonLat":hinge["pointLonLat"],
        "estimatedStone1to26CandidateKm":audit["twoDerivedParts"]["stone1through26Candidate"]["lengthKm"],
        "post26RiverCandidateKm":audit["twoDerivedParts"]["gelsARiverCandidate"]["lengthKm"],
        "turnAngleDegrees":hinge["turnAngleDegrees"],
        "topology":audit["sourceTopologyChecks"],
        "rasterPanels":[v["name"] for v in audit["rasterPanels"]],
        "historicalStoneFixConfirmed":False,
    },ensure_ascii=False,indent=2))


if __name__=="__main__":
    main()
