# Serbia–Montenegro 1914 border candidate — QA

Reference date: 1914-07-28

## Candidate extraction
- Source Montenegro: HistoGIS `Crna Gora`, valid 1913-08-11–1919-12-31.
- Source Serbia: HistoGIS `Srbija`, valid 1913-08-11–1919-12-31.
- Extraction method: exact undirected shared-edge intersection between the two state polygon rings, rounded to 1e-6 degrees only for handoff.
- One dominant continuous shared run was found.
- Exact shared edges: 1434
- Vertices: 1435
- Approximate geodesic length: 244.818 km
- Start: [19.226442, 43.527993]
- End: [20.0782, 42.5546]

## Primary historical control
The Serbia–Montenegro boundary agreement of 30 October O.S. / 12 November 1913 N.S. describes the frontier landmark by landmark. Article I states that the line was established according to an Austrian General Staff map at 1:200,000 attached to the agreement.

## Cartographic cross-checks required before finalization
1. Operationskarte B, Blatt 5 Plevlje.
2. Operationskarte B, Blatt 6 Noviparaz.
3. Operationskarte B, Blatt 9 Prizren.
4. Austrian Kriegsarchiv: `Die neue serbische Westgrenze von der Donau bis Elbassan` (1913).
5. 1914 Peucker `Südost-Europa mit den endgültigen Grenzen nach authentischen Materialien` as a lower-scale independent check.

## Current status
**PROVISIONAL — treaty verified, LoC georeferenced primary-map series located, partial visual checks completed.**

Detailed sheet-by-sheet work is recorded in `serbia-montenegro-1914-map-crosscheck.md`. The line remains a candidate until Bjelopolje 6763, Peštera 6764, Ipek 6964 and the small 34 XXIII grid-gap sector are resolved.


## Treaty contradiction found — Kanje / Metanac sector

The 1913 Serbia–Montenegro boundary agreement explicitly states that the boundary runs east along the ridge between **Kanje (Serbia)** and **Metanac/Metanjac (Montenegro)** and crosses the **Lim River between those two villages**.

A direct point-in-polygon check against the HistoGIS state polygons used to derive this candidate gives:
- Kanje (~19.75526, 43.13897): classified inside HistoGIS Montenegro, contrary to the treaty.
- Metanac/Metanjac (~19.77092, 43.12646): classified inside HistoGIS Montenegro, consistent with the treaty.

Therefore the exact HistoGIS shared-edge run is **not** an exact representation of the treaty frontier in the Kanje–Metanac sector. This is a substantive territorial-side mismatch, not merely a vertex-generalization issue.

Other treaty control settlements checked nearby mostly agree with the HistoGIS side assignment:
- Montenegro side: Mojstir, Požeginja, Donja Korita, Gornja Korita.
- Serbia side: Višnjevo, Krajinoviće, Bare, Crvsko, Boljare.

### Consequence

Do not promote this candidate geometry to final. The Kanje–Metanac–Lim segment must be reconstructed from the treaty route and contemporary maps before finalization. The rest of the line remains a candidate pending the same control procedure.
