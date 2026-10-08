#!/usr/bin/env python3
import json, pathlib, math
from shapely.geometry import shape, Point, LineString, MultiLineString, mapping
from shapely.ops import unary_union, nearest_points, substring, transform
from pyproj import Transformer

ROOT=pathlib.Path(__file__).resolve().parents[1]
SRC=ROOT/"tools/historical-library/sources/german-empire-1914/north-schleswig-001-026"
OUT=ROOT/"tools/historical-library/land-borders"
OUT.mkdir(parents=True,exist_ok=True)

parishes=json.loads((SRC/"dagi-current-parishes-sector.geojson").read_text(encoding="utf-8"))
candidates=json.loads((SRC/"osm-historic-border-candidates.json").read_text(encoding="utf-8"))
context=json.loads((SRC/"osm-endpoint-context.json").read_text(encoding="utf-8"))
route=json.loads((SRC/"graensestien-osm-relation-11260903.full.json").read_text(encoding="utf-8"))

north_names={"Vester Vedsted","Ribe Domsogn","Sankt Katharine","Seem"}
north_feats=[f for f in parishes["features"] if f.get("properties",{}).get("navn") in north_names]
if {f["properties"]["navn"] for f in north_feats} != north_names:
    raise SystemExit("missing required north-side parish polygons")
north=unary_union([shape(f["geometry"]) for f in north_feats])
if not north.is_valid: north=north.buffer(0)

to_m=Transformer.from_crs("EPSG:4326","EPSG:25832",always_xy=True).transform
to_w=Transformer.from_crs("EPSG:25832","EPSG:4326",always_xy=True).transform
N=transform(to_m,north)

stone1_w=Point(8.6622877,55.2763431)
S=transform(to_m,stone1_w)

gels_way=None
for w in context.get("ways",[]):
    if (w.get("tags",{}).get("name") or "").lower() in ("gels å","gels a","gels aa"):
        gels_way=w;break
if not gels_way:
    raise SystemExit("Gels Å geometry missing")
gels_w=LineString(gels_way["geometry"])
G=transform(to_m,gels_w)

ditch_lines=[]
for w in candidates.get("ways",[]):
    if w.get("tags",{}).get("name")=="Grænsegrøften" and len(w.get("geometry",[]))>=2:
        ditch_lines.append(LineString(w["geometry"]))
ditch_w=unary_union(ditch_lines) if ditch_lines else MultiLineString([])
D=transform(to_m,ditch_w) if not ditch_w.is_empty else ditch_w

# Reconstruct the Grænsestien member ways for a secondary spatial check.
nodes={e["id"]:(e["lon"],e["lat"]) for e in route.get("elements",[]) if e.get("type")=="node" and "lon" in e}
ways={e["id"]:e for e in route.get("elements",[]) if e.get("type")=="way"}
rel=next((e for e in route.get("elements",[]) if e.get("type")=="relation" and e.get("id")==11260903),None)
route_lines=[]
if rel:
    for m in rel.get("members",[]):
        if m.get("type")!="way": continue
        w=ways.get(m.get("ref"))
        if not w: continue
        pts=[nodes[n] for n in w.get("nodes",[]) if n in nodes]
        if len(pts)>=2: route_lines.append(LineString(pts))
route_w=unary_union(route_lines) if route_lines else MultiLineString([])
R=transform(to_m,route_w) if not route_w.is_empty else route_w

# Pick the exterior ring that is nearest to both stone 1 and Gels Å near Gelsbro.
rings=[]
if N.geom_type=="Polygon":
    rings=[LineString(N.exterior.coords)]
elif N.geom_type=="MultiPolygon":
    rings=[LineString(p.exterior.coords) for p in N.geoms]
else:
    raise SystemExit("unexpected north union geometry")

best=None
for ring in rings:
    ps=nearest_points(S,ring)[1]
    pg,p_on_g=nearest_points(ring,G)
    score=S.distance(ps)+pg.distance(p_on_g)
    rec=(score,ring,ps,pg,p_on_g)
    if best is None or score<best[0]: best=rec
score,ring,start_snap,end_snap,gels_snap=best

# Require sensible snap distances before extracting arcs.
start_snap_m=S.distance(start_snap)
end_gap_m=end_snap.distance(gels_snap)
if start_snap_m>250:
    raise SystemExit(f"stone 1 too far from parish boundary: {start_snap_m:.1f} m")
if end_gap_m>250:
    raise SystemExit(f"parish boundary too far from Gels Å: {end_gap_m:.1f} m")

# Closed ring: two possible paths between projected positions.
ring_len=ring.length
ds=ring.project(start_snap)
de=ring.project(end_snap)

def arc_forward(line,d1,d2):
    if d2>=d1:
        return substring(line,d1,d2)
    a=substring(line,d1,line.length)
    b=substring(line,0,d2)
    coords=list(a.coords)+list(b.coords)[1:]
    return LineString(coords)

arc1=arc_forward(ring,ds,de)
arc2=arc_forward(ring,de,ds)
# Reverse arc2 so both go start -> end.
arc2=LineString(list(arc2.coords)[::-1])

# Score primarily by route length, with direct evidence from Grænsegrøften and Grænsestien.
def sampled_mean_distance(line,target,n=250):
    if target.is_empty: return None
    vals=[]
    for i in range(n+1):
        p=line.interpolate(line.length*i/n)
        vals.append(p.distance(target))
    return sum(vals)/len(vals)

def evidence_score(line):
    md=sampled_mean_distance(line,D)
    mr=sampled_mean_distance(line,R)
    # Strong historical ditch evidence gets 4x weight; route is only secondary.
    return (md if md is not None else 100000)*4 + (mr if mr is not None else 100000) + line.length*0.02

s1=evidence_score(arc1); s2=evidence_score(arc2)
chosen=arc1 if s1<=s2 else arc2
alt=arc2 if chosen is arc1 else arc1

# Attach exact stone 1 and exact nearest point on Gels Å, preserving direction.
coords=list(chosen.coords)
if Point(coords[0]).distance(S)>Point(coords[-1]).distance(S):
    coords=list(reversed(coords))
if Point(coords[0]).distance(S)>0.05:
    coords.insert(0,(S.x,S.y))
else:
    coords[0]=(S.x,S.y)
# Last point is the river intersection/control; use nearest Gels Å point.
river_pt=nearest_points(Point(coords[-1]),G)[1]
if Point(coords[-1]).distance(river_pt)>0.05:
    coords.append((river_pt.x,river_pt.y))
else:
    coords[-1]=(river_pt.x,river_pt.y)
line_m=LineString(coords)
line_w=transform(to_w,line_m)

if not line_w.is_simple:
    raise SystemExit("candidate line self-intersects")
if not (10000 <= line_m.length <= 60000):
    raise SystemExit(f"candidate length implausible: {line_m.length/1000:.2f} km")

# Diagnostics against direct/secondary evidence.
ditch_intersection_length=line_m.intersection(D.buffer(3)).length if not D.is_empty else 0
mean_ditch=sampled_mean_distance(line_m,D)
mean_route=sampled_mean_distance(line_m,R)

feature={
 "type":"Feature",
 "id":"land-border:deu-dnk:1914:north-schleswig:001-026",
 "properties":{
   "name":"German–Danish border 1914, boundary stones 1–26",
   "referenceDate":"1914-07-31",
   "from":"Boundary stone 1, Råhede Sluse",
   "to":"Gels Å at Gelsbro / boundary stone 26 sector",
   "status":"working-master-needs-historical-map-review",
   "method":"Current 1:10,000 DAGI parish boundaries selected according to the 1865 boundary commission protocol; checked against OSM Grænsegrøften and Grænsestien.",
   "historicalRule":"Southern boundaries of Vester Vedsted, Ribe and Seem parish sectors to stone 22, then eastern Seem boundary to Gels Å at stone 26.",
   "doNotTreatAsFinal":True
 },
 "geometry":mapping(line_w)
}
out={"type":"FeatureCollection","name":"german-denmark-1914-north-schleswig-001-026", "features":[feature]}
(OUT/"german-denmark-1914-north-schleswig-001-026.geojson").write_text(
 json.dumps(out,ensure_ascii=False,separators=(",",":")),encoding="utf-8"
)
diag={
 "northParishes":sorted(north_names),
 "stone1":[stone1_w.x,stone1_w.y],
 "startSnapDistanceM":round(start_snap_m,3),
 "endGapToGelsAM":round(end_gap_m,3),
 "endpointGelsA":list(line_w.coords)[-1],
 "chosenLengthKm":round(line_m.length/1000,3),
 "alternativeLengthKm":round(alt.length/1000,3),
 "arcScores":[round(s1,3),round(s2,3)],
 "meanDistanceToGraensegroeftenM":None if mean_ditch is None else round(mean_ditch,3),
 "meanDistanceToGraensestienM":None if mean_route is None else round(mean_route,3),
 "lengthWithin3mOfGraensegroeftenKm":round(ditch_intersection_length/1000,3),
 "coordinateCount":len(line_w.coords),
 "simple":line_w.is_simple,
 "notes":[
   "Stone 1 is mapped at its original Råhede Sluse position.",
   "Moved memorial stones 2, 7b, 11, 22, 28, 30 and 31 were not used as positional anchors.",
   "Current parish geometry is a high-resolution control; historical map-sheet comparison is still required before finalizing this sector."
 ]
}
(OUT/"german-denmark-1914-north-schleswig-001-026.diagnostics.json").write_text(
 json.dumps(diag,ensure_ascii=False,indent=2),encoding="utf-8"
)
print(json.dumps(diag,ensure_ascii=False,indent=2))
