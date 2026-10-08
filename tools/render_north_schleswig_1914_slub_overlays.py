#!/usr/bin/env python3
"""Create temporary review images: 1878/1880 SLUB WMS raster + provisional line.

The images are workflow artifacts, NOT committed, and are not a verified
digitization of the historic frontier. Reports saved to git contain metadata
and image hashes only. Manual historical-cartographic review is required.
"""
from __future__ import annotations

import hashlib
import io
import json
import math
from pathlib import Path
from urllib.parse import urlsplit

import requests
from PIL import Image, ImageDraw, ImageFont
from shapely.geometry import shape

ROOT = Path(__file__).resolve().parents[1]
B = ROOT / "tools/historical-library/land-borders"
STEM = "german-denmark-1914-north-schleswig-001-026"
OUTPUT = B / f"{STEM}.slub-overlays.json"
PREVIEW_DIR = ROOT / "tools/historical-library/review-output/north-schleswig-1914-001-026"
EXPECTED = {"0619", "0717", "0718", "0719"}
HEADERS = {"User-Agent": "PandoEditor-HistoricalGIS/1.0 (archival research overlay)"}


def read(name):
    return json.loads((B / f"{STEM}.{name}").read_text(encoding="utf-8"))


def paths(geom):
    if geom.is_empty:
        return
    if geom.geom_type == "LineString":
        yield list(geom.coords)
    elif geom.geom_type in ("MultiLineString", "GeometryCollection"):
        for sub in geom.geoms:
            yield from paths(sub)


def font(size):
    for candidate in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                      "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"):
        try:
            return ImageFont.truetype(candidate, size)
        except OSError:
            pass
    return ImageFont.load_default()


def view_bounds(segment, sheet_box):
    minx, miny, maxx, maxy = segment.bounds
    sw, ss, se, sn = sheet_box
    padx = max(.014, (maxx-minx)*.11)
    pady = max(.008, (maxy-miny)*.18)
    bounds = [max(sw, minx-padx), max(ss, miny-pady),
              min(se, maxx+padx), min(sn, maxy+pady)]
    if bounds[2] <= bounds[0] or bounds[3] <= bounds[1]:
        raise RuntimeError("empty map extent after margin")
    return bounds


def main():
    PREVIEW_DIR.mkdir(parents=True, exist_ok=True)
    sheets = read("sheets.json")["sheets"]
    context = read("slub-discovery.json")
    checks = {p["sheetId"]: p for p in read("slub-wms-probe.json")["mapProbes"]}
    segs = {f["properties"]["sheetId"]: shape(f["geometry"])
            for f in read("sheet-segments.geojson")["features"]}
    if {s["sheetId"] for s in sheets} != EXPECTED:
        raise RuntimeError("sheet index changed without review")
    manifest = {
        "schemaVersion": 1,
        "referenceDate": "1914-07-31",
        "originalMapScale": "1:25,000",
        "status": "preview-needs-manual-tracing",
        "historicBorderValidated": False,
        "historicRasterStoredInGit": False,
        "reviewArtifactDirectory": "tools/historical-library/review-output/north-schleswig-1914-001-026",
        "attribution": "SLUB Dresden / Deutsche Fotothek historic Messtischblatt raster",
        "sheetOverlays": [],
    }
    session = requests.Session()

    for sheet in sheets:
        key = sheet["sheetId"]
        info = {
            "sheetId": key, "title": sheet["title"],
            "year": sheet["publicationYear"], "status": "not-rendered",
            "candidateLengthKm": sheet["candidateLengthKm"]
        }
        manifest["sheetOverlays"].append(info)
        if checks.get(key, {}).get("status") != "wms-image-decoded":
            info["status"] = "prerequisite-wms-not-verified"
            continue
        hits = context.get("matches", {}).get(key) or []
        if len(hits) != 1:
            info["status"] = "catalog-ambiguous"
            continue
        entry = hits[0]["metadata"]
        wms_links = [v["url"] for v in entry.get("online_resources", [])
                     if v.get("type") == "WMS"]
        if len(wms_links) != 1:
            info["status"] = "wms-url-ambiguous"
            continue
        parts = urlsplit(wms_links[0])
        if parts.scheme != "https" or parts.hostname != "wms.kartenforum.slub-dresden.de":
            info["status"] = "wms-host-not-allowed"
            continue
        base = "https://" + parts.netloc + parts.path
        layer_names = checks[key].get("availableWmsLayerNames", [])
        exact = [v for v in layer_names if v.startswith("df_dk_0010001_")]
        if len(exact) != 1:
            info["status"] = "historic-layer-not-disambiguated"
            continue
        layer = exact[0]
        bbox = view_bounds(segs[key], sheet["extentLonLat"])
        width, height = 1280, 900
        params = {
            "service": "WMS", "version": "1.3.0", "request": "GetMap",
            "layers": layer, "styles": "",
            "crs": "EPSG:4326",
            "bbox": ",".join(str(v) for v in
                             (bbox[1], bbox[0], bbox[3], bbox[2])),
            "width": width, "height": height, "format": "image/png",
            "transparent": "true"
        }
        try:
            response = session.get(base, params=params,
                                   headers=HEADERS, timeout=75)
            response.raise_for_status()
            img = Image.open(io.BytesIO(response.content)).convert("RGBA")
            if img.size != (width, height):
                raise RuntimeError("unexpected map response dimensions")
            canvas = Image.new("RGBA", (width, height + 70),
                               (245, 245, 245, 255))
            canvas.alpha_composite(img, (0, 34))
            draw = ImageDraw.Draw(canvas, "RGBA")
            draw.rectangle((0, 0, width, 34), fill=(25, 37, 49, 255))
            draw.text((12, 7), f"{key} {sheet['title']} ({sheet['publicationYear']}) — provisional 1914 candidate",
                      font=font(17), fill="white")
            sw, ss, se, sn = bbox
            for line in paths(segs[key]):
                pixels = [
                    ((lon-sw)/(se-sw)*(width-1),
                     34+(sn-lat)/(sn-ss)*(height-1))
                    for lon, lat in line]
                if len(pixels) >= 2:
                    draw.line(pixels, fill=(255, 255, 255, 235), width=7)
                    draw.line(pixels, fill=(211, 39, 42, 235), width=4)
                elif len(pixels) == 1:
                    x, y = pixels[0]
                    draw.ellipse((x-4, y-4, x+4, y+4), fill=(211, 39, 42, 255))
            draw.rectangle((0, 34+height, width, 70+height),
                           fill=(25, 37, 49, 255))
            draw.text((12, 42+height),
                      "Red: candidate boundary from CURRENT parish geometry — NOT validated against map",
                      font=font(17), fill="white")
            output_name = f"{key}-{sheet['title'].replace(' ', '-')}-{sheet['publicationYear']}-review.png"
            dest = PREVIEW_DIR / output_name
            canvas.convert("RGB").save(dest, "PNG", optimize=True)
            info.update({
                "status": "preview-rendered-needs-human-review",
                "wmsMapId": entry.get("map_id"),
                "wmsLayer": layer,
                "originalLink": entry.get("permalink"),
                "bboxLonLat": [round(v, 8) for v in bbox],
                "previewFilename": output_name,
                "previewWidthPx": width,
                "previewHeightPx": height+70,
                "previewSha256": hashlib.sha256(dest.read_bytes()).hexdigest(),
                "originalResponseSha256": hashlib.sha256(response.content).hexdigest(),
            })
        except (requests.RequestException, OSError, ValueError, RuntimeError) as e:
            info["status"] = "preview-failed"
            info["error"] = f"{type(e).__name__}: {str(e)[:200]}"

    items = []
    for x in manifest["sheetOverlays"]:
        link = x.get("previewFilename")
        label = f"{x['sheetId']} {x['title']} ({x['year']})"
        items.append(
            f"<li>{label}: " +
            (f'<a href="{link}">지도+국경 후보선</a>' if link else x["status"]) +
            "</li>")
    html = """<!doctype html><html lang="ko"><head><meta charset="utf-8"><title>1914 North Schleswig review</title>
<style>body{font:16px sans-serif;max-width:880px;margin:2em auto;line-height:1.65;padding:1em;}
img{max-width:100%}a{color:#16588e}small{color:#566}</style></head><body>
<h1>독일제국–덴마크 국경 1–26 구간 지도 대조</h1>
<p>아래는 1878/1880년 지형도 영상 위에 현대 교구 경계 기반 <strong>미확정 후보선</strong>을
시각적으로 중첩한 검토본입니다. 정확성 확인이나 국경선 확정 결과가 아닙니다.</p>
<ul>""" + "\n".join(items) + """</ul>
<p><small>자료: SLUB Dresden / Deutsche Fotothek. 연구용 임시 자료. 원본 재배포 금지.
1914년 당시 지적·제방·수로 변화는 별도 확인해야 합니다.</small></p></body></html>"""
    (PREVIEW_DIR / "index.html").write_text(html, encoding="utf-8")
    OUTPUT.write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+"\n",
                      encoding="utf-8")
    print(json.dumps({
        "success": sum(x["status"] == "preview-rendered-needs-human-review"
                       for x in manifest["sheetOverlays"]),
        "total": len(manifest["sheetOverlays"]),
        "statuses": {x["sheetId"]: x["status"] for x in manifest["sheetOverlays"]},
        "metadata": str(OUTPUT),
        "temporaryPreviewPath": str(PREVIEW_DIR)
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
