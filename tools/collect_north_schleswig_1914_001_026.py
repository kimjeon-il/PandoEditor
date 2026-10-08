#!/usr/bin/env python3
import json, pathlib, requests, xml.etree.ElementTree as ET, time

ROOT=pathlib.Path(__file__).resolve().parents[1]
OUT=ROOT/"tools/historical-library/sources/german-empire-1914/north-schleswig-001-026"
OUT.mkdir(parents=True,exist_ok=True)

UA={"User-Agent":"PandoLab-NorthSchleswig1914/1.0"}

def get_json(url):
    r=requests.get(url,headers=UA,timeout=120)
    r.raise_for_status()
    return r.json()

# OSM hiking relation: spatial aid only, not treated as the historical border.
route=get_json("https://api.openstreetmap.org/api/0.6/relation/11260903/full.json")
(OUT/"graensestien-osm-relation-11260903.full.json").write_text(
    json.dumps(route,ensure_ascii=False,separators=(",",":")),encoding="utf-8"
)

# Scan the 1→26 sector through the core OSM map API instead of Overpass.
# Small tiles keep each request well below OSM's map-call area limit.
MIN_LON,MIN_LAT,MAX_LON,MAX_LAT=8.62,55.245,8.99,55.335
NX,NY=8,3
nodes={}
ways={}
relations={}

for ix in range(NX):
    for iy in range(NY):
        x0=MIN_LON+(MAX_LON-MIN_LON)*ix/NX
        x1=MIN_LON+(MAX_LON-MIN_LON)*(ix+1)/NX
        y0=MIN_LAT+(MAX_LAT-MIN_LAT)*iy/NY
        y1=MIN_LAT+(MAX_LAT-MIN_LAT)*(iy+1)/NY
        url="https://api.openstreetmap.org/api/0.6/map"
        r=requests.get(url,params={"bbox":f"{x0},{y0},{x1},{y1}"},headers=UA,timeout=180)
        r.raise_for_status()
        root=ET.fromstring(r.content)
        for n in root.findall("node"):
            nid=int(n.attrib["id"])
            rec=nodes.setdefault(nid,{"type":"node","id":nid,"lat":float(n.attrib["lat"]),"lon":float(n.attrib["lon"]),"tags":{}})
            for t in n.findall("tag"):
                rec["tags"][t.attrib["k"]]=t.attrib["v"]
        for w in root.findall("way"):
            wid=int(w.attrib["id"])
            rec=ways.setdefault(wid,{"type":"way","id":wid,"nodes":[],"tags":{}})
            rec["nodes"]=[int(nd.attrib["ref"]) for nd in w.findall("nd")]
            for t in w.findall("tag"):
                rec["tags"][t.attrib["k"]]=t.attrib["v"]
        for rel in root.findall("relation"):
            rid=int(rel.attrib["id"])
            rec=relations.setdefault(rid,{"type":"relation","id":rid,"members":[],"tags":{}})
            rec["members"]=[dict(m.attrib) for m in rel.findall("member")]
            for t in rel.findall("tag"):
                rec["tags"][t.attrib["k"]]=t.attrib["v"]
        time.sleep(0.12)

def norm(s): return str(s or "").lower()
def is_candidate_node(n):
    t=n["tags"]; name=norm(t.get("name"))
    return (
        t.get("historic")=="boundary_stone" or
        t.get("boundary")=="marker" or
        any(x in name for x in ("grænsesten","graensesten","grenzstein","grensesten"))
    )
def is_candidate_way(w):
    t=w["tags"]; name=norm(t.get("name"))
    return (
        any(x in norm(t.get("historic")) for x in ("boundary","border")) or
        any(x in norm(t.get("boundary")) for x in ("historic","former")) or
        any(x in name for x in ("grænse","graense","grenz"))
    )
def is_context(tags):
    s=" ".join(norm(v) for v in tags.values())
    return any(x in s for x in ("sprækbro","spraekbro","vester vedsted","råhede","raahede","gelsbro","gjelsbro","gelså","gels å","gjels-å"))

candidate_nodes=[n for n in nodes.values() if is_candidate_node(n)]
candidate_ways=[w for w in ways.values() if is_candidate_way(w)]
context_nodes=[n for n in nodes.values() if is_context(n["tags"])]
context_ways=[w for w in ways.values() if is_context(w["tags"])]

# Add coordinates to candidate ways for later GIS inspection.
for w in candidate_ways+context_ways:
    w["geometry"]=[[nodes[nid]["lon"],nodes[nid]["lat"]] for nid in w["nodes"] if nid in nodes]

candidates={"nodes":candidate_nodes,"ways":candidate_ways}
context={"nodes":context_nodes,"ways":context_ways}
(OUT/"osm-historic-border-candidates.json").write_text(json.dumps(candidates,ensure_ascii=False,separators=(",",":")),encoding="utf-8")
(OUT/"osm-endpoint-context.json").write_text(json.dumps(context,ensure_ascii=False,separators=(",",":")),encoding="utf-8")

summary={
  "routeElements":len(route.get("elements",[])),
  "scannedNodes":len(nodes),
  "scannedWays":len(ways),
  "candidateNodeCount":len(candidate_nodes),
  "candidateWayCount":len(candidate_ways),
  "contextNodeCount":len(context_nodes),
  "contextWayCount":len(context_ways),
  "candidateNodes":candidate_nodes,
  "candidateWays":[{"id":w["id"],"tags":w["tags"],"pointCount":len(w.get("geometry",[]))} for w in candidate_ways],
  "contextNodes":context_nodes,
  "contextWays":[{"id":w["id"],"tags":w["tags"],"pointCount":len(w.get("geometry",[]))} for w in context_ways],
}
(OUT/"collection-summary.json").write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding="utf-8")
print(json.dumps(summary,ensure_ascii=False,indent=2))
