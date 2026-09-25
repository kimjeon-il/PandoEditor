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
| Central draw order and separate chooser order from current web | Implemented | `m5_render_pick_order_parity` now compares all 55 pairs of 11 object/primitive roles plus five boundary/line pairs against pinned web functions, keeping visible top and chooser first separate. Qt pixels cover country/subunit/region ownership; 1100/360 px QML checks label/flag placement above selected geometry. See `m5-order-verification.md`. A live browser confirmed flag/name labels above the canvas; pinned browser pixel captures for every synthetic pair, translucent ownership and selected-emphasis visual comparison remain open. |
| Corruption and allocation failure containment | Verified | `hydro_manifest_tests`, `hydro_format_tests`, `hydro_allocation_tests`, `hydro_runtime_tests` passed in run `36024805936`. |
| Labels, flags, distribution, generic current range, typed refs | Implemented | `presentation_editor_tests`, `migration_tests`, `retained_reference_tests`; the M5 content Oracle was present but not registered in the earlier 46-test CTest run. It is now registered as `m5_content_web_parity`. See `m5-display-closure.md` for the feature/UI matrix and remaining gates. |
| M3.1–M4 and M5 executable Oracle regression, fresh complete CTest | Verified | Run `36024805936` built afresh and passed all 46 tests then registered; 0 failed, 0 skipped. Its CTest list did not include `m5_content_web_parity`, despite the earlier prose claim below. M3.5 has native interaction tests but no separately named web Oracle. |
| Whole-object translation | Verified | `app/editorgeometry.cpp`, `ui/common/MapView.qml`, fragmented content geometry test in `presentation_editor_tests` passed in run `36024805936`. |
| All-domain hover/render-cache evidence | Implemented with visual limits | `presentation_editor_tests::visibleContentDomainsHoverAndHiddenHoverExpires` covers typed label/distribution/generic pick and hidden-hover invalidation; `map_render_tests::viewportChangesRepaintCachedMapTexture` checks pixel repaint after origin and scale changes. Labels/flags are a QML overlay and the other domains share one painted texture. Browser hover stroke and true device rendering remain open; see `m5-display-closure.md`. |
| Native v6 roundtrip and supported retained web input | Verified | `migration_tests`, `web_import_ui_tests` passed in run `36024805936`. Native v7 output is **Deferred explicitly** until a storage-format milestone, not claimed as v7 roundtrip. |
| Full 0.13.1 worldwide dataset | Verified | Pinned checkout succeeded; `hydro_full_dataset_tests` passed in run `36024805936` against 953 tiles, 5,173 logical features and 16,548 records, including lazy detail and logical ID zero copy. |
| Current web label viewport bounds | Implemented | `core/src/presentation.cpp`, `app/editorpresentation.cpp`; selected/pinned labels bypass the box filter. New pinned-web and Qt 1100/360px safe-area tests and captures are recorded in `m5-display-closure.md`; pinned browser pixel comparison is still open. |
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
structure, presentation and M4 geometry Oracles, plus M5 hydro fixture,
viewport, index, pack, logical merge and render/pick Oracles, passed in this
single run. Its offscreen UI tests do not substitute for visible Windows or
Android device verification.

Order verification code revision `b903e5d` passed [Linux workflow
36154575971](https://github.com/kimjeon-il/PandoEditor/actions/runs/36154575971):
fresh Qt build and complete CTest **46/46**, zero failures. The updated
`m5_render_pick_order_parity` compares 55 object/role pairs and five further
primitive-role pairs. `map_render_tests` includes the country/subunit/region
overlap pixel case, and `ui_tests` checks selected geometry behind the
label/flag delegate at 1100/360 px. Browser pixels for the complete pinned
scene and visible Windows/Android operation have not been verified.

## Merge gate

M5 is still open while any M5 acceptance row above lacks evidence. Keep the
draft PR out of `main` until the remaining pinned visual comparisons and
visible Windows interaction are recorded. The real dataset and source-driven
pair gates have Linux CI evidence; they do not replace visible platform checks.
Android physical validation must be reported separately if a device is unavailable.
