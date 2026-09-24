# M5 closure audit

Status: **open**. This is an evidence ledger for `codex/m5-completion`, not a
claim that M5 can be merged. The web baseline is `world-map` `c0bd31d1`; the
editor base was `168fb7cf` (the brief's `17bb218f` was older). The native codec
reads v1–v6 and writes v6. A native v7 writer is not present.

The labels below mean: **Implemented** = code exists but an acceptance gate is
outstanding; **Verified** = a named executable test/Oracle passed at the stated
revision; **Unsupported by web** = deliberately outside the web contract;
**Deferred explicitly** = a named follow-up stage or platform gate. A failed or
skipped test never counts as Verified. The definitive fresh CI run and its skips
must be recorded below before closing this ledger.

| M5 requirement | Classification | Evidence / remaining gate |
| --- | --- | --- |
| Local 0.13.1 manifest, sibling relative URLs, length/SHA, gzip and local-only asset paths | Implemented | `app/hydromanifest.cpp`, `app/hydroassetreader.cpp`, `tests/hydro_manifest_tests.cpp`; latest fresh full CI pending. |
| Global index, core metadata and lazy detail | Implemented | `core/src/hydroformat.cpp`, `app/hydrometadata.cpp`, `tests/hydro_format_tests.cpp`; actual full dataset integration pending. |
| Shard ranges, pack v4, delta geometry, widths and lake holes | Implemented | `app/hydroshardreader.cpp`, `core/src/hydroformat.cpp`, `tools/m5-hydro-oracle.mjs`, `tests/map_render_tests.cpp`; actual full dataset integration pending. |
| Stage thresholds, viewport selection and stale project/viewport work | Implemented | `core/src/hydroviewport.cpp`, `app/hydroloadscheduler.cpp`, `tests/hydro_runtime_tests.cpp`, `tools/m5-hydro-viewport-oracle.mjs`; latest CI pending. |
| Read-only runtime, 96/48 MiB pack cache and unchanged document | Implemented | `app/hydroruntimeprovider.cpp`, `app/hydroruntimecache.cpp`, `tests/hydro_cache_tests.cpp`; frame allocation is outside the cache byte counter. |
| Rivers and lakes, opacity, width, holes, selection and visibility pixels | Implemented | `renderer/maprenderitem.cpp`, `tests/map_render_tests.cpp`; opacity regressions are under fresh CI verification. |
| Built-in pick, logical identity, metadata search and focus | Implemented | `app/editorpicking.cpp`, `app/editorselection.cpp`, `tests/presentation_editor_tests.cpp`; hidden search is returned with `visible=false`. |
| Multi-pack copy-on-edit, hide, one Undo/Redo and save/reopen | Verified | `app/editorcontent.cpp`, `tests/presentation_editor_tests.cpp`; fragmented-copy roundtrip passed in run `36021994255` (overall suite still had two unrelated failures). |
| Central draw order and separate chooser order from current web | Implemented | `core/src/maprenderorder.cpp`, `tools/m5-render-order-oracle.mjs`, overlay pair pixels in `tests/map_render_tests.cpp`; full primitive pair matrix and visible web pixel comparison remain open. |
| Corruption and allocation failure containment | Implemented | `tests/hydro_manifest_tests.cpp`, `tests/hydro_format_tests.cpp`, `tests/hydro_allocation_tests.cpp`, `tests/hydro_runtime_tests.cpp`; latest full suite pending. |
| Labels, flags, distribution, generic current range, typed refs | Implemented | Existing `presentation_editor_tests`, `migration_tests`, `retained_reference_tests`, M5 content Oracle; exhaustive feature matrix/UI regression remains open. |
| M3.1–M4 and M5 executable Oracle regression, fresh complete CTest | Implemented | `.github/workflows/m5-validation.yml` configures/builds/tests in a fresh build; record final total, skips and commit when green. M3.5 has native interaction tests but no separately named web Oracle. |
| Whole-object translation | Implemented | `app/editorgeometry.cpp`, `ui/common/MapView.qml`, fragmented content geometry test in `tests/presentation_editor_tests.cpp`; fresh CI pending. |
| All-domain hover/render-cache evidence | Deferred explicitly | M5 drawing/picking and earlier M3.5 tests remain; exhaustive per-domain cache/hover Oracle is an open M5 acceptance gate, not counted as complete. |
| Native v6 roundtrip and supported retained web input | Implemented | `tests/migration_tests.cpp`, `tests/web_import_ui_tests.cpp`; fresh suite pending. Native v7 output is **Deferred explicitly** until a storage-format milestone, not claimed as v7 roundtrip. |
| Full 0.13.1 worldwide dataset | Verified | Pinned checkout succeeded; `hydro_full_dataset_tests` passed in run `36022370502` against 953 tiles, 5,173 logical features and 16,548 records. That run's complete CTest had unrelated failures. |
| Current web label viewport bounds | Implemented | `core/src/presentation.cpp`, `app/editorpresentation.cpp`; selected/pinned labels bypass the box filter; fresh CI pending. Web safe-area insets still need a visual comparison. |
| Visible Windows workflow | Deferred explicitly | Requires a real Windows Qt session: choose data, pan/zoom, river/lake pick, search/copy/save/reopen and content UI. Linux offscreen CI is separate evidence. |
| Android device workflow | Deferred explicitly | Requires a physical device for SAF, touch, rotation, IME and Back. Code completion and device verification are separate claims. |
| Asset download/packaging and GPU renderer | Deferred explicitly | M7/deployment scope. Local selected dataset and Qt painting are the M5 implementation. |

## Final fresh run

Pending the latest M5 branch commit. Record the immutable commit, workflow run,
passed/failed/skipped totals, full dataset checkout outcome and each named
Oracle. Do not combine focused reruns into a complete-suite result.

## Merge gate

M5 is still open while any M5 acceptance row above lacks evidence. Keep the
draft PR out of `main` until fresh CI is green, the real dataset gate runs, draw
and pick pair evidence is complete, and visible Windows interaction is recorded.
Android physical validation must be reported separately if a device is unavailable.
