#!/usr/bin/env python3
"""Stage 1: inspect west half of 0718 Hvidding against its georeferenced 1880 map.

Deliberately NON-DESTRUCTIVE. Generated geometry is only a clipping of the
modern-parish candidate. No historical boundary has been independently traced.
Images are temporary workflow artifacts, never committed to Git.
"""
import base64
import hashlib
import io
import json
import math
import os
from pathlib import Path

import requests
from PIL import Image, ImageDraw, ImageFont, ImageStat
from pyproj import Transformer
from shapely.geometry import LineString, Point, mapping, shape
from shapely.ops import substring, transform, nearest_points

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "tools/historical-library"
BORDER = WORK / "land-borders"
SOURCE = WORK / "sources/german-empire-1914/north-schleswig-001-026"
STEM = "german-denmark-1914-north-schleswig-001-026"
REPORT = BORDER / f"{STEM}.0718-west-stage1.json"
GEOMETRY = BORDER / f"{STEM}.0718-west-stage1.geojson"
ARTIFACT = WORK / "review-output/north-schleswig-1914-001-026/0718-west"
URL = "https://wms.kartenforum.slub-dresden.de/map/10006008"
PERMALINK = "https://www.deutschefotothek.de/documents/obj/71051522"
LAYER = "df_dk_0010001_0718"
TO_M = Transformer.from_crs("EPSG:4326", "EPSG:25832", always_xy=True).transform
TO_LL = Transformer.from_crs("EPSG:25832", "EPSG:4326", always_xy=True).transform


def load(path):
    return json.loads(path.read_text(encoding="utf-8"))


def dump(path, obj):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def nice_font(size=15):
    try:
        return ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",size)
    except OSError:
        return ImageFont.load_default()


def sample_points(line, n=14):
    return [[round(c, 8) for c in transform(TO_LL, line.interpolate(line.length*i/n)).coords[0]]
            for i in range(n+1)]


def get_wms(session, bbox, width, height):
    west, south, east, north = bbox
    params = {
        "SERVICE": "WMS", "VERSION": "1.3.0", "REQUEST": "GetMap",
        "LAYERS": LAYER, "STYLES": "", "CRS": "EPSG:4326",
        "BBOX": f"{south},{west},{north},{east}",  # WMS 1.3.0: lat,lon
        "WIDTH": str(width), "HEIGHT": str(height),
        "FORMAT": "image/png", "TRANSPARENT": "true",
    }
    r=session.get(URL,params=params,headers={
        "User-Agent":"PandoEditor-HistoricalGIS-Stage1/1.0"},timeout=80)
    r.raise_for_status()
    image=Image.open(io.BytesIO(r.content))
    image.load()
    if image.size != (width,height):
        raise RuntimeError(f"WMS returned wrong image size: {image.size}")
    rgba=image.convert("RGBA")
    # Reject blank/transparent/error image, not merely a HTTP 200.
    extrema=rgba.getextrema()
    if max(hi-lo for lo,hi in extrema[:3]) < 20:
        raise RuntimeError(f"Raster appears blank: {extrema}")
    return rgba,{"mime":r.headers.get("Content-Type"),"bytes":len(r.content),
                 "sha256":hashlib.sha256(r.content).hexdigest(),
                 "imageDimensions":[width,height],"channelExtrema":extrema}


def bbox_around(line, buffer_x=0.012,buffer_y=0.0085):
    w,s,e,n=transform(TO_LL,line).bounds
    return [max(8.6666666667,w-buffer_x),max(55.2,s-buffer_y),
            min(8.8333333333,e+buffer_x),min(55.3,n+buffer_y)]


def render(session,line,label,filebase,quality=77,width=1750,height=1150):
    bbox=bbox_around(line)
    source,request=get_wms(session,bbox,width,height)
    original_path=ARTIFACT / f"{filebase}-source-1880.jpg"
    original=Image.new("RGB",source.size,"white")
    original.paste(source,mask=source.getchannel("A"))
    original.save(original_path,format="JPEG",quality=quality,optimize=True)
    canvas=Image.new("RGB",(width,height+74),"white")
    canvas.paste(original,(0,36))
    draw=ImageDraw.Draw(canvas)
    draw.rectangle((0,0,width,35),fill=(25,38,53))
    draw.text((12,8),label+"  |  Hvidding 1880, 1:25,000",
              font=nice_font(19),fill="white")
    w,s,e,n=bbox
    points=[(round((lon-w)/(e-w)*(width-1)),
             round(36+(n-lat)/(n-s)*(height-1)))
            for lon,lat in transform(TO_LL,line).coords]
    draw.line(points,fill="white",width=8,joint="curve")
    draw.line(points,fill=(219,30,54),width=4,joint="curve")
    for i,c in enumerate([points[0],points[-1]]):
        x,y=c
        draw.ellipse((x-7,y-7,x+7,y+7),fill=(255,205,10),outline=(33,20,20),width=2)
        draw.text((x+12,y-21),"START" if i==0 else "END",font=nice_font(),fill=(105,8,15),
                  stroke_width=2,stroke_fill="white")
    draw.rectangle((0,height+36,width,height+74),fill=(25,38,53))
    draw.text((12,height+45),"RED = unverified present-day-parish candidate; historic map line NOT yet digitized.",
              fill="white",font=nice_font(17))
    overlay_path=ARTIFACT / f"{filebase}-overlay.jpg"
    canvas.save(overlay_path,format="JPEG",quality=quality,optimize=True)
    request.update({"bboxLonLat":[round(v,8) for v in bbox],
                    "wmsUrl":URL,"layer":LAYER,
                    "sourceArtifactFile":original_path.name,
                    "overlayArtifactFile":overlay_path.name,
                    "overlaySha256":hashlib.sha256(overlay_path.read_bytes()).hexdigest()})
    return request


def closest_named_ditch(line):
    data=load(SOURCE / "osm-historic-border-candidates.json")
    geometries=[]
    for v in data.get("ways",[]):
        if v.get("tags",{}).get("name")!="Grænsegrøften":
            continue
        coords=v.get("geometry") or []
        if len(coords)>1:
            geometries.append(transform(TO_M,LineString(coords)))
    if not geometries:
        return {"status":"not-available"}
    samples=[line.interpolate(line.length*i/120).distance(
             min(geometries,key=lambda geom:line.interpolate(line.length*i/120).distance(geom)))
             for i in range(121)]
    return {"source":"OSM mapped fragments named Grænsegrøften",
            "independentHistoricalValidation":False,
            "sampleCount":len(samples),
            "medianNearestDitchDistanceM":round(sorted(samples)[len(samples)//2],1),
            "shareWithin10m":round(sum(d<=10 for d in samples)/len(samples),3),
            "shareWithin25m":round(sum(d<=25 for d in samples)/len(samples),3),
            "shareWithin100m":round(sum(d<=100 for d in samples)/len(samples),3),
            "warning":"Incomplete contemporary OSM ditch segments. No inference of 1914 position."}


def main():
    parts=load(BORDER / f"{STEM}.sheet-segments.geojson")["features"]
    hit=[f for f in parts if f["properties"]["sheetId"]=="0718"]
    if len(hit)!=1 or hit[0]["geometry"]["type"]!="MultiLineString":
        raise RuntimeError("Expected exactly one 0718 MultiLineString feature")
    lines=hit[0]["geometry"]["coordinates"]
    if len(lines)!=1:
        raise RuntimeError("0718 is not a single contiguous candidate line")
    full=transform(TO_M,LineString(lines[0]))
    if not full.is_simple or full.length<11000 or full.length>13000:
        raise RuntimeError("Candidate length or topology unexpected")
    # The source coordinates run west-to-east from 0717 to 0719.
    west=substring(full,0,full.length/2)
    east=substring(full,full.length/2,full.length)
    if len(west.coords)<2 or len(east.coords)<2:
        raise RuntimeError("Empty half line")
    if Point(west.coords[-1]).distance(Point(east.coords[0]))>0.0001:
        raise RuntimeError("Western and eastern candidate halves do not join")
    if abs(west.length+east.length-full.length)>0.001:
        raise RuntimeError("Halves must sum to complete original candidate")
    west_ll=transform(TO_LL,west)
    geometry={
        "type":"FeatureCollection",
        "name":"german-denmark-1914-hvidding-0718-west-candidate",
        "features":[{
            "type":"Feature",
            "id":"land-border:deu-dnk:1914:north-schleswig:0718-west-stage1",
            "properties":{
                "referenceDate":"1914-07-31",
                "source":"1914 north-schleswig 1–26 candidate, clipped at 50% 0718 length",
                "sourceSheet":"0718 Hvidding, 1880 1:25,000",
                "status":"provisional-modern-parish-only",
                "historicalMapTraced":False,
                "doNotTreatAsFinal":True,
                "lengthKm":round(west.length/1000,5)
            },
            "geometry":mapping(west_ll)
        }]
    }
    dump(GEOMETRY,geometry)

    ARTIFACT.mkdir(parents=True,exist_ok=True)
    session=requests.Session()
    # Split 0718 western half again into two legible cartographic panels.
    quarters=[substring(west,0,west.length/2),
              substring(west,west.length/2,west.length)]
    panels=[]
    panels.append({"panel":"overview","map":render(
        session,west,"Stage 1: 0718 west 0–50%","0718-west-overview")})
    for i,line in enumerate(quarters,1):
        panels.append({"panel":f"subpanel-{i}","map":render(
            session,line,f"Stage 1: 0718 west subpanel {i}/2",
            f"0718-west-{i:02d}",width=1700,height=1300)})

    report={
      "schemaVersion":1,
      "workUnit":"1914-German-Danish-border/0718-west/step-1",
      "referenceDate":"1914-07-31",
      "sourceSheet":{"id":"0718","title":"Hvidding","issueYear":1880,
         "scale":25000,"mapId":"10006008","permalink":PERMALINK,
         "wms":URL,"layer":LAYER},
      "workingBorderStatus":"provisional-no-original-tracing",
      "originalRasterAccessible":True,
      "originalRasterVisuallyOverlayRendered":True,
      "independentHistoricBoundaryDigitizationCompleted":False,
      "candidateGeometryModified":False,
      "historicBorderDisplacementMeasured":False,
      "measurementBlockedBy":"Historic boundary line on the 1880 map must be manually identified, then digitized and checked against 1914 revisions.",
      "candidate":{
          "entireSheetLengthKm":round(full.length/1000,5),
          "westernHalfLengthKm":round(west.length/1000,5),
          "easternHalfLengthKm":round(east.length/1000,5),
          "startLonLat":list(west_ll.coords)[0],
          "splitLonLat":list(west_ll.coords)[-1],
          "whole0718EndLonLat":list(transform(TO_LL,east).coords)[-1],
          "candidateSimple":west.is_simple,
          "samplePointsLonLat":sample_points(west,12),
          "candidateVsModernNamedDitch":closest_named_ditch(west)
      },
      "rasterPanels":panels,
      "historicalRule":{
          "protocol":"Berlin border commission 1865, Article I §2",
          "textUrl":"https://da.wikisource.org/wiki/Freden_i_Wien_(1864)_Gr%C3%A6nsereguleringskommissionen",
          "applicable":"1864 frontier on southern boundaries of Vester Vedsted/Ribe/Seem parishes and major drainage ditch until boundary stone 22"
      },
      "sourceCautions":[
          "1880 survey precedes later railroad/station changes (1887) and granite boundary-stone replacement (1891–1915).",
          "Map sheet index and modern-parish clipping are not proof of the historic line.",
          "No red candidate coordinates are promoted to verified historical coordinates.",
          "Do not assume moved stone 2 location is the original boundary marker.",
          "Verify WMS georeferencing accuracy and map-date alterations before meter-scale claims."
      ],
      "githubActionsRunId":os.environ.get("GITHUB_RUN_ID"),
      "githubRepository":os.environ.get("GITHUB_REPOSITORY"),
      "reviewArtifactPath":"tools/historical-library/review-output/north-schleswig-1914-001-026/0718-west",
      "next":"Manual visual classification of historic 1880 boundary symbol for both west-half subpanels; trace only unambiguous portions, compare deviation and post-1880 changes."
    }
    dump(REPORT,report)
    print(json.dumps({
        "westernHalfLengthKm":report["candidate"]["westernHalfLengthKm"],
        "start":report["candidate"]["startLonLat"],
        "split":report["candidate"]["splitLonLat"],
        "threeMapSamples":[p["map"]["imageDimensions"] for p in panels],
        "ranInGitHub":report["githubActionsRunId"],
        "status":report["workingBorderStatus"],
        "geojsonFile":str(GEOMETRY),
        "reviewFiles":[p["map"]["overlayArtifactFile"] for p in panels],
    },ensure_ascii=False,indent=2))
    # Output downsampled JPEGs in the temporary CI job log for visual QC.
    # Do not put any map image into Git commits.
    if os.environ.get("GIS_EMIT_LOG_THUMBNAIL")=="1":
        for key, filename in [
            ("OVERVIEW","0718-west-overview-overlay.jpg"),
            ("ORIGINAL","0718-west-overview-source-1880.jpg"),
            ("PANEL1","0718-west-01-overlay.jpg"),
            ("PANEL2","0718-west-02-overlay.jpg"),
        ]:
            img=Image.open(ARTIFACT/filename)
            img.thumbnail((1200,930))
            buf=io.BytesIO()
            img.save(buf,format="JPEG",quality=66,optimize=True)
            print(f"===BEGIN_STAGE1_0718_WEST_{key}_JPEG===")
            print(base64.b64encode(buf.getvalue()).decode("ascii"))
            print(f"===END_STAGE1_0718_WEST_{key}_JPEG===")



if __name__ == "__main__":
    main()
