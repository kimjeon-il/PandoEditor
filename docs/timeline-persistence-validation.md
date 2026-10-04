# T2-2 native persistence validation (2026-10-04)

Native base: `98801f39e13e01ae74564efdc166f7a69a6047a8`, tracking branch
`feat/timeline-app`. Web exchange source, owned and pushed by the other chat:
`788f43fd27062851bdadf7fa6964819d43534685`, `feat/timeline-web`.
Both remote heads were rechecked before publication. Existing integration
checkout and its ignored build/deployment artifacts are preserved. No merge,
deployment, T3/T4 resolver, dated editing or timeline UI is included.

The user narrowed final verification to necessary checks. The final candidate
uses the ten focused tests listed below and actual production exchange; this
report does not claim a completed full regression run.

## Connected production paths

- `ProjectDocument` owns General/Regional identity and metadata; its existing
  `TimelineRecords` owns lifetimes, geometry bindings, administrative parents
  and coverage. Existing immutable `GeometryStore` owns all saved versions.
- `geometry-types.h` separates shared GeometryRef/Geometry/Validity/store types
  so the document and timeline record headers do not include each other.
- Native v9 QFile/QSaveFile, full autosave and snapshots use
  `projectcodec::encode/decode` and `losslessjson`. Required timelineRecords
  stays schema 1. Required geometries contains shared/unreferenced versions.
- `webimport::prepare` uses `projectcodec::decodeWeb`; the same codec boundary
  adds `encodeWeb(ProjectSnapshot)`. Web v9 territorial Feature geometry is
  null and contains identity/metadata, rather than another shape/parent owner.
- Native and web GeoPackage use full JSON project state. Static vector columns,
  IDs, geometry and properties must agree, including nonterritorial rows.
  Complex projects contain no derived static vectors. Unsupported fields,
  tables and numeric values are explicit errors, rather than discarded values.
- EditorController validates detached candidates and static activation before
  publishing, resetting renderer/session, selection, history, dirty or save
  target. Existing commands update canonical records; history restores records
  and immutable shapes together. Worker snapshots are immutable. Cursor changes
  do not change content revision, dirty state or Undo history.
- Existing web full/delta recovery includes complete records/archive. Matching
  baseline fingerprint is mandatory. Native import of a raw web delta without
  baseline returns BASE_DATA_REQUIRED; recovered full state is exchangeable.

## Final Windows checks

Qt 6.8.3, GCC 13.1, Debug assertions, offscreen/software Qt Quick. The local
task build uses `-g1` and LLD 20.1.8 after measured GNU archive/link stalls;
Linux keeps ordinary Debug flags and its default GNU toolchain.

| Check | Result |
| --- | --- |
| App plus selected targets configure/build | Pass |
| temporal_tests | Pass |
| timeline_records_tests | Pass (all 60 generated record fixtures retained) |
| timeline_storage_tests | Pass |
| command_tests | Pass |
| storage_tests | Pass |
| timeline_project_tests | 31 production file cases + 9 numeric cases, 0 failure/skip |
| timeline_editor_persistence_tests | 5 substantive Qt slots (7 totals), 0 failure/skip |
| project_geopackage_tests | Pass, including 10 vector/state corruption cases |
| project_geopackage_controller_tests | Pass |
| web_import_ui_tests | Pass, real QML/Qt import/file paths |
| Selected CTest total | 10/10, 0 failure/skip, 13.18 seconds |
| Actual web owner/serializer → native QFile → native web codec → web owner | 16/16, 0 failure/skip |
| Saved sample/source asset consistency | 17/17 |

The storage/file cases cover v1 through June 1914 and v2 from July, independently
changing parents, disjoint lifetimes, year/month/day precision, BCE/CE, holes,
islands/dateline coordinates, shared/unreferenced historical geometry, duplicate
IDs/versions, missing references, overlap/day gaps and unknown fields. Atomic
owner tests retain selection/history/future/dirty/save target on failure, exercise
real QFile/QSaveFile and native/web GeoPackage, autosave, immutable Worker input,
static geometry/parent Undo/Redo, and cursor movement.

Exchange invokes the real web project owner/serializer at the pinned SHA and
native production QFile codec, rather than a parallel helper reader. It verifies
static and complex files, label/generic/distribution/source/flag/capital content,
full autosave, matching delta recovery, mismatched/missing baseline rejection,
invalid flag/UUID rejection, and six actual precision-loss rejections before
output publication. Complex files round-trip; editor activation rejects them.
The web owner's separate report records 666 unit/Worker, 3 Python and 6 browser
checks, zero failure/skip. Those are the other chat's evidence, not reruns here.

## Baseline and earlier checks

Before implementation, six focused temporal/records/storage/history/autosave
checks passed. The exact-base historical library/instantiation supplement also
passed 2/2, but ran after implementation in an isolated archive of 98801f39.
Generated fixture aggregation originally failed to compile on MinGW; splitting
the 60 generated functions preserved every fixture. Existing Windows Node corpus
tests had URL.pathname and CRLF hash failures: reproduced 16/25 plus two asset
hash failures, fixed fileURLToPath/LF attributes, then 25/25 and 17/17 passed.
M32 preserved all 2330 comparisons and 30 pinned blob identities and passed.

Earlier full Debug build passed. The first full CTest run failed 12/118; stale
pre-v9 test/QML contracts, metadata history normalization and source catalog
labels were corrected, preserving the test slots and allocation stress loops.
The later full run was stopped for final review fixes and the user's focused
verification instruction. It is incomplete and is not reported as a pass.
Rendering/performance and full hydro regression are not final validation claims.

## Review rulings and costs

One final reviewer reported no Critical findings and three Important findings.
All three were accepted in one fix pass; there were no Minor findings:

1. Native numeric metadata/source precision could be lost in JavaScript. Six
   failing production export cases were reproduced. Recursive finite/exact
   decimal validation now rejects unsafe web values while native QFile storage
   remains lossless; three exact representable positives also pass. Cost:
   unsupported numeric values require correction before web exchange.
2. GeoPackage derived vectors could be changed/dropped while JSON succeeded.
   Deleted places/generic/distribution rows reproduced acceptance. The final
   tests reject those, valid but changed coordinates, and extra columns for both
   web and native packages. Expected native rows derive from the existing GIS
   writer, not another writer. Cost: inconsistent externally edited project
   packages reject; use the separate GIS import path for such edits. Surrogate
   SQLite fid values are not logical identity. JSON key ordering is semantic;
   numeric tokens and array ordering remain exact in property comparisons.
3. The staged sample was still pre-v9 although working tests used v9. The exact
   tested sample and updated provenance are staged together and hash verified.

Other scope rulings: General administrative moves preserve logical identity and
metadata and change parent records; sovereign intents explicitly reject. Historical
catalogKind preserves country/subunit source classification without keeping
those as project kinds. No political sovereignty is inferred. Old development
formats reject; no alias/migration reader is added. Full native autosave and
fingerprinted web delta recovery reuse existing boundaries. Rich project UI and
baseline-free delta activation remain unsupported pending their prerequisites.
Unrelated performance/rendering concerns were not judged as fixed.

Only this task build moved to D: after C: disk exhaustion; original artifacts
remain intact. Local LLD package is LLVM mingw 20250709, SHA-256
`82babcd6aae4dc3606e8e0471d816989c384b4bf86a139f184a8b4a1b2c2758d`.
Cost: Windows debug symbols/linker differ from ordinary Linux Debug provenance.

## Changed files and removed code

The commit diff is the complete file list. Main changes are document/project/
commands and metadata ownership in core; native/web codecs, GeoPackage,
controller/import/autosave/history and historical factories in app; derived
shape reads in engine/renderer; canonical kind calls in desktop/common QML;
v9 sample/provenance and reproducible fixtures; preserved regression callers;
new production owner/file tests, exchange oracle, and focused Linux CI.

Removed `app/webmigration.cpp` and `app/presentationmigration.h`: no surviving
callers after current v9 readers replaced obsolete development-format handling.
No substitute engine, compatibility wrapper, test deletion or skip was added.
Existing MapProjection, CPU/GPU Qt backends, adapters and geometry kernels remain.

## Linux and activation limits

The feature push starts `.github/workflows/timeline-persistence.yml`: Ubuntu
24.04, Qt 6.8.3, ordinary Debug, the same ten selected CTests, sample assets and
the same actual 16-case exchange at pinned web 788f43fd. Its result must be read
from the published run; scheduling/building is not a passing result. No full
CTest, full hydro fixture fetch or full regression audit is scheduled.

This work supports storage/exchange of rich timelines, but before T3/T4 only
one unbounded lifetime, binding and parent record per territorial object may
activate in existing editors. Qt/QML evidence is real offscreen interaction,
not a manual Windows desktop walkthrough. Full regression is intentionally
outside the final verification scope at the user's request.
