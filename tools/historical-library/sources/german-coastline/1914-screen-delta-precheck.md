# 1914 German Empire coast vs provisional modern coastline — screen-space preliminary check

**As of:** 2026-10-09  
**Branch:** `work/gis`  
**Status:** `PROVISIONAL_SOURCE_GEOMETRY_DIAGNOSTIC` — **not** `historically-verified`, `overlay-reviewed` or `screen-delta-checked` against independently digitized 1914 maps.

## Scope and decisive limitation

This records a real *coordinate-to-coordinate* comparison for mainland North Sea and Baltic coast arcs. It does **not** measure the historically correct 1914-to-present reclamation delta. The older HistoGIS national polygon is a low-resolution historical working **candidate**, not an independently rectified 1914 coastline raster/vector. The current `work/gis` base deliberately uses a **modern, generalized website coastline** as a temporary stand-in. Differences include cartographic generalization, mismatched estuary/lagoon interpretations, boundary endpoint snapping, and potentially real shoreline changes. **Do not attribute any observed value to a specific reclamation or edit a country polygon on this evidence alone.**

Independent scanned-period-map alignment, shoreline digitization, actual bidirectional projected-line Hausdorff comparison, dates of actual dry-land conversion, and island checks remain incomplete. **No sector has passed or failed the 0.5 CSS px historical-accuracy requirement.** All 19 recorded coastline events in `sources/german-coastline/change-events.json` remain source-indexed, not digitized.

## Exactly which sources were compared

- Historical candidate: `tools/historical-library/working/german-empire-1914-base.geojson` **as stored at commit** [`143a88d66c72`](https://github.com/kimjeon-il/Pando/blob/143a88d66c72ac37f787d312bd7a1769c7872207/tools/historical-library/working/german-empire-1914-base.geojson), prior to replacement of its coastal sections by the website's modern coastline. The candidate derives from HistoGIS and its survey dates and coastline accuracy are **not independently established** by this comparison.
- Modern candidate: the *same path* on `work/gis` as read for this check, blob SHA `39fce9309edbe47bf91259d9c90c68f11328eb75`; feature `status=working-base-modern-coast-not-final`. Modern shoreline material derives from the website Natural Earth v5.1.1 polygon plus reviewed working alterations. **Not a cadastral precision modern shoreline.**
- North Sea mainland arcs: historical main ring indices `6528..end + 0..583` (651 vertices); modern ring `5187..end + 0` (333 vertices). Historical endpoints approximately Dollart/Ems and North Schleswig.
- Baltic mainland arcs: historical main ring indices `650..2499` (1,850 vertices); modern ring `67..1160` (1,094 vertices). Approximate endpoints are North Schleswig east coast and Memel north. **Separate island polygons are intentionally excluded**, including Helgoland and Düne: their modern shapes do **not** establish 1914 shorelines.
- Both arcs are checked in both directions. Source paths and indices are fixed for reproducibility: **rerun or re-identify the coastal arcs if the working geometry changes.**

## Calculation

- Flat equirectangular web projection at `flatZoom=64`, viewport content width `2560 CSS px`; scale `2560×64/360 = 455.111111111 CSS px per longitude/latitude degree`.
- For each vertex in a defined sector **geographic bounding box**, compute Euclidean pixel distance to the *nearest line segment anywhere on the opposite extracted coastal arc*, not simply the nearest vertex. Repeat with roles reversed.
- Record each direction's maximum **vertex-to-opposite-polyline** distance. Region statistic is the larger of those two directed values. Values below are rounded to 0.01px *for reproducibility*, not as a claim of historical or raster georegistration accuracy.
- This is a **sampled-vertex diagnostic**, not the exact continuous polyline Hausdorff distance; dense supplemental interpolation and independently digitized map-to-map registration were not performed. Some region masks overlap; regional extrema are not additive. Larger numbers often reflect **deficient source topology or missing inlets**, not coastal reclamation. Modern polygon and historical polygon share some inherited inland border geometry, making such sections unsuitable as independent controls.
- ROI coordinates are listed in `results` below for reuse; pixel distance is a screen projection diagnostic, **not** an official survey error or legal tolerance.

## Candidate discrepancies

Columns: historic-source vertex count / modern-source vertex count falling within bounding box, historical→modern maximum, modern→historical maximum, sampled bidirectional maximum (CSS px).

| ROI | Sector | Sample points (H/M) | H→M max px | M→H max px | Candidate max px |
|---|---|---:|---:|---:|---:|
| N01 | Dollart / Emden | 43/28 | 7.81 | 8.64 | **8.64** |
| N02 | Leybucht / Greetsiel | 16/7 | 4.55 | 4.73 | **4.73** |
| N03 | Harle | 37/6 | 3.36 | 2.16 | **3.36** |
| N04 | Jade / Wilhelmshaven | 172/83 | 8.19 | 82.13 | **82.13** |
| N05 | Weser / Bremerhaven | 102/54 | 6.80 | 70.80 | **70.80** |
| N06 | Elbe / Cuxhaven | 160/82 | 6.67 | 19.25 | **19.25** |
| N07 | Dithmarschen | 129/65 | 8.16 | 5.00 | **8.16** |
| N08 | Eiderstedt / Husum | 125/59 | 6.01 | 6.01 | **6.01** |
| N09 | North Frisia | 80/33 | 19.35 | 78.55 | **78.55** |
| N10 | North Schleswig coast | 42/37 | 10.08 | 92.50 | **92.50** |
| B01 | Flensburg Fjord | 361/143 | 21.31 | 32.24 | **32.24** |
| B02 | Kiel / Schlei | 173/113 | 11.43 | 35.17 | **35.17** |
| B03 | Lübeck / Fehmarn vicinity | 279/115 | 20.88 | 8.86 | **20.88** |
| B04 | Mecklenburg / Rostock | 343/128 | 20.88 | 9.41 | **20.88** |
| B05 | Fischland-Darss / Stralsund | 308/177 | 14.31 | 9.41 | **14.31** |
| B06 | Stettin / Oder mouth | 128/217 | 7.82 | 92.13 | **92.13** |
| B07 | Pomeranian coast | 75/73 | 48.28 | 68.59 | **68.59** |
| B08 | Danzig / Gdynia | 90/71 | 6.76 | 6.56 | **6.76** |
| B09 | Pillau / Samland | 176/54 | 148.68 | 8.37 | **148.68** |
| B10 | Memel / Curonian Lagoon | 170/137 | 20.02 | 17.79 | **20.02** |

> **WARNING:** Several extreme values (e.g. >50 px at Jade/Weser, northern Schleswig, Szczecin/Oder and Pillau/Samland) are conspicuous **source-geometry anomalies**, not established historical reclamation amounts. Very large regions also include lagoons/inlets with differing classification and have overlapping rectangular ROIs. The smallest values (3–9px) can also be caused entirely by HistoGIS/Natural Earth line generalization. None can be interpreted as an authenticated 1914 shoreline change.

### Reproducibility: region boxes and maximum sample locations

Each entry is `[west_lon, south_lat, east_lon, north_lat]` and both directions' maximum-sample `[lon, lat]`.

```json
[
  {
    "id": "N01",
    "bbox": [
      6.99,
      53.16,
      7.29,
      53.6
    ],
    "h2m": {
      "maxPx": 7.81,
      "at": [
        7.00639,
        53.39067
      ],
      "p95Px": 6.17
    },
    "m2h": {
      "maxPx": 8.64,
      "at": [
        7.26832,
        53.32368
      ],
      "p95Px": 4.83
    }
  },
  {
    "id": "N02",
    "bbox": [
      7.2,
      53.45,
      7.68,
      53.77
    ],
    "h2m": {
      "maxPx": 4.55,
      "at": [
        7.4577,
        53.68479
      ],
      "p95Px": 4.02
    },
    "m2h": {
      "maxPx": 4.73,
      "at": [
        7.45883,
        53.69514
      ],
      "p95Px": 2.94
    }
  },
  {
    "id": "N03",
    "bbox": [
      7.6,
      53.63,
      8.15,
      53.91
    ],
    "h2m": {
      "maxPx": 3.36,
      "at": [
        8.02073,
        53.68721
      ],
      "p95Px": 2.5
    },
    "m2h": {
      "maxPx": 2.16,
      "at": [
        7.95143,
        53.72191
      ],
      "p95Px": 0.88
    }
  },
  {
    "id": "N04",
    "bbox": [
      7.8,
      53.35,
      8.62,
      53.87
    ],
    "h2m": {
      "maxPx": 8.19,
      "at": [
        8.15381,
        53.50556
      ],
      "p95Px": 6.09
    },
    "m2h": {
      "maxPx": 82.13,
      "at": [
        8.50441,
        53.35806
      ],
      "p95Px": 39.63
    }
  },
  {
    "id": "N05",
    "bbox": [
      8.2,
      53.38,
      8.82,
      53.87
    ],
    "h2m": {
      "maxPx": 6.8,
      "at": [
        8.24314,
        53.39255
      ],
      "p95Px": 5.88
    },
    "m2h": {
      "maxPx": 70.8,
      "at": [
        8.50733,
        53.3828
      ],
      "p95Px": 39.63
    }
  },
  {
    "id": "N06",
    "bbox": [
      8.5,
      53.45,
      9.65,
      54.05
    ],
    "h2m": {
      "maxPx": 6.67,
      "at": [
        8.51596,
        53.53878
      ],
      "p95Px": 3.97
    },
    "m2h": {
      "maxPx": 19.25,
      "at": [
        8.51287,
        53.49649
      ],
      "p95Px": 6.69
    }
  },
  {
    "id": "N07",
    "bbox": [
      8.4,
      53.85,
      9.2,
      54.3
    ],
    "h2m": {
      "maxPx": 8.16,
      "at": [
        9.0363,
        54.09518
      ],
      "p95Px": 4.16
    },
    "m2h": {
      "maxPx": 5,
      "at": [
        8.91798,
        54.14631
      ],
      "p95Px": 3.73
    }
  },
  {
    "id": "N08",
    "bbox": [
      8.25,
      54.24,
      9.15,
      54.65
    ],
    "h2m": {
      "maxPx": 6.01,
      "at": [
        8.63967,
        54.34153
      ],
      "p95Px": 4.52
    },
    "m2h": {
      "maxPx": 6.01,
      "at": [
        8.88795,
        54.30394
      ],
      "p95Px": 4.94
    }
  },
  {
    "id": "N09",
    "bbox": [
      8.2,
      54.56,
      9.18,
      55.08
    ],
    "h2m": {
      "maxPx": 19.35,
      "at": [
        8.61016,
        54.88847
      ],
      "p95Px": 13.32
    },
    "m2h": {
      "maxPx": 78.55,
      "at": [
        8.47022,
        55.0786
      ],
      "p95Px": 64.41
    }
  },
  {
    "id": "N10",
    "bbox": [
      8.38,
      55.04,
      8.93,
      55.32
    ],
    "h2m": {
      "maxPx": 10.08,
      "at": [
        8.68061,
        55.23234
      ],
      "p95Px": 7.47
    },
    "m2h": {
      "maxPx": 92.5,
      "at": [
        8.47022,
        55.17756
      ],
      "p95Px": 88.43
    }
  },
  {
    "id": "B01",
    "bbox": [
      9.42,
      54.68,
      10.18,
      55.4
    ],
    "h2m": {
      "maxPx": 21.31,
      "at": [
        9.61179,
        55.27656
      ],
      "p95Px": 9.57
    },
    "m2h": {
      "maxPx": 32.24,
      "at": [
        9.92994,
        54.68309
      ],
      "p95Px": 8.29
    }
  },
  {
    "id": "B02",
    "bbox": [
      9.6,
      54.19,
      10.62,
      54.91
    ],
    "h2m": {
      "maxPx": 11.43,
      "at": [
        9.66881,
        54.90822
      ],
      "p95Px": 7.49
    },
    "m2h": {
      "maxPx": 35.17,
      "at": [
        9.92945,
        54.6739
      ],
      "p95Px": 25.28
    }
  },
  {
    "id": "B03",
    "bbox": [
      10.55,
      53.74,
      11.72,
      54.7
    ],
    "h2m": {
      "maxPx": 20.88,
      "at": [
        11.54123,
        54.07825
      ],
      "p95Px": 11.67
    },
    "m2h": {
      "maxPx": 8.86,
      "at": [
        11.47242,
        53.96882
      ],
      "p95Px": 6.16
    }
  },
  {
    "id": "B04",
    "bbox": [
      11.45,
      53.85,
      12.6,
      54.5
    ],
    "h2m": {
      "maxPx": 20.88,
      "at": [
        11.54123,
        54.07825
      ],
      "p95Px": 11.71
    },
    "m2h": {
      "maxPx": 9.41,
      "at": [
        12.53387,
        54.48827
      ],
      "p95Px": 6.52
    }
  },
  {
    "id": "B05",
    "bbox": [
      12.35,
      53.8,
      13.54,
      54.75
    ],
    "h2m": {
      "maxPx": 14.31,
      "at": [
        12.79008,
        54.33962
      ],
      "p95Px": 7.47
    },
    "m2h": {
      "maxPx": 9.41,
      "at": [
        12.53387,
        54.48827
      ],
      "p95Px": 6.02
    }
  },
  {
    "id": "B06",
    "bbox": [
      13.61,
      53.35,
      14.55,
      54.31
    ],
    "h2m": {
      "maxPx": 7.82,
      "at": [
        14.54096,
        53.67974
      ],
      "p95Px": 5.37
    },
    "m2h": {
      "maxPx": 92.13,
      "at": [
        14.40675,
        53.92182
      ],
      "p95Px": 81.94
    }
  },
  {
    "id": "B07",
    "bbox": [
      14.45,
      53.82,
      17.91,
      55.19
    ],
    "h2m": {
      "maxPx": 48.28,
      "at": [
        14.74878,
        53.91625
      ],
      "p95Px": 41.26
    },
    "m2h": {
      "maxPx": 68.59,
      "at": [
        14.51735,
        53.96524
      ],
      "p95Px": 16.69
    }
  },
  {
    "id": "B08",
    "bbox": [
      17.8,
      54.05,
      19.11,
      55.12
    ],
    "h2m": {
      "maxPx": 6.76,
      "at": [
        18.5519,
        54.52604
      ],
      "p95Px": 4.79
    },
    "m2h": {
      "maxPx": 6.56,
      "at": [
        17.88543,
        54.82412
      ],
      "p95Px": 4.2
    }
  },
  {
    "id": "B09",
    "bbox": [
      19.07,
      54.3,
      20.72,
      55.25
    ],
    "h2m": {
      "maxPx": 148.68,
      "at": [
        20.27881,
        54.62521
      ],
      "p95Px": 125.53
    },
    "m2h": {
      "maxPx": 8.37,
      "at": [
        19.92066,
        54.88959
      ],
      "p95Px": 7.47
    }
  },
  {
    "id": "B10",
    "bbox": [
      20.64,
      54.82,
      21.62,
      55.91
    ],
    "h2m": {
      "maxPx": 20.02,
      "at": [
        21.31556,
        55.25184
      ],
      "p95Px": 10.56
    },
    "m2h": {
      "maxPx": 17.79,
      "at": [
        21.26905,
        55.25357
      ],
      "p95Px": 11.49
    }
  }
]
```

## Independent period-map sources needed to turn these into actual 1914 deltas

1. **Schleswig-Holstein official Chronologen WMS/WCS** — 1:25,000 period sheets, 1878–1950, CC BY 4.0: [German open-data register](https://data.gov.de/suche/daten/preussische-landesaufnahme-bis-1950-chronologen), [WMS](https://dienste.gdi-sh.de/WMS_SH_FD_Chronologen), [WCS](https://dienste.gdi-sh.de/WCS_SH_FD_Chronologen). Individual *survey/revision* years still need checking. This is the leading independent overlay candidate for the North Frisian and Schleswig–Holstein coasts.
2. **Niedersachsen PL25 official historical map series** — 1877–1912, 1:25,000: [state catalogue](https://numis.niedersachsen.de/trefferanzeige?docuuid=b315810a-3039-4d83-86e8-0683265eb373). The service previously returned HTTP 401 to the automated runner; no period coastline raster was extracted for this check. Relevant open historical map-sheet catalogues are in `german-empire-1914-north-sea-mainland-coast-audit.json`.
3. **Baltic / East Prussia**: original TK25 1908/1914 Pillau, 1908/1916 Zimmerbude, 1908/1916 Königsberg west and the 1937 1:5,000 Gdynia port plan are catalogued in `sources/german-coastline/change-events.json`. None was independently registered or line-digitized for this run.
4. **Helgoland**: British Admiralty No. 126 chart published September 1914 and 1910 map are listed in `working/german-empire-1914-helgoland-temporal-qa.json`. Modern Helgoland/Düne geometry remains `provisional-modern-proxy`, with no valid pixel delta to 1914.

## Next controlled pass / promotion gates

- First georeference appropriate 1900–1914 period sheet(s), preserving scan provenance, control-point residuals, shoreline/foreshore and engineering/dyke classifications.
- Digitize genuine 1914 coastline and independently comparable current coastline **within the same named shoreline definition**, including island and lagoon topology. Separate a completed embankment from reclaimed *dry land*.
- Compute **bidirectional continuous projected-line** (screen-space) Hausdorff distances with endpoints and date/ownership/topology exceptions separately assessed; report coverage gaps rather than silently masking them. If validated line discrepancy ≤0.5 CSS px and no chronology/ownership/topology exception exists, stop finer digitization.
- Prioritize **Wilhelmshaven/Jade (historical sheet), Eider/North Frisia and North Schleswig (official WCS), Gdynia/Gdańsk and Pillau/Königsberg (TK25), and the 1914 Helgoland chart**. Reclamation work/event-year alone cannot establish a polygon switch date.
- **Do not** change the main country polygon, historical timeline, `main` branch, or app geometry on the basis of this precheck.

References: `docs/historical-border-reconstruction-policy.md`; `tools/historical-library/working/german-empire-1914-north-sea-mainland-coast-audit.json`; `tools/historical-library/working/german-empire-1914-northsea-dollart-jade.diagnostics.json`; `tools/historical-library/sources/german-coastline/README.md`.
