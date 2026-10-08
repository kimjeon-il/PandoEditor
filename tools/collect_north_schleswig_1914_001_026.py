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



# Identify current OSM parish/administrative relations as geometric controls for the
# parish boundaries named in the 1865 protocol.
parish_terms=("vester vedsted","seem","hviding","roager","ribe")
parish_relations=[]
for rel in relations.values():
    tags=rel.get("tags",{})
    name=norm(tags.get("name"))
    boundary=norm(tags.get("boundary"))
    admin=norm(tags.get("admin_level"))
    if any(term in name for term in parish_terms) and boundary in ("administrative","religious_administration"):
        parish_relations.append(rel)

parish_full={}
for rel in parish_relations:
    rid=rel["id"]
    try:
        data=get_json(f"https://api.openstreetmap.org/api/0.6/relation/{rid}/full.json")
        parish_full[str(rid)]=data
        (OUT/f"osm-parish-relation-{rid}.full.json").write_text(
            json.dumps(data,ensure_ascii=False,separators=(",",":")),encoding="utf-8"
        )
    except Exception as exc:
        parish_full[str(rid)]={"error":repr(exc)}


# Public DAGI ArcGIS mirror: current parish polygons in the historical sector.
dagi_url="https://demo.geoinfo.dk/server/rest/services/DAGI_Hele_DK/MapServer/1/query"
dagi_params={
    "where":"1=1",
    "geometry":f"{MIN_LON},{MIN_LAT},{MAX_LON},{MAX_LAT}",
    "geometryType":"esriGeometryEnvelope",
    "inSR":"4326",
    "spatialRel":"esriSpatialRelIntersects",
    "outFields":"*",
    "returnGeometry":"true",
    "outSR":"4326",
    "f":"geojson",
}
dagi_response=requests.get(dagi_url,params=dagi_params,headers=UA,timeout=180)
dagi_response.raise_for_status()
dagi_parishes=dagi_response.json()
(OUT/"dagi-current-parishes-sector.geojson").write_text(
    json.dumps(dagi_parishes,ensure_ascii=False,separators=(",",":")),encoding="utf-8"
)

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
  "parishRelationCount":len(parish_relations),
  "parishRelations":[{"id":r["id"],"tags":r.get("tags",{}),"memberCount":len(r.get("members",[]))} for r in parish_relations],
  "dagiParishFeatureCount":len(dagi_parishes.get("features",[])),
  "dagiParishProperties":[f.get("properties",{}) for f in dagi_parishes.get("features",[])],
}
(OUT/"collection-summary.json").write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding="utf-8")
print(json.dumps(summary,ensure_ascii=False,indent=2))
