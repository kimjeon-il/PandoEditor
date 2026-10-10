# Historical library geometry builds

## Mandatory historical boundary reconstruction standard

Historical national/subnational borders, rivers, coastlines and reclamation
geometry follow the shared [historical boundary reconstruction policy](../../docs/historical-border-reconstruction-policy.md).
The standard uses suitable period maps (normally around 1:25,000), with a
maximum-zoom **2560 CSS px / 0.5 CSS px** visual stopping criterion, but never
allows subpixel tolerance to override source chronology, territory ownership,
shoreline changes or shared-border topology. Keep canonical evidence separate
from display-only simplified geometry.


`east-germany-1989.recipe.json` is the pinned, declarative build recipe for the
PandoLab historical-country pilot `state:deutsche-demokratische-republik`.

The normal build uses offline source data. Exact regeneration of the stored
coordinates is verified on Windows with the pinned Shapely/pyproj versions:

```powershell
python -m pip install -r tools/requirements-historical-library.txt
pnpm build:territorial-geometry
pnpm check:territorial-library
```

CI runs `build-territorial-geometry.py --check` in the required Windows
`historical-geometry` job. The Linux `data-contracts` job depends on that check
and retains preview regeneration, catalog chunk regeneration and catalog
validation. Coordinate projection can differ by one floating-point step between
Windows and Linux, crossing the existing 12-decimal rounding boundary. The
generator, stored coordinates and exact comparison remain unchanged.

The checked-in files below are the only geometry inputs used by that build:

- Natural Earth Admin 1 v5.1.1: `DE-BB`, `DE-BE`, `DE-MV`, `DE-SN`, `DE-ST`,
  and `DE-TH` only. Natural Earth data is public domain.
- BKG VG250: Berlin Land and Amt Neuhaus (`AGS 03355049`). Attribution:
  **© BKG 2026 dl-de/by-2-0**, Datenlizenz Deutschland – Namensnennung –
  Version 2.0.
- Berlin Open Data, *Verlauf der Berliner Mauer, 1989*: physical front-wall
  linework and political-boundary deviations. Datenlizenz Deutschland – Zero –
  Version 2.0. The source was manually transferred from the 25 April 1989 aerial
  image to a 1:5,000 map and is not parcel-accurate.
- PandoLab's canonical `DEU` polygon from Natural Earth Admin 0 v5.1.1, used to
  reuse the exact Baltic/coast/Poland/Czech exterior boundary.

`python tools/fetch-east-germany-sources.py` explicitly refreshes the source
subsets. It is not part of the normal build because BKG is updated annually.
After a deliberate refresh, review the geometry and update the recipe SHA-256
values before committing. CShapes is not an input and no CShapes-derived
coordinate is distributed.

The recipe performs union, difference and canonical-boundary reconciliation.
The Berlin mask is polygonized in EPSG:25833 and uses the smallest linework snap
that closes the West Berlin cell, never exceeding the recipe's 100 m limit. A
failure to close the boundary stops the build rather than interpolating a
replacement line.

## Additional historical-country entries

`assets/data/territorial-entities/source/` contains one file per modern or historical entity, including territory-replacement
entries built from the canonical Natural Earth Admin 0 polygons. They are
reference-date approximations, not a claim of cadastral historical precision:

- `state:ukraine`: current Ukraine plus the canonical Russian
  Crimea component, through 17 March 2014.
- `state:yugoslavia`: three geometry versions for the Kingdom of
  Yugoslavia (1918–1941), Socialist Federal Republic (1945–1992), and Federal
  Republic (1992–2003).
- `state:sudan`: present Sudan plus South Sudan through 8 July
  2011.
- `state:indonesia`: present Indonesia plus Timor-Leste through
  19 May 2002.

These entries use the same `territory-replacement` materialization mode, so adding one
subtracts its transferred geometry from overlapping current-country objects in
one undoable operation.

## German coastline changes across eras

The [coastline event/source index](sources/german-coastline/README.md) and
[`change-events.json`](sources/german-coastline/change-events.json) record
coastal engineering and land/water changes across the German Empire,
German Reich (1919–1945), and later periods **without assigning each change to
a permanent country owner or inventing a precise completion date**.

The index currently records historical sources, construction/closure year
ranges and the available Pillau/Zimmerbude/Gdynia map-sheet series. It is
**research evidence, not digitized shoreline geometry**. Do not promote an
event into a dated polygon until independent period maps have established its
location, geometry, actual land/water classification and applicable dates.
The present German Empire 1914 working geometry still has provisional modern
coastline sections.

Validate its data contract with
`node --test tests/unit/historical-german-coastline-events.test.mjs`.

## German Empire 1914 working base

`german-empire-1914-base.recipe.json` defines the reproducible working-base
pipeline for the German Empire immediately before the First World War
(reference date 1914-07-31). Unlike the distributable East Germany pilot, the
HGIS source and its generated derivative are kept local because the HGIS
metadata restricts use to non-commercial academic research unless separately
licensed.

Prepare the source and build the working base with:

```powershell
python tools/fetch_german_empire_1914_source.py
python tools/build_german_empire_1914_base.py
python tools/build_german_empire_1914_base.py --check
```

If Harvard/NYU WFS access is unavailable, download **Germany State Boundaries,
1914, German Historical GIS** manually, convert it to EPSG:4326 GeoJSON, then:

```powershell
python tools/fetch_german_empire_1914_source.py --from-file PATH_TO_GEOJSON
```

The build performs these steps deterministically:

1. Validate the local HGIS source as the 26-state 1914 dataset and check its
   envelope against the catalog metadata.
2. Apply the OSHistory linear HGIS correction with the published west-east
   coefficients on longitude and south-north coefficients on latitude.
   This is an intentional axis-correct port: upstream `transform-coords.py`
   passes the longitude coefficients to both coordinate axes despite shipping
   separate WE and SN coefficient files.
3. Dissolve all corrected state polygons into one German Empire base.
4. Clear the Heligoland work window and insert the exact Heligoland + Düne
   components from PandoLab's canonical Natural Earth v5.1.1 `DEU` geometry.
5. Validate geometry, area, historical inside/outside control points, and the
   isolation of the Heligoland patch.

The generated base and diagnostics are written below
`tools/historical-library/generated/` and are gitignored. The checked-in
`tools/historical-library/working/` artifacts preserve the reviewed 1914
working geometry and modern-coastline variants used by the GIS reconstruction
workflow.

The HGIS source contract and local-file policy are documented in
`sources/german-empire-1914/README.md`.

## Worldwide event-based shoreline pilot (October 2026)

The [global historical coastline change workflow](../../docs/historical-coastline-change-workflow.md) reuses the German coastline event catalogue instead of duplicating it by sovereign or year. Its [read-only catalogue registry](sources/coastline-catalogs.json) references the existing German and new Dutch inventories without duplicating event records. The Netherlands pilot adds a [nine-event source index](sources/netherlands/change-events.json), [twelve read-only national-polygon point probes](sources/netherlands/canonical-phase0-probes.json), and a [Phase 0 audit](sources/netherlands/phase0-canonical-audit.md).

The Dutch sample audit established that the current NLD national polygon covers inland-water positions and does not prove whether small artificial islands/lake masks are represented. It also places the published Maasvlakte 2 sample point outside the present national outline. All historical geometry, hydrology and contemporary canonical files remain unchanged; the probes are **not** a verified shoreline or polygon rollback. Run `node --test tests/unit/historical-netherlands-coast-phase0.test.mjs` after syncing the GIS branch to a local worktree to reproduce the regression assertions.
