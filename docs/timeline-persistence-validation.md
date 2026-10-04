# T2-2 native persistence validation (2026-10-04)

Native base: `98801f39e13e01ae74564efdc166f7a69a6047a8`, tracking branch
`feat/timeline-app`. Web exchange source, owned and pushed by the other chat:
`788f43fd27062851bdadf7fa6964819d43534685`, `feat/timeline-web`.
Both remote heads were rechecked before publication. Existing integration
checkout and its ignored build/deployment artifacts are preserved. No merge,
deployment, T3/T4 resolver, dated editing or timeline UI is included.

Implementation commit and exact tested native source:
`d2af1d518aba3b55e7b7c09db049a94ca607497f`, pushed nonforce to the feature ref.
Final tested candidate, including the portable fixture path correction:
`286602b3757efccab6c8b198ea5e5dfdf3ab983a`. The subsequent report commit changes
documentation only and deliberately does not schedule another CI build.

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
| Desktop/360px canonical creation routes | 2 substantive Qt slots, 0 failure/skip |
| Selected CTest total after path fix | 10/10, 0 failure/skip, 12.51 seconds |
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

## Decision ledger, in execution order

| Decision | Reason and cost of the choice |
| --- | --- |
| Use the existing tracking feature worktrees; initial web checkout at an external ASCII path | Native worktree tooling targets the current repository. Original checkout stays intact; the separate path must remain available for exchange evidence. |
| Repair the task-local cached Ninja path | The cached executable no longer existed. This is environment setup, not a product baseline failure. |
| Split generated record-fixture construction into functions | MinGW could not compile the aggregate even serially. All 60 fixtures remain; generated source layout changes. |
| Use T2-2a UTF8 ID/version archive ordering | It is the existing canonical contract. Original input array order is not preserved; all version keys and values are. |
| Reuse validated equal immutable geometry during label-only history | This avoids invalidating unchanged shapes; the object must remain immutable. This early web decision is subsequently owned by the companion chat. |
| Transfer all web implementation/commit responsibility to the other chat | Explicit user assignment. Native exchange evidence depends on the pinned, separately owned web commit. |
| General administrative moves keep identity and metadata | Canonical parent records own placement. Previous sovereign-operation intents reject rather than infer political data. |
| Treat GeoPackage project JSON as authoritative | Actual Worker stores null sourceInfo as {} in its spool. Derived spatial state must agree; independent GIS edits require GIS import. |
| Preserve historical catalogKind as source metadata | Project kinds are General/Regional; original country/subunit classification stays inspectable. Unsupported political instantiation rejects. |
| Restore original tracked line endings and content-identical files | Keep the diff scoped. Pinned byte-sensitive source assets explicitly use LF. |
| Reject old development formats | User excludes migration and parallel readers. Old v7 saves are not recoverable through current v9 readers; files remain intact. |
| Retain M4 math kernels and pinned blob hashes | Administrative tests assert current identity/parent semantics. Pre-v9 political transition parity is not claimed. |
| Use task-local Debug -g1 on Windows | GNU debug archives/links stalled for minutes. Assertions stay enabled; symbols provide less detail. |
| Fix Windows URL.pathname and LF source-asset defects | Baseline Node/asset failures reproduced. Runtime paths and line-ending attributes change; data and pinned hash content do not. |
| Pin the current T0 source for M32 and normalize General history metadata | Existing oracle predated accepted month precision. All 2330 cases remain; stale pre-T0 date rejection is not retained. |
| Keep default Project as unloaded | Empty loaded tests use the existing ProjectDocument constructor. Callers must initialize a document before encoding it. |
| Move only this task build to D: through a junction | C: disk exhausted. Original artifacts remain; local build depends on D:. |
| Use task-local LLD 20.1.8 for Windows links | Measured GNU ld stalls. Compiler/assertions unchanged; separate Linux GNU evidence is required. |
| Use ReplaceGeometryIntent for geometry allocation stress | General promotion now changes only parent records. Allocation loops remain, but geometry work is tested through its actual owner command. |
| Compare opaque JSON semantically through losslessjson | Object key byte order is not guaranteed. Number tokens and array order remain exact. Matching optional flag spool rows are inserted before corruption tests, because the actual Worker fixture has none. |
| Accept numeric, vector-consistency and staged-sample review findings | RED cases reproduced before fixes; lossless native storage remains and unsafe/inconsistent exchange rejects. Exact tested sample/provenance is committed. |
| Follow the final user request for necessary checks only | Final ten CTests and real exchange run; the incomplete full regression is not a pass or final claim. |
| Change actual coordinates in shared-shape corruption fixtures | A/B/C/R share one immutable shape, so swapping rows did not change geometry. The replacement now changes valid ring/envelope coordinates. |
| Resolve test fixture parents through QFileInfo | Linux rejects a file.json/../child path that Windows normalizes. Only the fixture lookup changes; all production and test cases remain. |

Deferred Minor findings: none. Final review scope rulings retain T3/T4 UI exclusion,
current-only formats, missing-baseline delta rejection, separately owned web work
and unjudged unrelated performance/rendering behavior; the corresponding costs
and activation limits are stated above.

## Linux and activation limits

The first focused Linux run on d2af1d5 built successfully and passed 9/10
selected CTests. The new numeric fixture failed to open because its path used
file.json/../child; it returned 2 after all 31 storage cases. QFileInfo now
resolves the real parent directory before joining the child. This is a test
path defect, and that failed run is not a validation pass. Remaining CI steps
were unexecuted in that run.

Final [Linux CI run 37195223402](https://github.com/kimjeon-il/PandoEditor/actions/runs/37195223402)
on exact candidate `286602b3757efccab6c8b198ea5e5dfdf3ab983a` passed. Ubuntu
24.04, Qt 6.8.3, ordinary Debug/default GNU toolchain, offscreen/software QML:

| Linux focused check | Result |
| --- | --- |
| App plus selected targets configure/build | Pass |
| Same ten CTests | 10/10, 0 failure/skip, 5.85 seconds |
| Production storage/numeric suite inside CTest | 31 + 9, 0 failure/skip |
| Actual web/native file exchange at pinned web 788f43fd | 16/16, 0 failure/skip |
| Saved sample/source assets | 17/17, 0 failure |
| XML and Qt log skip audit | 0 failed/skipped CTest entries and no Qt SKIP |

Published XML, Qt LastTest.log, source-asset report and actual exchange artifacts
were downloaded and checked, rather than inferring pass from job scheduling.
Windows and Linux evidence is preserved under `D:/Codex/evidence/timeline-app-t22`.
No full CTest, full hydro fixture fetch or full regression audit was scheduled.

This work supports storage/exchange of rich timelines, but before T3/T4 only
one unbounded lifetime, binding and parent record per territorial object may
activate in existing editors. Qt/QML evidence is real offscreen interaction,
not a manual Windows desktop walkthrough. Full regression is intentionally
outside the final verification scope at the user's request.
