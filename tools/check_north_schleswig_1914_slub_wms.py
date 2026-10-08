#!/usr/bin/env python3
"""Verify 1878–1880 SLUB original maps are reachable as WMS image samples.

No scan contents are committed: only capabilities, sampled image characteristics
and source provenance are saved. This does NOT trace or certify the 1914 border.
"""
from __future__ import annotations

import datetime as dt
import hashlib
import io
import json
import xml.etree.ElementTree as ET
from pathlib import Path
from urllib.parse import urlsplit

import requests
from PIL import Image
from shapely.geometry import shape, box

ROOT = Path(__file__).resolve().parents[1]
B = ROOT / "tools/historical-library/land-borders"
STEM = "german-denmark-1914-north-schleswig-001-026"
SHEETS = B / f"{STEM}.sheets.json"
DISCOVERY = B / f"{STEM}.slub-discovery.json"
SEGMENTS = B / f"{STEM}.sheet-segments.geojson"
OUTPUT = B / f"{STEM}.slub-wms-probe.json"
EXPECTED = {"0619", "0717", "0718", "0719"}


def local(tag):
    return tag.rsplit("}", 1)[-1]


def descendants(node, name):
    return [child for child in node.iter() if local(child.tag) == name]


def field(node, name):
    return next(((c.text or "").strip() for c in node if local(c.tag) == name), None)


def single_sheet_segment_segments(fc):
    result = {}
    for ft in fc.get("features", []):
        sheet_id = ft.get("properties", {}).get("sheetId")
        if sheet_id is None:
            continue
        g = shape(ft["geometry"])
        if not g.is_empty:
            result[sheet_id] = g
    return result


def sample_box(segment, sheet_extent):
    # Bound the sample request to a portion that actually intersects the
    # line, avoiding blank results on shoreline/outside-extent corners.
    minx, miny, maxx, maxy = segment.bounds
    sx0, sy0, sx1, sy1 = sheet_extent
    px = max(0.013, (maxx - minx) * 0.2)
    py = max(0.008, (maxy - miny) * 0.2)
    return [
        max(sx0, minx - px),
        max(sy0, miny - py),
        min(sx1, maxx + px),
        min(sy1, maxy + py),
    ]


def main():
    sheets = json.loads(SHEETS.read_text(encoding="utf-8"))["sheets"]
    discovery = json.loads(DISCOVERY.read_text(encoding="utf-8"))
    segs = single_sheet_segment_segments(
        json.loads(SEGMENTS.read_text(encoding="utf-8")))
    assert {s["sheetId"] for s in sheets} == EXPECTED
    report = {
        "schemaVersion": 1,
        "referenceDate": "1914-07-31",
        "collectedUtc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "candidateHistoricBorderVerified": False,
        "fullResolutionOriginalReviewed": False,
        "imageScanFilesCommitted": False,
        "sourceSearch": "https://search.kartenforum.slub-dresden.de/vk20",
        "mapProbes": [],
    }
    session = requests.Session()
    for sheet in sheets:
        number = sheet["sheetId"]
        matches = discovery["matches"].get(number) or []
        details = {
            "sheetId": number,
            "title": sheet["title"],
            "catalogPublicationYear": sheet["publicationYear"],
            "candidateLengthKm": sheet["candidateLengthKm"],
            "status": "not-queried",
        }
        report["mapProbes"].append(details)
        if len(matches) != 1:
            details["status"] = "catalog-ambiguous"
            details["matches"] = len(matches)
            continue
        entry = matches[0]["metadata"]
        details["mapId"] = entry.get("map_id")
        details["indexTitle"] = entry.get("title_long")
        details["indexGeoreferenced"] = entry.get("has_georeference")
        details["originalPermalink"] = entry.get("permalink")
        urls = [i.get("url") for i in (entry.get("online_resources") or [])
                if i.get("type", "").upper() == "WMS" and i.get("url")]
        if len(urls) != 1:
            details["status"] = "wms-link-absent"
            continue
        base = urlsplit(urls[0])
        if base.scheme != "https" or base.hostname != "wms.kartenforum.slub-dresden.de":
            details["status"] = "unsafe-wms-origin"
            continue
        service = f"{base.scheme}://{base.netloc}{base.path}"
        details["capabilitiesUrl"] = urls[0]
        try:
            caps = session.get(
                service, params={"service": "WMS", "version": "1.3.0",
                                 "request": "GetCapabilities"},
                timeout=30,
                headers={"User-Agent": "PandoEditor-HistoricalGIS/1.0"})
            caps.raise_for_status()
            details["capabilitiesHttpStatus"] = caps.status_code
            xml = ET.fromstring(caps.content)
            if "Capabilities" not in local(xml.tag):
                details["status"] = "non-wms-capabilities"
                details["responseSnippet"] = caps.text[:280]
                continue
            layers = [layer for layer in descendants(xml, "Layer")
                      if field(layer, "Name")]
            details["availableWmsLayerNames"] = [field(layer, "Name") for layer in layers[:20]]
            if not layers:
                details["status"] = "no-wms-layer"
                continue
            name = field(layers[-1], "Name")
            bounds = sample_box(segs[number], sheet["extentLonLat"])
            # WMS 1.3.0 EPSG:4326 requires latitude-longitude axis order.
            params = {
                "service": "WMS", "version": "1.3.0", "request": "GetMap",
                "layers": name, "styles": "", "crs": "EPSG:4326",
                "bbox": ",".join(str(v) for v in
                                 (bounds[1], bounds[0], bounds[3], bounds[2])),
                "width": 768, "height": 512,
                "format": "image/png", "transparent": "true",
            }
            r = session.get(service, params=params, timeout=45,
                            headers={"User-Agent": "PandoEditor-HistoricalGIS/1.0"})
            details["sampleHttpStatus"] = r.status_code
            details["sampleMimeType"] = r.headers.get("Content-Type")
            r.raise_for_status()
            image = Image.open(io.BytesIO(r.content))
            image.load()
            extrema = image.convert("RGBA").getextrema()
            histogram = image.convert("RGB").getcolors(maxcolors=768*512)
            alpha = image.convert("RGBA").getchannel("A")
            details["sample"] = {
                "format": image.format, "dimensions": list(image.size),
                "imageSha256": hashlib.sha256(r.content).hexdigest(),
                "rasterBytes": len(r.content),
                "rgbaExtrema": extrema,
                "opaquePixelBounds": alpha.getbbox(),
                "sampleExtentLonLat": [round(v, 7) for v in bounds],
                "distinctColors": len(histogram) if histogram is not None else ">393216",
            }
            details["status"] = "wms-image-decoded"
        except (requests.RequestException, ET.ParseError, ValueError,
                OSError, KeyError) as ex:
            details["status"] = "sample-failed"
            details["error"] = f"{type(ex).__name__}: {str(ex)[:450]}"

    OUTPUT.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                      encoding="utf-8")
    print(json.dumps({
        "outcome": [
            {"sheet": entry["sheetId"], "status": entry["status"],
             "layerNames": entry.get("availableWmsLayerNames"),
             "distinctColors": entry.get("sample", {}).get("distinctColors")}
            for entry in report["mapProbes"]
        ],
        "output": str(OUTPUT),
        "candidateHistoricBorderVerified": False,
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
