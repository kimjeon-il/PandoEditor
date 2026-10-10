# Netherlands shoreline pilot — Phase 0 canonical baseline probe

**Status:** measured **national-polygon sample coverage only**; shoreline and hydrography reconstruction **not started**.  
**Checked:** 2026-10-09 on Web `Pando` branch `work/gis`.  
**Source:** Canonical source: `assets/data/territorial-entities/source/countries/nld.json` (repository root relative).  
**Canonical geometry:** `assets/data/territorial-entities/source/countries/nld.json`, entity `state:NLD`, version `state:NLD:natural-earth-5.1.1`.  
**Machine-readable source and regression:** [canonical-phase0-probes.json](canonical-phase0-probes.json), `tests/unit/historical-netherlands-coast-phase0.test.mjs`.

## 1. What was actually checked

A read-only point-in-MultiPolygon probe was run directly against the checked-in canonical NLD coordinate array, not a screenshot. The geometry has:

- **12 component polygons:** 9 in European Netherlands, 3 in the Caribbean, and **0 polygon interior holes**;
- no independent, period-specific historical line as part of this check;
- 12 hand-selected location probes, 11 inside the country polygon and 1 outside.

With the **current project flat-map normalization** (`flatZoom=64`, `2560 CSS px`), the published approximate Maasvlakte 2 location ([Wikidata place coordinate](https://www.wikidata.org/wiki/Q2733197), **3.9833°E, 51.9583°N**) falls outside the canonical NLD country outline. The nearest-boundary distance calculated against its vertices/segments is about **15.63 CSS px**. This is **not** an independent historical coastline difference, nor proof that the *whole* Maasvlakte 2 is absent. Dated 2010/2013 coastline polygons are still required.

All point positions other than the published Maasvlakte 2 point are **manually chosen approximate checkpoints**, not survey coordinates and not a test of the event footprint. A pass/fail against a sample is *only* about whether that particular point lies inside this country polygon.

## 2. Baseline point results

| Probe | Sample lon, lat | National polygon | What this does **not** establish |
| --- | --- | --- | --- |
| Maasvlakte 2 | 3.9833, 51.9583 | **outside** | Complete 2013 landfill outline or shoreline |
| Maasvlakte 1 control | 4.05, 51.96 | inside | Complete earlier port footprint |
| Noordoostpolder | 5.73, 52.70 | inside | 1942 shoreline or Urk connection |
| Oostelijk Flevoland | 5.56, 52.47 | inside | 1957 polder shoreline |
| Zuidelijk Flevoland | 5.26, 52.36 | inside | 1968 polder shoreline |
| Wieringermeer | 5.02, 52.82 | inside | 1930 shoreline or polder enclosure |
| IJsselmeer water | 5.25, 52.85 | **inside** | Whether the water is actually dry land |
| Markermeer water | 5.25, 52.55 | **inside** | Lake geometry, lake separation or flood connectivity |
| Marker Wadden candidate | 5.37, 52.59 | inside | Whether separate artificial islands are depicted |
| IJburg candidate | 5.01, 52.36 | inside | Whether phase-specific IJburg islands are depicted |
| Lauwersmeer water | 6.22, 53.38 | inside | 1969 closure shoreline or remaining open-water geometry |
| Braakman candidate | 3.63, 51.35 | inside | Whether the historical inlet existed |

The NLD national polygon covers large inland water areas. Therefore **point-in-country must never be used as a land/water, lake, artificial-island or polder-presence classifier**. It is only a first-pass *outer national footprint* check. The existing built-in lakes/hydro source requires a **separate** current-versus-historical review.

## 3. Dated event catalogue created

[change-events.json](change-events.json) records **nine** source-indexed physical events: Amsteldiepdijk (1924), Wieringermeer (1930), Afsluitdijk (1927–1932), Noordoostpolder (1942), Oostelijk Flevoland (1957), Zuidelijk Flevoland (1968), Lauwerszee closure (1969), Houtribdijk (1963–1976; 1975 closure) and Maasvlakte 2 (2008–2013).

All nine events are **`source-indexed` + `not-digitized`**. Sources and engineering/closure/drainage *dates* have been indexed; none has a verified dated coastal polygon, verified epoch-specific water mask or quantitative historical-screen Hausdorff comparison. In particular:

- Rijkswaterstaat dates the **Zuiderzee's final barrier closure to 1932-05-28** and the main polder dry years to 1930, 1942, 1957, 1968.
- Rijkswaterstaat dates **Lauwerszee closure to 1969-05-23**.
- Rijkswaterstaat dates **Houtribdijk gap closure to 1975-09-04**, with construction continuing to 1976.
- The Port of Rotterdam identifies **2013** as the Maasvlakte 2 opening; this is **not** necessarily the date every reclaimed area first became land.

Official primary-source URLs are recorded under each event's `sourceIds`. Unindexed checklist events remain in the [reverse timeline plan](netherlands-land-reclamation-reverse-timeline.md), not silently converted to verified geometry.

## 4. Implications for the general worldwide scheme

1. **Do not rewrite a country's full coast per year.** Store independently sourced events and actual date-validated local polygon/line changes.
2. **Separate national ownership, dry land, inland water and waterway connectivity.** A national polygon cannot stand in for all four; barrier closure may alter connectivity without creating the full land polygon.
3. **Never interpret a completed engineering project as an exact global polygon switch** unless source-based topology and waterline classification are established. Event intervals and exact hydraulic closure dates have different fields.
4. **Do not double-count cross-border features.** The Dollard/Ems sector must be cross-referenced to the existing German coast event inventory without copying the same reconstruction under two sovereigns.
5. **Preserve the 0.5 CSS px project rule** for independent historically dated geometry at 2560 CSS px and max flat zoom 64. Component identity, ownership and water topology are non-negotiable even at smaller differences.

## 5. Next physically correct geometry work

**First target: modern Maasvlakte 2 extent vs 2010 and 2013 dated coastlines.** The outside-point result suggests the canonical current outer coast may miss material modern fill at this location. Before generating rollback patches:

1. Obtain a reference-year modern extent from [PDOK Sea Regions coastline](https://www.pdok.nl/ogc-apis/-/article/zeegebieden) / TOP10NL and [Kadaster Topotijdreis](https://www.topotijdreis.nl/); review the date, CRS and actual licensing/attribution terms of each source.
2. Confirm whether the *actual web-rendered* NLD outer polygon, built-in lake mask and coastline display include the modern filled land; a point sample alone cannot certify that.
3. Trace and validate a complete modern footprint and a dated pre-fill outline **separately**. Only then derive a local proposed change with explicit land/water and port-basin semantics.
4. Compute independently sourced, projected **bidirectional** shoreline separation (not the place-to-boundary measurement above), test geometry validity, unchanged adjacent sectors and relevant topology, and publish a provenance record.
5. Keep the present world, German 1914 provisional working shape, original coastline input, and all user projects untouched until a specific verified patch passes.

After the modern port experiment, use Zuiderzee/Flevoland to test **hydrography and disconnected-island** transitions. Treat broad waterfront changes of Germany/Netherlands as *linked evidence*, not independent country-by-country edits.

**Out of scope here:** any Web/App runtime changes, generating 1914 Netherlands polygons, integrating the global event schema into production, changing official hydro files, historical Hausdorff verification or merging to `main`.


## 6. Current representation inventory — bounded source comparison

A second read-only Phase 0 pass now compares the current NLD country geometry
against the current Natural Earth lake source. Machine-readable results are in
[current representation inventory](phase0-current-representation-inventory.json).

Pinned source blobs for this pass:

- `assets/data/territorial-entities/source/countries/nld.json`:
  `b6bb670257d4e73d5fc4cf1880064b91bb365c8b`
- `assets/data/hydro/lakes_base.geojson`:
  `02ec805c9ec3347b81b7b6ec3aba5ed5eb0028a8`

### 6.1 What the current bundled data actually contains

Within the Netherlands study BBOX, the Natural Earth lake source contains only
two intersecting lake features:

- `Lauwersmeer`
- `IJsselmeer`

There is **no separate Markermeer feature**. The IJsselmeer feature is one
Polygon with one interior ring. Its outer extent covers both the IJsselmeer and
Markermeer sample areas.

Three samples placed on/near the Houtribdijk run
(`[5.37,52.61]`, `[5.45,52.54]`, `[5.31,52.69]`) all test **inside the
IJsselmeer water polygon**. Both the Markermeer water sample and IJsselmeer
water sample also test inside that same feature.

Therefore the current lake source must **not** be treated as a correct modern
hydraulic-topology baseline for the Houtribdijk / Markermeer split. At the
tested positions, the barrier is generalized away.

### 6.2 Large polders represented consistently with modern dry land

The existing Phase 0 samples for:

- Wieringermeer
- Noordoostpolder
- Oostelijk Flevoland
- Zuidelijk Flevoland

are all inside the current NLD national polygon and outside the current
IJsselmeer lake polygon. At these samples the bundled country+lake sources are
therefore consistent with the modern dry-land state.

This is still **sample-level evidence**, not proof of each full polder
shoreline.

### 6.3 Modern artificial islands not exposed by the current lake mask

The existing Marker Wadden sample (`[5.37,52.59]`) and IJburg sample
(`[5.01,52.36]`) both test inside the current IJsselmeer water polygon.

This means the current Natural Earth lake mask does not expose dry land at
those sample positions. It is not sufficient to infer whether every island is
absent from every render path, but the source geometry itself is too generalized
to serve as the modern rollback baseline for these projects.

Chronology controls:

- Natuurmonumenten: Marker Wadden construction began in 2016; the first five
  islands were completed by 2021 and islands 6–7 by 2023.
  https://www.natuurmonumenten.nl/projecten/marker-wadden/planning-en-voortgang
- Gemeente Amsterdam: IJburg first-phase construction began in 1999; the second
  phase began in 2013, Strandeiland in 2018 and Buiteneiland in 2023.
  https://www.amsterdam.nl/projecten/ijburg/geschiedenis/

These dates index events only; they are not substitute shoreline geometry.

### 6.4 Maasvlakte 2 remains the first external-geometry target

The published Maasvlakte 2 place point (`3.9833, 51.9583`) is outside the
current NLD polygon. The closest point found on the current NLD boundary is
approximately `[4.021169, 51.963324]`, about **2.66 km** away by local
WGS84-distance approximation. The Maasvlakte 1 control point
(`[4.05,51.96]`) remains inside.

This strengthens the earlier conclusion: the current Natural Earth national
outline misses at least part of the modern Maasvlakte 2 area. It still does not
establish the complete missing footprint. The next geometry source comparison
remains current PDOK/TOP10NL coastline versus dated pre-/post-reclamation
mapping.

PDOK documents its Zeegebieden dataset as TOP10NL-derived and containing
Shoreline and Coastline features:
https://www.pdok.nl/ogc-apis/-/article/zeegebieden

### 6.5 Lauwersmeer correction

The previous manually selected Lauwersmeer probe `[6.22,53.38]` is not inside
the bundled Lauwersmeer polygon and should not be used as a lake-presence
probe. The current source nevertheless does contain an explicit Lauwersmeer
Polygon. A geometry-derived interior point is approximately
`[6.1953974,53.3670096]`.

This correction does not change the historical conclusion: the current source
represents the enclosed modern lake, while the pre-1969 tidal connection still
requires dated historical geometry.

## 7. Phase 0 status after this pass

Current status:

- **modern dry-land sample confirmed:** Wieringermeer, Noordoostpolder,
  Oostelijk Flevoland, Zuidelijk Flevoland;
- **modern lake feature confirmed:** Lauwersmeer, IJsselmeer;
- **modern hydro topology deficient:** Houtribdijk / Markermeer separation;
- **recent artificial-land samples masked as water:** Marker Wadden, IJburg;
- **current outer coast materially incomplete at sample:** Maasvlakte 2;
- **still footprint-unresolved:** every event above until authoritative modern
  and dated geometry is compared.

No rollback polygon or historical shoreline has been generated by this pass.


## 8. 2026-10-10 investigation handoff — bounded lookup and priority

This pass rechecked the latest Phase 0 audit and the available bounded
inspector contract without changing production geometry. The next independent
geometry comparison is the **Maasvlakte 2 modern outer coast**: current
`state:NLD` boundary versus dated PDOK/TOP10NL and 2010/2013 Kadaster
coastlines. The existing outside point and 2.66 km distance are only a
screening observation, not a digitized footprint or bidirectional shoreline
comparison.

Recommended read-only local extraction:

```sh
node tools/inspect-world-data.mjs --country NLD --mode summary
node tools/inspect-world-data.mjs --country NLD --bbox 3.8,51.8,4.3,52.1 --mode boundary --out /tmp/nld-maasvlakte-boundary.geojson
node tools/inspect-world-data.mjs --country NLD --bbox 3.8,51.8,4.3,52.1 --mode polygon --out /tmp/nld-maasvlakte-polygon.geojson
```

The `boundary` result includes non-coastal boundaries; classify against
actual shore reference geometry. `polygon` returns whole components selected
by their envelopes rather than clipping them. Do not mark the Maasvlakte 2
event as fully present/absent until a complete dated footprint is obtained.

Second priority is IJsselmeer/Markermeer water topology and artificial
islands, since the existing bundled lake feature merges both lakes and masks
Marker Wadden / IJburg sample positions as water. Preserve separate statuses
for ownership, land, lake and connectivity.

**Status:** investigation continued; no new verified dated external coastline
polygon, geometry patch or completed footprint classification is claimed.


## 9. Natural Earth generalization / source-age audit — 2026-10-10

The earlier Maasvlakte 2 screening discrepancy must **not** be classified as a
verified missing modern reclamation footprint.

### 9.1 Intended scale makes kilometre-scale displacement plausible

Natural Earth explicitly describes its linework as carefully generalized for
fixed small-map scales. The 1:10m product is intended for maps around
1:10,000,000, where 1 cm on the map represents 100 km on the ground.

At that design scale, the current ~2.66 km screening distance between the
published Maasvlakte 2 place point and the Pando/Natural Earth NLD boundary is
only about **0.266 mm on the intended map**. That is small enough to be
cartographically generalized or absorbed by line shape/weight. Pando's
`flatZoom=64` then magnifies that source-scale generalization far beyond the
dataset's intended display scale, making the discrepancy visually prominent.

Natural Earth itself warns that its data is a general world dataset and that
zooming far beyond the intended display resolution exposes apparent boundary
inaccuracy.

References:
- https://www.naturalearthdata.com/
- https://www.naturalearthdata.com/downloads/
- https://www.naturalearthdata.com/about/data-creation/
- https://www.naturalearthdata.com/forums/reply/re-poor-accuracy-of-the-boundaries/

### 9.2 Version number is not a shoreline epoch

Natural Earth v5.1.1 was released in May 2022, but its changelog does **not**
describe a contemporary Netherlands shoreline refresh. The v5.1.1 changes to
admin-0 and land themes are primarily field/PostGIS compatibility changes.

The 2018 v4.1.0 release switched administrative and 10m land/ocean topology
building to MapShaper and states that some shapes were adjusted for new
snapping tolerances; it was not a systematic worldwide contemporary coastline
redigitization. Natural Earth's changelog contains no Maasvlakte entry and no
Netherlands mainland-coast update relevant to Maasvlakte 2.

The 2013 v3.0.0 coastline rebuild was triggered by specifically documented
New Zealand coastline changes. This reinforces the rule that a later Natural
Earth package version must not be interpreted as a surveyed shoreline of the
same year.

References:
- https://github.com/nvkelso/natural-earth-vector/blob/master/CHANGELOG
- https://github.com/nvkelso/natural-earth-vector/releases
- https://www.naturalearthdata.com/downloads/10m-physical-vectors/

### 9.3 Maasvlakte 2 is large but still near the Natural Earth detail threshold

Port of Rotterdam describes Maasvlakte 2 as about **2,000 ha (20 km²)** of new
land, constructed 2008–2013. The first development phase alone produced about
700 ha. This is a real, large reclamation, but its several-kilometre coastal
projection is still only fractions of a millimetre at Natural Earth's intended
1:10m scale.

References:
- https://www.portofrotterdam.com/en/news-and-press-releases/maasvlakte-2-five-years-operation
- https://www.portofrotterdam.com/sites/default/files/2023-03/widening-the-yangtze-canal.pdf

### 9.4 Revised classification

Current evidence supports this classification:

- **verified:** the Pando canonical NLD geometry follows a generalized Natural
  Earth 1:10m baseline, not a survey-grade current Dutch shoreline;
- **verified:** the published Maasvlakte 2 sample lies materially seaward of
  that generalized boundary at Pando max zoom;
- **not verified:** that the difference equals the actual Maasvlakte 2 missing
  footprint;
- **plausible causes:** intended cartographic generalization, old/unrefreshed
  coastline linework, and actual omission of some post-2008 reclamation;
- **cannot yet separate those causes quantitatively** without comparing the
  Natural Earth line to a current survey/topographic coastline and then to a
  pre-Maasvlakte-2 dated coastline.

Therefore the earlier wording “current outer coast materially incomplete at
sample” should be read as **“current generalized baseline differs materially
from survey-scale modern coastline at the sample when magnified to Pando max
zoom”**, not as proof of a source-data omission.

### 9.5 Correct comparison chain

For all Dutch reclamation work, use:

```text
Natural Earth/Pando current baseline
        ↓ compare
current PDOK/TOP10NL survey/topographic coastline
        = baseline generalization / source-age error
        ↓ compare
dated historical Dutch coastline
        = actual historical shoreline change
        ↓ generalize to Pando-compatible detail
historical Pando geometry
```

Do not use the raw Natural Earth-to-historical-map difference as the
reclamation delta.


## 10. Quantified Natural Earth simplification check — 2026-10-10

A machine-readable audit is now stored at
[phase0-generalization-audit.json](phase0-generalization-audit.json).

### 10.1 Vertex density

The current Pando NLD geometry contains:

- 12 component polygons;
- 854 coordinate positions total;
- approximate total boundary length: 1,974.7 km;
- mean source segment length: about 2.35 km.

For comparison, geoBoundaries' 2022 NLD ADM0 layer, sourced from the Dutch
National Georegister, reports:

- 36,029 vertices;
- perimeter about 2,041.2 km.

That is about **42 times as many vertices** as the Pando/Natural Earth
geometry. The high-precision layer's mean perimeter-per-vertex scale is roughly
57 m, versus kilometres in the Pando source. The datasets do not have identical
boundary semantics, so this is a density/generalization comparison, not a
coordinate-by-coordinate shoreline validation.

### 10.2 Rotterdam / Maasvlakte source spacing

On the Pando outer-ring run inside BBOX `3.8,51.8,4.3,52.1`, a 26.24 km run
is represented by only 17 vertices / 16 segments:

- mean segment: about 1.64 km;
- median segment: about 0.90 km;
- longest segment: about 4.56 km.

The earlier 2.66 km Maasvlakte 2 place-to-boundary screening distance is
therefore of the same order as the source line's own generalized segment
lengths. It cannot be used as standalone evidence that a 2.66 km-wide strip of
reclamation is missing.

### 10.3 Aggregate area is nevertheless close

Approximate spherical area of the Pando NLD MultiPolygon is about
**37,253 km²**.

CBS 2022 reports:

- land: **33,626 km²**;
- inland water: **3,748.92 km²**;
- land + inland water: **37,374.92 km²**;
- outside/tidal water: **4,168.45 km²**.

Thus the Pando polygon is only about **122 km² (-0.33%)** below the CBS
land-plus-inland-water total, despite the much coarser shoreline. This strongly
supports the interpretation that the current polygon preserves national-scale
area reasonably well while local coast shape is heavily generalized.

It also explains why the country polygon is a poor dry-land mask: the excess
of Pando polygon area over CBS dry land is about **3,627 km²**, close to the
CBS inland-water total. Removing a complete, correctly aligned inland-water
mask would therefore move the result close to dry-land area.

However, the current bundled Natural Earth lake layer is **not** such a
complete mask: the Netherlands Phase 0 inventory finds only Lauwersmeer and
IJsselmeer in the study BBOX, with Markermeer generalized into IJsselmeer and
many other inland waters absent. Therefore “subtract the current lake
polygons” is only an approximation, not an exact dry-land reconstruction.

### 10.4 Revised Maasvlakte 2 conclusion

The strongest current interpretation is now:

1. **Natural Earth generalization is definitely material** at Pando max zoom.
2. Aggregate area is close enough that a local several-kilometre visual
   displacement does not imply a comparable national area error.
3. Maasvlakte 2 may still be partly absent or represented by older linework,
   but that has **not** been separated quantitatively from generalization yet.
4. The deciding comparison remains the current TOP10NL/PDOK mean-high-water
   coastline against the Natural Earth run, followed by a dated pre-reclamation
   coastline.

No historical or current production geometry was modified by this check.


## 11. Whole-Netherlands country-minus-water test — 2026-10-10

Machine-readable results:
[phase0-land-water-subtraction-audit.json](phase0-land-water-subtraction-audit.json).

### 11.1 European Netherlands aggregate area

The current Pando European NLD components total approximately
**36,957.00 km²**.

CBS NBBG2022 reports for Nederland:

- dry land: **33,626.00 km²**;
- inland water: **3,748.92 km²**;
- land + inland water: **37,374.92 km²**.

Thus the unmasked Pando European country polygon is about **417.92 km²
(-1.12%)** below the CBS land+inland-water total. This is consistent with a
generalized administrative polygon that broadly includes inland water while
losing/redistributing some detailed coastline area.

CBS source:
https://www.cbs.nl/nl-nl/cijfers/detail/86211ned

CBS states that its land/water split is based on BRT topographic water
surfaces, including drying water parts.

### 11.2 What the current bundled lake mask removes

Within the Netherlands study BBOX the current bundled Natural Earth lake source
contains only:

- IJsselmeer: approximately **1,961.58 km²** in the current source geometry;
- Lauwersmeer: approximately **30.72 km²**.

Total current removable lake area is therefore approximately
**1,992.30 km²**.

That is only **53.14%** of the CBS official inland-water area by aggregate area
equivalence.

Subtracting these two lake polygons from the current Pando European NLD area
gives approximately:

**34,964.71 km²**

versus the CBS dry-land figure:

**33,626.00 km²**.

The derived land remains approximately **1,338.71 km² too large**, or
**+3.98%**.

Therefore:

> **current Pando NLD polygon − current bundled lake polygons is not an
> accurate dry-land polygon.**

### 11.3 Why the remaining error is large

CBS divides the **3,748.92 km²** of inland water approximately as follows:

| CBS inland-water class | Area |
| --- | ---: |
| IJsselmeer + Markermeer | 1,820.88 km² |
| closed sea arms | 320.85 km² |
| Rhine and Meuse | 183.16 km² |
| Randmeren | 155.14 km² |
| reservoirs | 13.33 km² |
| recreational inland water | 112.70 km² |
| extraction water | 37.04 km² |
| flow/sludge fields | 4.86 km² |
| other inland water | 1,100.95 km² |

The bundled lake mask represents the large IJsselmeer system and Lauwersmeer,
but it does **not** provide a complete polygon mask for the Rhine/Meuse,
Randmeren, closed sea arms, canals and the very large CBS “other inland water”
class.

The Natural Earth IJsselmeer polygon is itself generalized: its calculated
area is about **140.70 km² (+7.73%)** larger than CBS's combined
IJsselmeer+Markermeer category. Therefore the net missing-water estimate is not
a direct polygon difference; over- and under-generalization partly cancel.

### 11.4 Regional usefulness

The subtraction is still useful as a **coarse historical-reclamation
baseline**, but usefulness varies by region:

- **Zuiderzee / Flevoland:** broad modern dry-land pattern is useful. Large
  polders remain outside the lake polygon. Houtribdijk/Markermeer separation
  and Randmeren are incomplete.
- **Lauwersmeer:** explicit current lake polygon exists.
- **Rhine–Meuse delta / Zeeland:** poor dry-land mask. Major river and
  closed-sea-arm water areas are not represented by the current lake layer.
- **Randstad and smaller lakes:** incomplete; many water surfaces remain inside
  the derived “land”.
- **small dikes / narrow water barriers:** topology can still be wrong even
  where their area is below the map's ordinary visual threshold.

So for the reclamation project the current subtraction can help identify the
largest Flevoland-era changes, but it must not become the canonical modern
land/water reference.

### 11.5 Caribbean components

The Pando NLD object also contains Bonaire, Sint Eustatius and Saba. Their
combined calculated area is about **296.36 km²**. Rijksoverheid gives
approximately **322 km²** total (288 + 21 + 13 km²).

This difference is a separate 1:10m small-island generalization issue and is
not part of the CBS European NBBG2022 land/water table.

Source:
https://www.rijksoverheid.nl/vraag-en-antwoord/caribische-deel-van-het-koninkrijk/waaruit-bestaat-het-koninkrijk-der-nederlanden

### 11.6 Working decision

For subsequent Netherlands rollback work:

1. keep the current country polygon as the coarse ownership/outer-footprint
   baseline;
2. use the current Natural Earth lake mask only as a coarse visual aid;
3. do **not** define modern dry land as country minus that mask;
4. obtain a fuller BRT/TOP10NL or NBBG-derived water polygon control before
   generating canonical modern/historical dry-land subtraction;
5. continue treating country ownership, dry land, water polygons and hydraulic
   connectivity as separate layers of evidence.

No production geometry was modified by this audit.


## 11. Whole-country “country polygon − current water = land” audit — 2026-10-10

Machine-readable measurements:
[phase0-country-minus-water-area-audit.json](phase0-country-minus-water-area-audit.json).

This audit uses the **European Netherlands only**. The current NLD source also
contains three Caribbean components (Bonaire, Sint Eustatius and Saba), while
the CBS national land-use totals used here refer to the Netherlands' European
provincial territory. Mixing those components made the earlier whole-NLD area
comparison look artificially closer.

### 11.1 Current Pando country polygon

European NLD components:

- 9 polygon components;
- approximate spherical area: **36,957.00 km²**.

CBS 2022:

- dry land: **33,626.00 km²**;
- inland water: **3,748.92 km²**;
- dry land + inland water: **37,374.92 km²**.

Thus the current Pando European country polygon is about **417.92 km²
(-1.12%)** below the CBS land+inland-water total. Its semantics are therefore
much closer to “territorial land plus inland water” than to a dry-land mask.

CBS source:
https://www.cbs.nl/nl-nl/cijfers/detail/86211NED

### 11.2 What the current bundled lake layer actually removes

Only two `lakes_base.geojson` polygons intersect the European Netherlands:

| Current bundled feature | Approx. area |
| --- | ---: |
| IJsselmeer (Natural Earth polygon also covers Markermeer) | 1,961.58 km² |
| Lauwersmeer | 30.72 km² |
| **Total** | **1,992.30 km²** |

All vertices of these two polygons test inside the current European NLD
country geometry, so the first-pass area subtraction does not need an
intersection correction for these features.

Actual current-source subtraction:

```text
36,957.00 km²  current European NLD
-1,992.30 km²  current bundled lake polygons
------------
34,964.71 km²  derived “land”
```

CBS dry land is 33,626.00 km². Therefore this derived value is
**1,338.71 km² too large, or +3.98%**.

### 11.3 Why it fails

The current Natural Earth lake layer represents only about **53.14%** of the
CBS inland-water area:

```text
Natural Earth bundled lakes: 1,992.30 km²
CBS inland water:            3,748.92 km²
unrepresented by lake area:  1,756.62 km²
```

So about **46.86% of official inland-water area has no corresponding current
bundled lake polygon area** in this national comparison.

CBS inland water includes not only IJsselmeer/Markermeer but also closed sea
arms, Rhine/Meuse waters, Randmeren, reservoirs, recreational water and other
inland water. CBS defines inland-water polygons down to 6 m width and generally
1 ha area for the smaller categories. Natural Earth `lakes_base` is a
selective small-scale cartographic layer and is not intended as a complete
Dutch water mask.

CBS category definitions:
https://www.cbs.nl/nl-nl/cijfers/detail/86211NED

### 11.4 What a complete water mask would imply

If one uses the CBS inland-water total purely as an area control:

```text
36,957.00 − 3,748.92 = 33,208.08 km²
```

This is about **417.92 km² (-1.24%)** below CBS dry land, exactly reflecting
the Pando country polygon's pre-existing area deficit versus CBS
land+inland-water.

This is **not** a geometric overlay result, but it shows that the model
“country polygon − complete inland-water mask ≈ dry land” is structurally
sound to roughly the 1% level. The model
“country polygon − current Natural Earth lakes = dry land” is **not**.

### 11.5 Final Phase 0 classification

- **Current Pando country polygon − current Pando lake polygons:** **FAIL** as
  a dry-land mask; about +4.0% / +1,339 km² too much area.
- **Current Pando country polygon − complete authoritative inland-water mask:**
  **plausibly close**, with the country baseline itself already about 1.1%
  below CBS land+inland-water.
- **Next exact test:** intersect the current NLD polygon with the complete
  current TOP10NL `waterdeel_vlak` coverage and compute the actual geometric
  difference. PDOK documents TOP10NL as nationwide and updated through
  2026-09-03.

PDOK water polygon collection:
https://api.pdok.nl/kadaster/brt-top10nl/ogc/v1/collections/waterdeel_vlak?f=html

No production geometry was modified.
