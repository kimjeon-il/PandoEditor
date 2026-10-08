#!/usr/bin/env python3
"""Inspect Schleswig-Holstein's public Prussian Landesaufnahme WMS coverage.

Only service metadata is saved. A WMS service which does not spatially cover
historic Danish Schleswig cannot validate our border even if it displays
maps from 1878–1880. No raster scanning or publication occurs here.
"""
import json
import xml.etree.ElementTree as ET
from pathlib import Path

import requests

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / "tools/historical-library/land-borders/german-denmark-1914-north-schleswig-001-026.wms-services.json"
URL = "https://dienste.gdi-sh.de/WMS_SH_FD_Chronologen"
BORDER_BOUNDS = (8.62, 55.245, 8.99, 55.335)
CITE = "https://www.schleswig-holstein.de/DE/landesregierung/ministerien-behoerden/LVERMGEOSH/Service/serviceGeobasisdaten/geodatenService_Geobasisdaten_Dienste.html"


def local(tag):
    return tag.rsplit("}", 1)[-1] if isinstance(tag, str) else ""


def pick(element, child_name):
    return next((c for c in element if local(c.tag) == child_name), None)


def text_child(element, child_name):
    child = pick(element, child_name)
    return (child.text or "").strip() if child is not None else None


def bbox_for(layer):
    box = pick(layer, "EX_GeographicBoundingBox")
    if box is None:
        return None
    try:
        west = float(text_child(box, "westBoundLongitude"))
        east = float(text_child(box, "eastBoundLongitude"))
        south = float(text_child(box, "southBoundLatitude"))
        north = float(text_child(box, "northBoundLatitude"))
        return [west, south, east, north]
    except (TypeError, ValueError):
        return None


def overlap(a, b):
    return a and (a[0] <= b[2] and a[2] >= b[0]
                  and a[1] <= b[3] and a[3] >= b[1])


def main():
    report = {
        "schemaVersion": 1,
        "referenceDate": "1914-07-31",
        "service": URL,
        "officialInformation": CITE,
        "reportedDataRights": "CC BY 4.0 for Chronologen up to 1950, attribution LVermGeo SH",
        "candidateExtentLonLat": list(BORDER_BOUNDS),
        "candidateHistoricBorderVerified": False,
        "rasterDownloaded": False,
        "status": "not-attempted",
    }
    try:
        r = requests.get(URL, params={
            "SERVICE": "WMS", "REQUEST": "GetCapabilities", "VERSION": "1.3.0"
        }, timeout=35, headers={"User-Agent": "PandoEditor-HistoricalGIS/1.0"})
        report["httpStatus"] = r.status_code
        report["contentType"] = r.headers.get("Content-Type")
        r.raise_for_status()
        root = ET.fromstring(r.content)
        if "WMS_Capabilities" not in local(root.tag) and "WMT_MS_Capabilities" not in local(root.tag):
            raise RuntimeError("Unexpected WMS capabilities XML root: " + local(root.tag))
        cap = pick(root, "Capability")
        parent = pick(cap, "Layer") if cap is not None else None
        if parent is None:
            raise RuntimeError("WMS has no root Layer")
        report["rootBoundingBoxLonLat"] = bbox_for(parent)
        report["rootExtentIntersectsBorder"] = overlap(
            report["rootBoundingBoxLonLat"], BORDER_BOUNDS)
        layers = []
        def traverse(node, inherited_bbox=None):
            bounds = bbox_for(node) or inherited_bbox
            name = text_child(node, "Name")
            title = text_child(node, "Title")
            if name:
                layers.append({
                    "name": name,
                    "title": title,
                    "boundsLonLat": bounds,
                    "extentIntersectsCandidate": overlap(bounds, BORDER_BOUNDS),
                })
            for child in node:
                if local(child.tag) == "Layer":
                    traverse(child, bounds)
        traverse(parent)
        report["layerCount"] = len(layers)
        report["layers"] = layers[:160]
        report["status"] = "capabilities-read"
    except (requests.RequestException, ET.ParseError, RuntimeError) as ex:
        report["status"] = "not-accessible"
        report["error"] = f"{type(ex).__name__}: {str(ex)[:450]}"
    TARGET.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "status": report["status"],
        "httpStatus": report.get("httpStatus"),
        "rootBoundingBoxLonLat": report.get("rootBoundingBoxLonLat"),
        "rootExtentIntersectsBorder": report.get("rootExtentIntersectsBorder"),
        "layerCount": report.get("layerCount"),
        "candidateHistoricBorderVerified": False,
        "output": str(TARGET),
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
