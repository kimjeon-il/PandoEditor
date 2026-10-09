#!/usr/bin/env python3
"""Stage 5: visually audit Hvidding-Ufer 0717 stone 1 and the 1914 coast.

Use 1878-survey/1880-issue cartography only as a baseline, distinguishing
the 1911-1912/13 Fløjdiget from the still-being-completed 1911-1915
Ribediget, and excluding the 1923-1925 Rejsby Dike.
No historical shoreline is invented, no coastline moved, and the master
border remains provisional. TIFF/scan fragments are temporary CI artifacts.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import io
import json
import os
from pathlib import Path

import requests
from PIL import Image, ImageDraw, ImageFont
from pyproj import Transformer
from shapely.geometry import LineString, Point, shape, mapping
from shapely.ops import transform

ROOT=Path(__file__).resolve().parents[1]
LIB=ROOT/"tools/historical-library"
B=LIB/"land-borders"
S=LIB/"sources/german-empire-1914/north-schleswig-001-026"
STEM="german-denmark-1914-north-schleswig-001-026"
BASE=STEM+".0717-hvidding-ufer-stage5"
OUT=B/(BASE+".json")
CLIP=B/(BASE+".geojson")
PREVIEW=LIB/"review-output/north-schleswig-1914-001-026/0717-hvidding-ufer"
TO_METRIC=Transformer.from_crs("EPSG:4326","EPSG:25832",always_xy=True).transform
WMS={
 "0717":("10006009","df_dk_0010001_0717","1880"),
 "0718":("10006008","df_dk_0010001_0718","1880"),
}
WIDTH=2560
FLAT_ZOOM=64
STOP_PX=.5
PERIOD="1914-07-31"
PANELS=[
 ("western-landing",[8.649,55.267,8.673,55.289],"Stone 1 / Råhede: 1880 West Coast",("0717","0718"),1700,1100),
 ("historical-start-closeup",[8.657,55.2715,8.6725,55.281],"Historic stone 1 and modern parish candidate",("0717","0718"),1700,1100),
 ("wing-dike-context",[8.634,55.262,8.687,55.296],"Fløjdiget (1912) context / pre-dike map 1880",("0717","0718"),1650,1150),
]

def read(path):
 return json.loads(path.read_text(encoding="utf-8"))

def save(path,data):
 path.parent.mkdir(parents=True,exist_ok=True)
 path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")

def font(size=17):
 try:return ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",size)
 except OSError:return ImageFont.load_default()

def get_map(session,sheet,bbox,width,height):
 map_id,layer,year=WMS[sheet]
 west,south,east,north=bbox
 params={
  "SERVICE":"WMS","REQUEST":"GetMap","VERSION":"1.3.0",
  "LAYERS":layer,"STYLES":"","CRS":"EPSG:4326",
  "BBOX":f"{south},{west},{north},{east}",
  "WIDTH":width,"HEIGHT":height,
  "FORMAT":"image/png","TRANSPARENT":"true"
 }
 url="https://wms.kartenforum.slub-dresden.de/map/"+map_id
 response=session.get(url,params=params,timeout=95,
    headers={"User-Agent":"PandoEditor-HistoricalGIS-0717Stage5/1.0"})
 response.raise_for_status()
 raster=Image.open(io.BytesIO(response.content)).convert("RGBA")
 raster.load()
 if raster.size!=(width,height) or raster.getchannel("A").getbbox() is None:
  raise RuntimeError(f"Missing original cartographic data in {sheet}")
 return raster, {
  "sheet":sheet,"sourceWmsMapId":map_id,
  "sourceWmsLayer":layer,"mapCatalogPublicationYear":int(year),
  "responseBytes":len(response.content),
  "pngSha256":hashlib.sha256(response.content).hexdigest()
 }

def screen_info(line_ll):
 factor=WIDTH*FLAT_ZOOM/360
 px=LineString([(lon*factor,-lat*factor) for lon,lat in line_ll.coords])
 simplified=px.simplify(STOP_PX,preserve_topology=True)
 return {
  "mapCssWidthPx":WIDTH,
  "webFlatZoom":FLAT_ZOOM,
  "displayToleranceCssPx":STOP_PX,
  "originalVertices":len(line_ll.coords),
  "screenLengthCssPx":round(px.length,5),
  "illustrativeVerticesAt0p5CssPx":len(simplified.coords),
  "candidateToOwnSimplificationHausdorffCssPx":round(px.hausdorff_distance(simplified),6),
  "independent1880BorderDigitized":False,
  "actual1914CoastlineDigitized":False,
  "historic1914ScreenDeviationCssPx":None,
  "stopRuleHistoricallyPassed":None,
  "meaning":"Same modern candidate compared to its own simplified version; historical deviation UNMEASURED"
 }

def render_one(session,name,bbox,label,sheets,width,height,border,marker):
 west,south,east,north=bbox
 data=[get_map(session,s,bbox,width,height) for s in sheets]
 base=Image.new("RGBA",(width,height),(255,255,255,0))
 for image,meta in data:
  base.alpha_composite(image)
 origin=Image.new("RGB",(width,height),"white")
 origin.paste(base,mask=base.getchannel("A"))
 original_name=f"{name}-source-1880.jpg"
 origin.save(PREVIEW/original_name,format="JPEG",quality=79,optimize=True)
 out=Image.new("RGB",(width,height+76),"white")
 out.paste(origin,(0,35))
 d=ImageDraw.Draw(out)
 d.rectangle((0,0,width,34),fill=(20,40,55))
 d.text((12,9),label+" | independent 1914 shoreline not yet traced",
        fill="white",font=font(16))
 xyp=lambda p:((p[0]-west)/(east-west)*(width-1),
                 35+(north-p[1])/(north-south)*(height-1))
 raw=[xyp(p) for p in border.coords]
 if len(raw)>1:
  d.line(raw,fill="white",width=8)
  d.line(raw,fill=(213,35,55),width=4)
 # All panels span the known target point and 0717/0718 edge.
 m=xyp(marker)
 d.ellipse((m[0]-8,m[1]-8,m[0]+8,m[1]+8),
           fill=(255,208,26),outline="black",width=2)
 d.text((m[0]+12,m[1]-24),"1 (2014 restored-position OSM)",fill=(96,20,20),
        stroke_fill="white",stroke_width=2,font=font(16))
 d.rectangle((0,height+35,width,height+76),fill=(20,40,55))
 d.text((12,height+48),
        "RED: provisional border. YELLOW: present restored marker 1; dike is NOT automatically the shoreline.",
        fill="white",font=font(15))
 overlay_name=f"{name}-working-border-overlay.jpg"
 out.save(PREVIEW/overlay_name,"JPEG",quality=81,optimize=True)
 return {
  "name":name,
  "bboxLonLat":bbox,
  "historicSourceMaps":[metadata for _,metadata in data],
  "originalArtifact":original_name,
  "overlayArtifact":overlay_name,
  "overlaySha256":hashlib.sha256((PREVIEW/overlay_name).read_bytes()).hexdigest(),
  "historic1880BorderIndependentlyTraced":False,
  "historic1914CoastVerified":False
 }

def main():
 source=read(B/(STEM+".sheet-segments.geojson"))
 selected={f["properties"]["sheetId"]:f for f in source["features"]}
 if any(s not in selected for s in ("0717","0718")):raise RuntimeError("Missing source sheet")
 one=selected["0717"]["geometry"]["coordinates"]
 if len(one)!=1:raise RuntimeError("0717 source contains disconnected line")
 line=LineString(one[0])
 following=LineString(selected["0718"]["geometry"]["coordinates"][0])
 if not line.is_simple or not following.is_simple:raise RuntimeError("Input topology has self-intersection")
 if not (280<transform(TO_METRIC,line).length<315):raise RuntimeError("Unexpected 0717 segment length")
 if line.coords[-1]!=following.coords[0]:raise RuntimeError("0717/0718 seam is disconnected")
 master=read(B/(STEM+".geojson"))
 if master["features"][0]["properties"].get("doNotTreatAsFinal") is not True:
  raise RuntimeError("Original master should be provisional")
 candidate=master["features"][0]["geometry"]["coordinates"]
 if len(candidate)!=1343 or tuple(line.coords[0])!=tuple(candidate[0]):
  raise RuntimeError("Master reference stone1 or vertex count changed")
 gathered=read(S/"collection-summary.json")
 stone=[x for x in gathered["candidateNodes"] if x.get("tags",{}).get("alt_name","").startswith("Grænsesten nr. 1 (")]
 if len(stone)!=1:raise RuntimeError("Stone1 source is not unique")
 marker=(stone[0]["lon"],stone[0]["lat"])
 err=Point(marker).distance(Point(line.coords[0]))
 if err>1e-7:raise RuntimeError("Candidate no longer pinned to restored marker1")
 clip={
  "type":"FeatureCollection",
  "name":STEM+"-0717-hvidding-ufer-nonfinal",
  "features":[{
    "type":"Feature",
    "id":"candidate:deu-dnk:1914:0717-stone1-coast",
    "properties":{
      "referenceDate":PERIOD,"sheet":"0717 Hvidding-Ufer",
      "lengthKm":round(transform(TO_METRIC,line).length/1000,5),
      "status":"provisional-requires-1914-coastline-review",
      "doNotTreatAsFinal":True,
      "independentCoastlineValidation":False,
      "source":STEM+".sheet-segments.geojson",
      "historicalMarineBoundaryIsSeparate":True
    },
    "geometry":mapping(line)
  }]
 }
 save(CLIP,clip)
 PREVIEW.mkdir(parents=True,exist_ok=True)
 session=requests.Session()
 photos=[render_one(session,*params,line,marker) for params in PANELS]
 report={
  "schemaVersion":1,
  "workUnit":"1914-German-Danish-0717-Hvidding-Ufer/stone1-coast/stage5",
  "referenceDate":PERIOD,
  "sourceMaps":{
    "0717":{"name":"Hvidding-Ufer","surveyYear":1878,"publicationYear":1880,
      "mapScale":25000,"wmsMapId":"10006009",
      "source":"https://www.deutschefotothek.de/documents/obj/71051521"},
    "0718":{"name":"Hvidding","surveyYear":1878,"publicationYear":1880,
      "wmsMapId":"10006008",
      "source":"https://www.deutschefotothek.de/documents/obj/71051522"}
  },
  "primaryTreaty":{
    "source":"https://da.wikisource.org/wiki/Freden_i_Wien_(1864)_Gr%C3%A6nsereguleringskommissionen",
    "articleI1":"North Sea maritime frontier is a separate line from midpoint Mandø/Rømø toward Ribe cathedral, turning to boundary stone1 near shore.",
    "articleI2":"Land frontier starts at stone1 where Vester Vedsted southern parish boundary meets the North Sea shore.",
    "historicalMarineBoundaryDigitized":False,
    "landShoreIntersection1914Confirmed":False
  },
  "chronology":{
    "fløjdiget":{
      "type":"storm-surge flank/wing dike near Vester Vedsted border",
      "circaCompletion":1912,
      "presentBefore1914Likely":True,
      "sources":[
        "https://slks.dk/omraader/kulturarv/bevaringsvaerdige-bygninger-og-miljoeer/bevaringsvaerdige-bygninger-metode/atlas/vadehavet-kulturarvsatlas/bebyggede-strukturer/niveau-2-de-store-landskaber/marsken-ved-ribe-aa",
        "https://vestervedsted.dk/floejdiget-i-vester-vedsted/"
      ]},
    "ribediget":{
      "type":"larger coastal protection system",
      "constructionYears":"1911-1915",
      "initialBuiltOut":"1912-1913 depending on sector",
      "officialHandover":"1915-05-10",
      "entireSystemCompleteByReferenceDate":False,
      "source":"https://danhostel-ribe.dk/wp-content/uploads/2022/03/Kammerslusen_A2skilte_WEB.pdf"
    },
    "rejsbydiget":{
      "type":"dike continuing south of Vester Vedsted",
      "constructionYears":"1923-1925",
      "existsByReferenceDate":False,
      "source":"https://graenseforeningen.dk/leksikon/rejsby-diget"
    },
    "rahedeSluse":{
      "type":"Råhede Bæk sluice at 1923–1925 Rejsby Dike (modern landmark)",
      "referenceDate1914Exists":False,
      "constructionEpoch":"1923–1925",
      "priorTo1925":"Råhede Bæk (old international border watercourse) had its own separate outlet to the Wadden Sea; Vester Vedsted Bæk was discharged separately through the Ribe-Darum system",
      "modernLandmarkNotPeriodFeature":True,
      "source":"https://tidsskrift.dk/geografisktidsskrift/article/download/46290/57034?inline=1"
    },
    "vesterVedstedBaekSluse":{
      "period":"1911–1914",
      "location":"Vester Vedsted Bæk outlet in Ribe-Darum sea dike",
      "notInterchangeableWithRahedeSluse":True,
      "source":"https://tidsskrift.dk/geografisktidsskrift/article/download/46290/57034?inline=1"
    },
    "graniteStone1":{
      "originalClaim":"Restored to historical/original position 2014-08-20",
      "osmMarkerId":stone[0]["id"],
      "source":"https://graenseforeningen.dk/om-graenselandet/genforeningssten/raahede-sluse-graensesten-nr-1",
      "2014RestoreIs1914ShoreEvidence":False
    }
  },
  "workingGeometry":{
    "candidateLengthKm":round(transform(TO_METRIC,line).length/1000,5),
    "candidateVertices":len(line.coords),
    "startLonLat":list(line.coords[0]),
    "endLonLat":list(line.coords[-1]),
    "restoredModernStone1":list(marker),
    "restoredStone1CoordDifferenceDegrees":round(err,12),
    "join0718GapDegrees":0,
    "sourceSimple":line.is_simple,
    "renderDisplayEstimate":screen_info(line)
  },
  "warnings":[
    "A sea-wall/dike crest is not automatically the mean-high-tide shoreline, cadastral land edge, or 1914 maritime border.",
    "Ribediget completed/handed over May 1915 does not prove local Fløjdiget absent July 1914.",
    "The 1923-25 Rejsby dike and its later sea drainage/sluice geometry must not be used in a 1914 coastline.",
    "Råhede Sluse is a post-1923/25 landmark: it did not exist as this modern sluice in July 1914. Keep Råhede Bæk and Vester Vedsted Bæk separate 1914 outlets.",
    "The 2014 stone No. 1 reinstallation is an oral/heritage restoration claim, not a geodetic survey of 1914 tidal shoreline or sea-wall toe.",
    "1880 pre-dike map is a baseline only; cannot certify 1914 coast or flanking land of the German/Danish state.",
    "The 2014 restoration claim for stone1 does not establish contemporary 1914 tide edge geometry.",
    "Current 0.2938km candidate has not passed an independent historical deviation test.",
    "No legal maritime boundary or 1914 coastline may be inferred by extending the restored marker point seaward."
  ],
  "sourceRasterImages":photos,
  "historicalCoastlineIndependentlyDigitized":False,
  "historicalScreenDeviationCssPx":None,
  "historicalStopThresholdPassed":None,
  "physicalCoastEdited":False,
  "masterBorderEdited":False,
  "status":"provisional:original-map-panels-generated-needs-human-review",
  "ciRunId":os.environ.get("GITHUB_RUN_ID"),
  "artifactPath":str(PREVIEW.relative_to(ROOT)),
  "next":"Examine 1880 historical coast and the 1912/1913 wing dike chronology; determine whether any >0.5px difference matters to 1914 state coast/land ownership. Preserve original geometry and legal marine/land divide."
 }
 save(OUT,report)
 print(json.dumps({
  "candidateLengthKm":report["workingGeometry"]["candidateLengthKm"],
  "vertices":len(line.coords),
  "screenLenCssPx":report["workingGeometry"]["renderDisplayEstimate"]["screenLengthCssPx"],
  "displaySimpleVertices":report["workingGeometry"]["renderDisplayEstimate"]["illustrativeVerticesAt0p5CssPx"],
  "join0718GapDegrees":0,
  "originalMaps":list(WMS),
  "panels":[p["name"] for p in photos],
  "historicalShorelineValidated":False,
  "runId":report["ciRunId"]
 },ensure_ascii=False,indent=2))
 if os.environ.get("GIS_STAGE5_LOG_IMAGE")=="1":
  for photo in photos:
   im=Image.open(PREVIEW/photo["overlayArtifact"])
   im.thumbnail((1250,920))
   buf=io.BytesIO()
   im.save(buf,"JPEG",quality=66,optimize=True)
   tag=photo["name"].upper().replace("-","_")
   print(f"===BEGIN_STAGE5_0717_{tag}_JPEG===")
   print(base64.b64encode(buf.getvalue()).decode("ascii"))
   print(f"===END_STAGE5_0717_{tag}_JPEG===")

if __name__=="__main__":
 main()
