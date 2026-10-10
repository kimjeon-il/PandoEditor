# Serbia–Montenegro 1914 border — historical map cross-check

Reference date: 1914-07-28

## 1. Primary legal control

The definitive Serbia–Montenegro boundary was fixed by the bilateral agreement signed in Belgrade on 30 October 1913 O.S. / 12 November 1913 N.S.

Article I states that the frontier line was established on an attached Austrian General Staff map at 1:200,000. Article II required a mixed commission to mark the line on the terrain; if the map and terrain disagreed, a Russian senior officer would arbitrate.

Public transcript reproducing the 1914 Serbian Foreign Ministry publication:
- https://www.antenam.net/clanak/301960-ugovor-o-razgranicenju-kraljevine-crne-gore-i-kraljevine-srbije

## 2. Candidate geometry

Source state polygons:
- ACDH-CH HistoGIS `Crna Gora`, valid 1913-08-11–1919-12-31
- ACDH-CH HistoGIS `Srbija`, valid 1913-08-11–1919-12-31

Exact shared-edge extraction:
- 1,434 edges
- 1,435 vertices
- approx. 244.818 km
- north endpoint: [19.226442, 43.527993]
- south endpoint: [20.078200, 42.554600]

The shared run is topologically exact between the two independent HistoGIS state objects. This is strong evidence but is not treated as a substitute for primary-map verification.

## 3. Austrian Spezialkarte 1:75,000 coverage

The candidate was routed through the Austrian 1:75,000 sheet grid. Relevant published sheet identities are:

| Candidate sector | Sheet | Tile | Title | Current verification |
| --- | --- | ---: | --- | --- |
| northern endpoint | 31 XX | — | Goražde und Čajniče | visual gross-route check completed |
| north | 32 XX | 6662 | Vikoč | visual gross-route check completed |
| north-central | 32 XXI | 6663 | Nova Varoš, Plevje | visual gross-route check completed |
| central | 33 XXI | 6763 | Bjelopolje | LoC georeferenced copies located; edition/date matching pending |
| central | 33 XXII | 6764 | Peštera | 1915 300 dpi LoC scan identified; detailed overlay pending |
| south-central | 34 XXII | 6864 | Berane | visual gross-route check completed |
| small eastern excursion | theoretical 34 XXIII | — | no indexed sheet found in Mapster/LoC index | unresolved sheet-gap issue |
| south | 35 XXII | 6964 | Ipek | 1914 edition reported by Mapster; LoC georeferenced copies located; detailed overlay pending |

Mapster index:
- https://igrek.amzp.pl/mapindex.php?cat=KUK075

## 4. Library of Congress georeferenced dataset

Library of Congress Labs publishes the Austrian-Hungarian Spezialkarte scans as original TIFFs and collar-removed georeferenced GeoTIFFs.

Manifest:
- https://data.labs.loc.gov/austro-hungarian-maps/manifest.html

Relevant manifest entries confirmed:

### Tile 6763 — Bjelopolje
- `6763_000_geo.tif`
- `6763_001_geo.tif`
- `6763_002_geo.tif`
- matching original-image TIFFs also exist

### Tile 6764 — Peštera
- `6764_000_geo.tif`
- `6764_001_geo.tif`
- `6764_002_geo.tif`
- matching original-image TIFFs also exist

Mapster independently identifies a LoC scan:
- sheet 33 XXII (6764)
- title PEŠTERA
- year 1915
- scale 1:75,000
- 300 dpi
- K.u.k. Militärgeographisches Institut in Wien
- LoC call number G6480 s75 .A8
- https://igrek.amzp.pl/details.php?id=11861941

### Tile 6964 — Ipek
- `6964_000_geo.tif`
- `6964_001_geo.tif`
- `6964_002_geo.tif`
- four original-image TIFF scans (`000`–`003`) are present in the manifest

## 5. Independent historical controls

A 1915–1916 British War Office / Ordnance Survey map of the Ipek region is available from the Library of Congress and can be used as an independent, lower-priority check after the Austrian primary-series comparison.

The Austrian Kriegsarchiv also catalogs:
- `Die neue serbische Westgrenze von der Donau bis Elbassan` (1913)

The 1914 Austrian 1:200,000 `Operationskarte B. Serbien, Montenegro u. Anland` series is another high-value control because the bilateral treaty itself references an Austrian General Staff 1:200,000 map. The relevant broad sheets are Plevlje, Novipazar and Prizren, but the exact physical identity of the treaty annex has not yet been proven.

## 6. Treaty-route consistency

The treaty text describes the boundary landmark-by-landmark from the Bosnia-Herzegovina frontier through the Pljevlja / Bijelo Polje / upper Ibar region, then Jablanica–Mokra Planina, Rakoš–Klina and finally the Beli Drim to the Albania tripoint.

The HistoGIS shared-border candidate follows the same gross geographic sequence. Detailed point-by-point landmark matching and raster overlay remain required before promotion to a final historical-library object.

## 7. Status

**PROVISIONAL — treaty verified, primary-map series and exact LoC tiles located, partial visual checks completed.**

Do not promote the candidate to final until:
1. Bjelopolje 6763 edition/date is identified and visually checked.
2. Peštera 6764 is overlaid against the candidate.
3. Ipek 6964 1914 edition is visually checked and overlaid.
4. The 34 XXIII sheet-gap / eastern excursion is resolved.
5. Treaty landmarks in the Jablanica–Mokra–Rakoš–Klina–Beli Drim sector are explicitly matched.
