# M6 history, temporal and GIS implementation plan

> M5 remaining acceptance gates remain in `docs/m5-closure-audit.md`. This
> branch starts from the M5 implementation tree; neither M5 nor M6 is merged
> into `main` by this plan.

## Goal and fixed inputs

Implement the current web contract for date precision, a read-only historical
catalog and transactional additions, then GeoJSON/ZIP/GeoPackage import and
export. Avoid a global timeline, historical boundary inference and unapproved
territorial changes.

- PandoEditor/main: `168fb7cf7ecde29544e9f65e10fca6a2530d1de0`.
- M5 implementation branch remote head at M6 start:
  `620fd372b71d29ac4c110ce845232a92d797eebb`.
- world-map/main: `c0bd31d13dc8495593d78cf51f7cc195de7c9469`.
- Historical pilot blob: `94ef368befa665b4ed61892622a71ffa6520457f`;
  schema 2, read-only, never embedded wholesale in a project.
- `temporal.js`: `31faf94894e302b3eadf61d79a32efd0490affe3`.
- Historical source blobs: library `dce9ebb1e0326e1156dffe5282b01208b6b9dd01`,
  service `d71a90aae1ac4ecc8b9630316340267f6fd92294`,
  controller `1060331d23f96e9ee4fedab6143c6fbe61a0abc3`.
- GIS source blobs: import plan `f99e9031adfcd33b52249990417c2ab6a79bc5b3`,
  transaction `78a80e50d52c9d1d510c71f4f1ff48ca3c942104`,
  file controller `5222a552fbb800a8a42bcaeeb119d128a73cc9b6`,
  export controller `871293159a55d13f88f14813f8b706765d6368f9`,
  registry `634fab3cfa6e19d5dd4488eb330c9add02df7237`, adapter
  `assets/js/gis-adapters.js` blob `be606ee43b39b1b1ddc15dc166908f966217f960`.
- Native codec now reads v1–v7 and writes v7 for typed historical origins;
  prior readers and migrations remain covered by roundtrip tests.

## Architecture and file boundaries

- `core/include/pandoeditor/temporal.h`, `core/src/temporal.cpp`: one
  parser, keys, comparison, interval and containment API. `document.cpp`
  delegates its existing integer-key and relation validation to it.
- `core/include/pandoeditor/historicallibrary.h`,
  `core/src/historicallibrary.cpp`: immutable schema-2 entities, versions,
  snapshots, search and selection. Geometry materialization calls the M4
  kernel; catalog data is never stored in `ProjectDocument`.
- `app/historicallibrary*.cpp`: file loading, plan/preview and Controller
  integration. Typed `LibraryOrigin` lives in the canonical unit and codec,
  with materialized geometry. One confirmed command commits and undoes all
  affected objects and country updates.
- `ui/common/HistoricalLibrary*.qml`: shared controller for desktop and
  360 px mobile with explicit ownership choices and session tokens.
- `core/include/pandoeditor/gis*.h`, `core/src/gis*.cpp`: GeoJSON,
  GeoPackage geometry and immutable import/export DTOs. The format gate chooses
  SQLite3 and a ZIP library using pinned web samples before product linkage.
- `app/gis*.cpp`, `ui/common/Gis*.qml`: file boundary, plan/confirm,
  cancellation and same controller path on PC/mobile. GIS exchange and project
  GeoPackage are separate entry points.

## Delivery slices (test first for each behavior)

1. **M6.0 Oracle and source gate.** Pin original blobs and minimal fixtures,
   add a Node Oracle calling the actual `temporal.js` exports. Record the
   historical pilot shape and actual GIS adapter/format paths.
2. **M6.1 Temporal.** Failing C++ cases for BCE, extended years, year zero,
   leap dates, whitespace, year-width intervals and inclusive endpoints;
   implement the single core parser and six APIs. Compare a C++ probe with the
   pinned web Oracle. Move unit/relation/distribution validation to the common
   API and add `effectiveRelationAt(document,id,referenceDate)`; retain the
   old integer-key overload for compatibility. Test endpoint transition,
   overlap, cycle, sovereign mismatch and absence of a global timeline.
3. **M6.2 Catalog.** Parse schema 2 only, aliases and lossless metadata,
   geometry versions, reference-date selection, searches, snapshots and child
   expansion. Run an Oracle against web library functions. Guard missing
   versions, broken refs and source immutability.
4. **M6.3 Transactions.** Introduce a typed provenance field and bump the
   native writer after a migration/roundtrip test. Plan independent addition,
   ownership resolution, child depth and snapshot as immutable candidates.
   Territory replacement hands geometry changes to M4. Confirm once for one
   undo; test cancel, stale project/revision, missing catalog reopening, ID
   conflicts, countryUpdates and partial policies.
5. **M6.4 UI.** Shared QML search, preview, version override, ownership and
   impact/confirm. Verify 1100 px and 360 px command paths. Changing entity
   clears stale version choice; changing country resets the parent choice.
6. **M6.5 Format gate.** Pin actual web GeoJSON ZIP and project/GIS GeoPackage
   samples. Check external SQLite tables, WKB, CRS 4326 and ZIP central
   directory. Choose SQLite3 plus a portable ZIP implementation only after
   Windows/Android dependency checks; no GDAL by default.
7. **M6.6 Import.** Parse, map, validate geometry/CRS/IDs/relationships,
   prepare M4 geometry work, build a candidate, preview impact, then confirm
   one command. Cover project-replace, country-merge, territorial, generic and
   distribution with web target normalization. Cancel/corruption/stale and
   allocation failure leave the live project untouched.
8. **M6.7 Export.** Separate selected GIS layers (GeoJSON ZIP/GeoPackage)
   from project GeoPackage. Preserve territorial provenance and distribution
   references. Project roundtrip includes settings, assets and three flag
   policies; GIS-only output excludes project tables. Check SQLite schema,
   geometry headers and 4326 with independent readers.
9. **M6.8 Complete regression.** A fresh build directory, all M3–M5 and M6
   Oracles, `ctest --output-on-failure` with explicit skipped count, 1100/360
   offscreen UI, then visible Windows and physical Android if available.
   Record unavailable platforms honestly; do not merge while acceptance is open.

## Review focus

- Year-only endpoint versus full date and BCE transition must not skip a day.
- Historical source with incomplete ownership must not silently attach to a
  modern state or invent geometry.
- A plan from an old project/revision must not mutate a newly opened project.
- Imported CRS, invalid rings, duplicate IDs and broken ZIP/GPKG must fail
  without changing the document or selection.
- GIS-only export must not leak canonical project settings/assets; project
  GeoPackage must reopen embedded flags and provenance exactly.
