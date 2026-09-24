# M5 closure audit

Status: **open**. This is an evidence ledger for `codex/m5-completion`, not a
claim that M5 can be merged. The web baseline is `world-map` `c0bd31d1`; the
editor base was `168fb7cf` (the brief's `17bb218f` was older). The native codec
reads v1–v6 and writes v6. A native v7 writer is not present.

The labels below mean: **Implemented** = code exists but an acceptance gate is
outstanding; **Verified** = a named executable test/Oracle passed at the stated
revision; **Unsupported by web** = deliberately outside the web contract;
**Deferred explicitly** = a named follow-up stage or platform gate. A failed or
skipped test never counts as Verified. The latest fresh CI run and its skips
are recorded below; the open acceptance gates remain explicit.

| M5 requirement | Classification | Evidence / remaining gate |
| --- | --- | --- |
| Local 0.13.1 manifest, sibling relative URLs, length/SHA, gzip and local-only asset paths | Verified | `app/hydromanifest.cpp`, `app/hydroassetreader.cpp`, `hydro_manifest_tests` in run `36024805936`. |
| Global index, core metadata and lazy detail | Verified | `core/src/hydroformat.cpp`, `app/hydrometadata.cpp`, `hydro_format_tests`, `hydro_full_dataset_tests` in run `36024805936`. |
| Shard ranges, pack v4, delta geometry, widths and lake holes | Verified | `hydro_format_tests`, `map_render_tests`, pinned web index/pack/merge Oracles and actual full dataset in run `36024805936`. |
| Stage thresholds, viewport selection and stale project/viewport work | Verified | `hydro_runtime_tests`, `m5_hydro_viewport_parity` in run `36024805936`. |
| Read-only runtime, 96/48 MiB pack cache and unchanged document | Verified | `hydro_cache_tests`, `hydro_runtime_tests` in run `36024805936`; frame allocation is outside the cache byte counter. |
| Rivers and lakes, opacity, width, holes, selection and visibility pixels | Verified | `renderer/maprenderitem.cpp`, `map_render_tests` in run `36024805936`; real visible Windows rendering remains a separate gate. |
| Built-in pick, logical identity, metadata search and focus | Verified | `app/editorpicking.cpp`, `app/editorselection.cpp`, `presentation_editor_tests` in run `36024805936`; hidden search returns `visible=false`. |
| Multi-pack copy-on-edit, hide, one Undo/Redo and save/reopen | Verified | `app/editorcontent.cpp`, `presentation_editor_tests`; fragmented-copy roundtrip and actual logical ID zero copy passed in run `36024805936`. |
| Central draw order and separate chooser order from current web | Implemented | `m5_render_pick_order_parity`, overlay pair/country/hydro/point pixels in `map_render_tests` pass in run `36024805936`; full primitive pair matrix, label/flag/selection z order, and visible web pixel comparison remain open. |
| Corruption and allocation failure containment | Verified | `hydro_manifest_tests`, `hydro_format_tests`, `hydro_allocation_tests`, `hydro_runtime_tests` passed in run `36024805936`. |
| Labels, flags, distribution, generic current range, typed refs | Implemented | `presentation_editor_tests`, `migration_tests`, `retained_reference_tests`, M5 content Oracle passed in run `36024805936`; exhaustive feature matrix/UI regression remains open. |
| M3.1–M4 and M5 executable Oracle regression, fresh complete CTest | Verified | Run `36024805936` built afresh and passed all 46 registered tests, including the named web Oracles; 0 failed, 0 skipped. M3.5 has native interaction tests but no separately named web Oracle. |
| Whole-object translation | Verified | `app/editorgeometry.cpp`, `ui/common/MapView.qml`, fragmented content geometry test in `presentation_editor_tests` passed in run `36024805936`. |
| All-domain hover/render-cache evidence | Deferred explicitly | M5 drawing/picking and earlier M3.5 tests remain; exhaustive per-domain cache/hover Oracle is an open M5 acceptance gate, not counted as complete. |
| Native v6 roundtrip and supported retained web input | Verified | `migration_tests`, `web_import_ui_tests` passed in run `36024805936`. Native v7 output is **Deferred explicitly** until a storage-format milestone, not claimed as v7 roundtrip. |
| Full 0.13.1 worldwide dataset | Verified | Pinned checkout succeeded; `hydro_full_dataset_tests` passed in run `36024805936` against 953 tiles, 5,173 logical features and 16,548 records, including lazy detail and logical ID zero copy. |
| Current web label viewport bounds | Implemented | `core/src/presentation.cpp`, `app/editorpresentation.cpp`; selected/pinned labels bypass the box filter; tests pass in run `36024805936`. Web safe-area insets still need a visual comparison. |
| Visible Windows workflow | Deferred explicitly | Requires a real Windows Qt session: choose data, pan/zoom, river/lake pick, search/copy/save/reopen and content UI. Linux offscreen CI is separate evidence. |
| Android device workflow | Deferred explicitly | Requires a physical device for SAF, touch, rotation, IME and Back. Code completion and device verification are separate claims. |
| Asset download/packaging and GPU renderer | Deferred explicitly | M7/deployment scope. Local selected dataset and Qt painting are the M5 implementation. |

## Final fresh run

Code revision `efd98f213360e8ad8c72c1103ed2e039b830f852`, GitHub Actions
run `36024805936`, fresh Linux Qt 6.8.3 configure/build and `ctest
--output-on-failure`: **46 passed / 0 failed / 0 skipped**. The uploaded JUnit
`results.xml` independently reports `tests=46`, `failures=0`, `disabled=0`,
`skipped=0`. The pinned `world-map` full hydro dataset checkout and
`hydro_full_dataset_tests` both passed. Registered M3 selection, property,
structure, presentation and M4 geometry Oracles, plus M5 content, hydro fixture,
viewport, index, pack, logical merge and render/pick Oracles, passed in this
single run. Its offscreen UI tests do not substitute for visible Windows or
Android device verification.

## Merge gate

M5 is still open while any M5 acceptance row above lacks evidence. Keep the
draft PR out of `main` until fresh CI is green, the real dataset gate runs, draw
and pick pair evidence is complete, and visible Windows interaction is recorded.
Android physical validation must be reported separately if a device is unavailable.
