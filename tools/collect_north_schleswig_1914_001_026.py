#!/usr/bin/env python3
import json, pathlib, requests

ROOT=pathlib.Path(__file__).resolve().parents[1]
OUT=ROOT/"tools/historical-library/sources/german-empire-1914/north-schleswig-001-026"
OUT.mkdir(parents=True,exist_ok=True)

UA={"User-Agent":"PandoLab-NorthSchleswig1914/1.0"}

def get_json(url, params=None):
    r=requests.get(url,params=params,headers=UA,timeout=120)
    r.raise_for_status()
    return r.json()

route=get_json("https://api.openstreetmap.org/api/0.6/relation/11260903/full.json")
(OUT/"graensestien-osm-relation-11260903.full.json").write_text(
    json.dumps(route,ensure_ascii=False,separators=(",",":")),encoding="utf-8"
)

OVERPASS=[
    "https://overpass.kumi.systems/api/interpreter",
    "https://overpass.private.coffee/api/interpreter",
    "https://overpass-api.de/api/interpreter",
]
def overpass(query):
    last=None
    for endpoint in OVERPASS:
        try:
            r=requests.post(endpoint,data={"data":query},headers=UA,timeout=180)
            r.raise_for_status()
            return r.json()
        except Exception as exc:
            last=exc
            print("overpass failed",endpoint,repr(exc))
    raise last

q=r'''
[out:json][timeout:60];
(
  node(55.245,8.62,55.335,8.99)["historic"="boundary_stone"];
  node(55.245,8.62,55.335,8.99)["boundary"="marker"];
  node(55.245,8.62,55.335,8.99)["name"~"Grænsesten|Graensesten|Grenzstein|Grensesten",i];
);
out body;
'''
candidates=overpass(q)
(OUT/"osm-historic-border-candidates.json").write_text(
    json.dumps(candidates,ensure_ascii=False,separators=(",",":")),encoding="utf-8"
)

q2=r'''
[out:json][timeout:120];
(
  nwr(55.268,8.665,55.292,8.715)["name"~"Sprækbro|Spraekbro|Vester Vedsted|Råhede|Raahede",i];
  nwr(55.285,8.910,55.306,8.945)["name"~"Gelsbro|Gjelsbro|Gelså|Gjels-Å|Gels Å",i];
);
out body geom;
'''
context=overpass(q2)
(OUT/"osm-endpoint-context.json").write_text(
    json.dumps(context,ensure_ascii=False,separators=(",",":")),encoding="utf-8"
)

summary={
  "routeElements":len(route.get("elements",[])),
  "candidateCount":len(candidates.get("elements",[])),
  "contextCount":len(context.get("elements",[])),
  "candidateElements":[{
      "type":e.get("type"),"id":e.get("id"),"lat":e.get("lat"),"lon":e.get("lon"),
      "tags":e.get("tags",{})
  } for e in candidates.get("elements",[])],
  "contextElements":[{
      "type":e.get("type"),"id":e.get("id"),"lat":e.get("lat"),"lon":e.get("lon"),
      "center":e.get("center"),"tags":e.get("tags",{})
  } for e in context.get("elements",[])]
}
(OUT/"collection-summary.json").write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding="utf-8")
print(json.dumps(summary,ensure_ascii=False,indent=2))
