# Netherlands land-reclamation rollback survey

Status: **planning / research only**  
Target branch: `work/gis`  
Prepared: 2026-10-08  
Primary target: current Pando web map → progressively older Dutch coastline / hydrography → 1914

## 1. Purpose

The Netherlands must not be reconstructed by jumping directly from the current
country polygon to a single 1914 polygon. Large-scale reclamation, closure of
sea inlets, artificial islands and port expansion changed both the coastline
and the topology of inland/coastal waters in multiple distinct steps.

The working rule for this project is therefore:

1. start from the geometry that is actually visible in the current website;
2. identify changes large enough to be visible at the website's maximum zoom;
3. reverse those changes in chronological order;
4. keep intermediate stable states instead of collapsing everything into
   “modern” and “1914” only;
5. validate every rollback against dated Dutch topographic or official source
   material.

This document is a research plan. It does **not** implement or modify Dutch
geometry.

## 2. Repository status before this report

Before this document was added, `work/gis` had no Netherlands-specific
historical-library source directory or Netherlands reclamation report. The
branch contains other GIS work, including German Empire / North Schleswig and
Siachen material, but no Netherlands rollback artifact was found.

Therefore the Netherlands reclamation reconstruction is considered **not yet
started** at geometry / recipe / generator level as of this report.

No country geometry, historical recipe, generated GeoJSON, world mesh, preview,
hydro dataset or runtime code is changed by this commit.

## 3. Website scale used for inclusion

Current `work/gis` runtime limits:

- flat map: `max = 64`
- globe: `max = 32`

Source:
`assets/js/modules/app-environment.js` → `ZOOM_LIMITS`.

At flat-map zoom 64, multi-kilometre coastline changes are clearly relevant.
The first-pass inclusion rule is therefore:

- include reclamation or closure that moves a coastline by roughly several
  kilometres;
- include land creation of several square kilometres or more;
- include smaller features when they create/remove an island, reconnect a sea
  inlet, change a major water body, or materially alter an international-border
  sector;
- defer small quay fills, local harbour walls, beach nourishment and
  sub-kilometre shoreline correction unless later comparison shows a visible
  mismatch.

This is a visual/project threshold, not a historical definition of
“reclamation”.

## 4. Critical modelling rule: separate events by physical effect

Do not store only one generic “reclamation date”.

Different events can have different geometry consequences:

- **dike closure**: changes water connectivity before the enclosed land is dry;
- **drainage / polder dry date**: changes water to land;
- **harbour fill**: pushes the open-sea coastline outward;
- **artificial-island construction**: creates new isolated polygons first,
  sometimes later connected;
- **inlet closure**: can change a tidal sea arm into a lake without creating
  the full final land polygon immediately.

Example:

- Wieringermeer was dry in 1930.
- Afsluitdijk closed the Zuiderzee in 1932.

So a 1931 state must be able to show **Wieringermeer as land while the
Zuiderzee is still connected to the Wadden Sea**.

The same principle applies to Flevoland and the later Houtribdijk.

## 5. Reverse-chronological first-pass change log

The dates below are rollback checkpoints / investigation anchors. Exact
construction phase boundaries must be refined during geometry work.

| Roll back before | Feature / area | Required reverse change | Priority |
| --- | --- | --- | --- |
| 2024 | IJburg Buiteneiland works | Remove any new fill already present in canonical geometry; verify first because very recent features may not exist in the source dataset | C / verify |
| 2021–2023 | Marker Wadden later islands | Remove later artificial islands if present | C |
| 2018 | IJburg Strandeiland | Restore IJmeer water over post-2018 artificial land | B |
| 2016 | Marker Wadden first construction | Remove the modern artificial archipelago if represented | B |
| 2013 | Maasvlakte 2 | Move Rotterdam outer coastline substantially eastward; remove second Maasvlakte reclamation | **A** |
| 1999 | IJburg first-phase reclamation | Restore IJmeer around Haveneiland / Steigereiland / Rieteilanden where applicable | B |
| 1979 | Polder Breebaart / Dollard sector | Restore pre-polder Dollard shoreline; border-adjacent sector requires careful topology review | B |
| 1975 | Houtribdijk | Reconnect what is now Markermeer and IJsselmeer; remove the dike as a separating land feature | **A** |
| early 1970s | Eemshaven | Reduce modern Groningen port protrusion / fill to its pre-port coast | B |
| c. 1973 | Maasvlakte 1 | Remove first Maasvlakte land and restore the older Rotterdam/North Sea coast | **A** |
| 1969 | Lauwerszee closure | Restore Lauwerszee as a tidal sea inlet connected to the Wadden Sea | **A** |
| 1968 | Zuidelijk Flevoland | Return Almere / Zeewolde area to water | **A** |
| late 1950s–1960s | Europoort expansion | Roll Rotterdam harbour coastline eastward in phases instead of one synthetic jump | **A** |
| 1957 | Oostelijk Flevoland | Return Lelystad / Dronten area to water | **A** |
| 1950s | Botlek expansion | Restore earlier harbour/water geometry where the current coast contains later fills | B |
| 1952 | Braakman closure / reclamation | Restore the Braakman sea inlet in Zeeuws-Vlaanderen | **A** |
| 1942 | Noordoostpolder | Return the polder to water and restore Urk as an island | **A** |
| 1932 | Afsluitdijk | Reopen Zuiderzee to the Wadden Sea; IJsselmeer as a closed lake no longer exists | **A** |
| 1930 | Wieringermeer | Return Wieringermeer to Zuiderzee water | **A** |
| 1924 | Amsteldiepdijk | Restore Wieringen as an island by removing the mainland connection | **A** |
| 1924 | Carel Coenraadpolder | Restore part of the Dollard shoreline predating this reclamation | B |
| 1914 target | final target state | Continue detailed rollback only after all post-1914 visible changes are accounted for | target |

### Notes on the table

1. “Roll back before” is not automatically the only valid change date.
   Construction start, closure, drainage and completion can differ.
2. Rotterdam must be treated as a **multi-stage coast reconstruction**, not a
   single Maasvlakte toggle.
3. Flevoland / Zuiderzee must retain intermediate hydrographic states.
4. Zeeland requires a second detailed pass because the Delta Works and local
   polder history changed channels and island connectivity as well as land area.
5. Dollard / Ems-adjacent changes need special handling because coastline
   reconstruction and the historical Netherlands–Germany frontier problem are
   related but not identical.

## 6. Large areas that must remain land in 1914

Do not remove every visually obvious Dutch polder when approaching 1914.
Several famous reclamations predate the target year and must remain.

Examples to preserve unless a dated source proves otherwise:

- **Haarlemmermeer** — drained in the nineteenth century (1852);
- **IJpolders around Amsterdam / the IJ** — major reclamation already present
  by the late nineteenth century;
- other pre-1914 polders already shown as land on contemporary Dutch
  topographic sheets.

This is a major false-positive risk if the process is implemented as
“subtract all reclamation”.

## 7. Geographic work packages

### A. Zuiderzee / IJsselmeer / Flevoland

Highest-priority package because it dominates the national silhouette.

Required stable checkpoints at minimum:

- current
- pre-Houtribdijk (before 1975)
- pre-Zuidelijk Flevoland (before 1968)
- pre-Oostelijk Flevoland (before 1957)
- pre-Noordoostpolder (before 1942)
- pre-Afsluitdijk (before 1932)
- pre-Wieringermeer (before 1930)
- pre-Amsteldiepdijk (before 1924)
- 1914

This package must distinguish coastline geometry from water-body connectivity.

### B. Rotterdam / Nieuwe Waterweg / port expansion

Rollback order:

1. Maasvlakte 2
2. Maasvlakte 1
3. Europoort
4. Botlek and other post-war harbour fill
5. retain Nieuwe Waterweg where appropriate because it predates 1914

Do not simply replace the present Rotterdam coast with one 1914 line in the
first implementation pass. Each large post-war expansion should be reversible
and attributable.

### C. Wadden / Groningen / Friesland

Key items:

- Lauwerszee closure
- Eemshaven
- Dollard polders including Breebaart and Carel Coenraad
- Wieringen mainland connection
- smaller coastal polders only if visible at max zoom or needed for shared
  topology

### D. Zeeland / southwest delta

Key first-pass item:

- Braakman

Second pass:

- large Delta Works closures and channel/topology changes;
- Sloe and other major industrial reclamation;
- remaining visibly significant island/coastline changes.

This region should be validated against dated topographic maps because a simple
land-area subtraction can produce the wrong channel network.

### E. Amsterdam / IJmeer / Markermeer

Key modern changes:

- IJburg phases
- Marker Wadden

These are smaller than Flevoland but can still be visible at flat zoom 64,
especially as isolated artificial islands.

## 8. Source hierarchy

Use sources in this order where possible.

### Tier 1 — dated Dutch topographic mapping

**Kadaster / Topotijdreis**

Primary visual control for exact historical coastline and water/land state.
Compare adjacent years, not only the nominal target year, because map edition
and survey/update timing can lag the displayed year.

- https://www.topotijdreis.nl/
- https://www.kadaster.nl/producten/kaarten-en-luchtfoto-s/topotijdreis

### Tier 1 — official engineering / GIS datasets

**Rijkswaterstaat** and Dutch government open data.

Useful for exact Zuiderzee Works polygons/dikes, closure history and major
water-engineering chronology.

- Zuiderzee Works open data:
  https://data.overheid.nl/dataset/17832-zuiderzeewerken-polders
- Rijkswaterstaat:
  https://www.rijkswaterstaat.nl/

### Tier 1 / 2 — authoritative project owners

**Port of Rotterdam**

Use for Maasvlakte / Europoort chronology and project extents.

- https://www.portofrotterdam.com/

**Municipality of Amsterdam**

Use for IJburg phase dates and island construction history.

- https://www.amsterdam.nl/projecten/ijburg/

### Tier 2 — heritage / conservation bodies and provincial material

Use for Marker Wadden, Dollard polder history and Zeeland-specific chronology
where a national engineering dataset does not directly provide the needed
historical outline.

Examples:

- https://www.natuurmonumenten.nl/projecten/marker-wadden
- provincial / regional heritage publications for Groningen and Zeeland

### Tier 3 — secondary reconstruction sources

Use only to locate candidate dates or map sheets. Do not promote geometry to a
final historical object solely from an unsourced secondary web map when an
official map or GIS layer is available.

## 9. Geometry workflow

For each rollback checkpoint:

1. **Inspect actual canonical NLD geometry first through the bounded world-data inspector.**
   Do not open or parse the monolithic `current-world.geojson`, PCG, or render mesh
   for this task. Use `tools/inspect-world-data.mjs`, which resolves NLD through
   `assets/data/territorial-entities/generated/v2/index.json`, reads only the
   matching compressed country chunk, verifies its SHA-256, and returns only the
   requested summary/polygon/boundary extract.
2. Narrow the query to the event area whenever practical:
   - whole-country metadata: `node tools/inspect-world-data.mjs --country NLD --mode summary`
   - candidate land polygons: `node tools/inspect-world-data.mjs --country NLD --bbox W,S,E,N --mode polygon --out /tmp/nld-area.geojson`
   - local boundary segments: `node tools/inspect-world-data.mjs --country NLD --bbox W,S,E,N --mode boundary --out /tmp/nld-boundary.geojson`
   - neighboring country geometry is queried separately only when coast-vs-land-border
     classification requires it.
3. Locate the relevant historical/engineering source.
4. Record:
   - event ID;
   - event type (closure, drainage, reclamation, island, port fill);
   - effective date or date interval;
   - source;
   - source CRS / scale where applicable;
   - confidence;
   - affected bounding box.
5. Build a candidate rollback polygon/line.
6. Apply only the local geometry change.
7. Validate:
   - polygon validity;
   - no accidental holes/slivers;
   - expected land-area delta;
   - coast continuity;
   - shared-border continuity where relevant;
   - water connectivity for closures/dikes;
   - dateline is irrelevant here but standard geometry checks still apply.
8. Compare visually at the website's maximum flat zoom.
9. Save diagnostics before promoting a checkpoint.
10. Only after the modern-to-1914 rollback chain is complete should the final
   1914 Netherlands geometry be treated as a stable historical-library source.

## 10. Proposed checkpoint series

First-pass stable series:

```text
current
→ 2023/2024 modern-artificial-island cleanup where represented
→ 2018
→ 2013
→ 1999
→ 1979
→ 1975
→ 1973
→ 1969
→ 1968
→ 1957
→ 1952
→ 1942
→ 1932
→ 1930
→ 1924
→ 1914
```

This list is intentionally not “one file per year”. It is a list of dates where
the visible large-scale geometry or water topology changes enough to justify a
separate stable historical state.

## 11. Recommended implementation order

### Phase 0 — inventory only

Before editing geometry, compare the current canonical NLD feature against the
change list and mark every item as:

- present in current canonical geometry;
- absent from current canonical geometry;
- uncertain / requires zoomed inspection.

Use the existing bounded lookup path rather than loading the full world dataset:

1. `--country NLD --mode summary` for the canonical entity/version and overall
   geometry bounds/statistics.
2. `--country NLD --bbox ... --mode polygon` for each reclamation footprint.
   Polygon mode returns whole source polygon components whose envelopes intersect
   the BBOX; it does **not** clip or synthesize a new polygon.
3. `--country NLD --bbox ... --mode boundary` when only the local shoreline/
   border run is needed. Synthetic BBOX clip endpoints are explicitly flagged.
4. Query `DEU` or `BEL` separately only where a boundary segment must be
   classified against a neighboring state's polygon.
5. Use `--mode lakes` / `--mode rivers` with a tight BBOX only when the
   water dataset is needed. These modes are independent from the country chunk
   lookup.

This avoids attempting to subtract features that the bundled Natural Earth
version never contained and avoids repeatedly opening the global country
dataset merely to inspect one Dutch sector.

### Phase 1 — current → 1999

Work through the modern artificial-island / harbour additions:

- Buiteneiland if present
- Marker Wadden if present
- Strandeiland / IJburg
- Maasvlakte 2

This is the safest way to establish the rollback mechanism on well-documented
modern changes.

### Phase 2 — 1979 → 1952

- Dollard / Breebaart
- Houtribdijk
- Eemshaven
- Maasvlakte 1 / Europoort / Botlek
- Lauwerszee
- Zuidelijk and Oostelijk Flevoland
- Braakman

### Phase 3 — 1942 → 1924

- Noordoostpolder
- Afsluitdijk
- Wieringermeer
- Amsteldiepdijk
- Carel Coenraadpolder

### Phase 4 — 1924 → 1914 detailed historical cross-check

Use Topotijdreis and other dated mapping to identify remaining visible
1914–1924 differences, especially Zeeland, Wadden/Dollard and local sea-inlet
geometry.

## 12. Relationship to the German Empire 1914 work

The Netherlands rollback is useful independently, but the Dollard / Ems sector
also affects the western end of the German Empire North Sea work.

Keep these concerns separate:

- Dutch reclamation/coastline history determines **where land and water were**;
- the historical Netherlands–Germany boundary question determines **which
  state owned / claimed which side**.

Do not infer the international boundary solely from a reconstructed coastline,
and do not force the Dutch coast to match a modern political boundary.

## 13. Completion criteria for the research stage

Research may be promoted to implementation when:

- every A-priority item has a primary/official source;
- the current canonical NLD geometry has been checked for each modern feature;
- every checkpoint has an explicit event date or date interval;
- closure and drainage dates are not conflated;
- pre-1914 polders that must remain land are documented;
- Rotterdam, Flevoland/Zuiderzee, Lauwerszee and Braakman have dated map
  controls;
- Dollard changes are separated from the unresolved/independent
  international-boundary question.

## 14. Current next action

**Do not jump to 1914 geometry.**

Phase 0 has started, so the next task is to **finish the event-footprint
inventory using the bounded country lookup**, not to re-read the monolithic
world dataset and not yet to create rollback geometry.

For every event in section 5:

> query only the NLD chunk and event BBOX, classify the relevant canonical
> land/boundary geometry as present / absent / uncertain, and attach the result
> to the Phase 0 audit.

Point-in-country probes already recorded below are only supporting evidence.
They do not replace footprint/boundary inspection, especially for enclosed
water, artificial islands and dike connectivity.

Only after that inventory is complete should the first rollback geometry be
created.

## 15. Phase 0 started — 2026-10-09 (country + lake-source inventory)

[Phase 0 canonical sample audit](phase0-canonical-audit.md) and [machine-readable probe locations](canonical-phase0-probes.json) have now been added. Against the **actual** `state:NLD:natural-earth-5.1.1` national geometry, 12 point samples were tested: 11 inside, the approximate Maasvlakte 2 place point outside. This result is **not** the required full present/absent/uncertain inventory of each event footprint. The NLD polygon has no interior water holes, so inner water presence and artificial islands cannot be classified from point-in-country.

The corresponding [nine-event evidence index](change-events.json) provides independent construction, drainage and inlet-closure chronology with no digitized shoreline. A second pass is now recorded in [phase0-current-representation-inventory.json](phase0-current-representation-inventory.json): large Flevoland/Wieringermeer samples are consistent with modern dry land, the current lake source contains no separate Markermeer feature and generalizes across sampled Houtribdijk positions, Marker Wadden/IJburg samples remain inside the water mask, and the published Maasvlakte 2 point is outside the current NLD country outline. Phase 0 must continue with dated modern coastal GIS/actual map rendering, and first rollback geometry must not start until the modern baseline and water masks are verified. The earlier Phase 0/1–4 sequence remains unchanged.


## 16. Bounded country-data lookup contract — 2026-10-09

The web repository now has a read-only lookup path specifically suited to this
survey:

- CLI: `tools/inspect-world-data.mjs`
- documentation: `docs/world-data-inspection.md`
- country/entity index:
  `assets/data/territorial-entities/generated/v2/index.json`
- selected country payload:
  the indexed `territorial-entities/generated/v2/*.json.gz` chunk only

The inspector:

1. resolves an ISO3 such as `NLD` to its current territorial entity;
2. prefilters by indexed BBOX where applicable;
3. reads only the selected compressed country chunk;
4. verifies the chunk SHA-256 against the index;
5. selects the documented geometry version;
6. returns summary, whole polygon components, or BBOX-limited boundary
   segments;
7. never needs the combined `current-world.geojson`, PCG, or mesh for a
   country-local inspection.

Recommended Netherlands queries:

```sh
# Identify current NLD geometry and source/version metadata.
node tools/inspect-world-data.mjs --country NLD --mode summary

# Extract only Dutch polygon components relevant to a study area.
node tools/inspect-world-data.mjs \
  --country NLD \
  --bbox 3,50,8,54 \
  --mode polygon \
  --out /tmp/nld-study-area.geojson

# Extract only local Dutch boundary runs for a tighter event footprint.
node tools/inspect-world-data.mjs \
  --country NLD \
  --bbox 4,51,5,52 \
  --mode boundary \
  --out /tmp/nld-event-boundary.geojson

# Discover current indexed entities in a local area without decoding every
# country chunk.
node tools/inspect-world-data.mjs --mode list --bbox 3,50,9,55
```

Important limitations:

- `boundary` contains both coast and land borders; use neighboring polygons or
  other geographic evidence to classify them.
- `polygon` is an envelope-filtered source-component return, not an exact BBOX
  polygon intersection.
- a `--date` query can only select a geometry version already recorded for the
  entity; the inspector does not manufacture a historical shoreline.
- hydro modes currently read their requested base source only when invoked, so
  keep hydro BBOX queries regional.
- this lookup layer is for bounded inspection and evidence extraction; it is
  not itself the Netherlands rollback generator.

For this project, the practical consequence is that Phase 0 and later local
coast QA should request **NLD first, plus only the neighboring country/water
data actually needed for that event**. Full-world extraction is no longer the
normal inspection path.
