# M5 Completion Implementation Plan

> **For agentic workers:** Execute tasks in order, with a failing test, a passing focused test, review, and a commit for each task. Use `superpowers:executing-plans` for inline implementation. Copy this document into `docs/superpowers/plans/2026-09-24-m5-completion.md` in the implementation branch before starting.

**Goal:** Read and display the real web hydro dataset in the Qt app, match web drawing and picking behavior, verify existing M5 content, and close every M5 item with evidence.

**Architecture:** Keep worldwide built-in hydro in a read-only runtime provider. Decode the binary format in Qt-independent core, validate local files and schedule viewport work in app, and send render packets to the existing renderer. `ProjectDocument.hydro` holds only user objects and copy-on-edit results. Drawing order and chooser order have separate, centralized policies derived from measured web behavior.

**Tech Stack:** C++17, Qt 6.5+, QML, QPainter, zlib, CMake/CTest, Node.js web oracles. Windows visible run and Android device verification are separate gates.

**Spec:** User's M5-A/B/C/D brief in the 2026-09-24 conversation; existing `docs/m5-implementation.md`, `docs/m5-web-oracle.md`, `docs/m5-v6-validation.md`.

## Baseline and rebase gate

- At plan creation, `PandoEditor/main = 168fb7cf7ecde29544e9f65e10fca6a2530d1de0`, `world-map/main = c0bd31d13dc8495593d78cf51f7cc195de7c9469`. The brief's `17bb218f` predates the M3.1 history merge and fixes. Before coding, fetch both heads; if either moved, record the delta and refresh pinned oracles before implementation.
- The old M5 web pin is `bc46720`; `c0bd31d` is 76 commits ahead. Audit M5-related changes in `app-hydro-settings.js`, `gpu-map-renderer.js`, `label-layout.js`, `layer-presentation.js`, `physical-layer-service.js`, `rendering-domain.js`, flag and generic flows. Do not copy unrelated site changes.
- The current Qt codec **reads v1–v6 and writes v6** (`app/projectcodec.cpp:401,596–598`). There is no native v7 writer yet. Test v6 roundtrips and retained web v7 input only where the existing importer supports it; do not claim native v7 roundtrip.
- The real web `v0.13.1/manifest.json` references `../v0.13.0/index.bin.gz`, `../v0.13.0/metadata-detail.json.gz`, and `../v0.13.0/shards/s*.bin`; core metadata is in `v0.13.1`. It declares EPSG:4326, four stages at zoom 6/6.7/7/7.5, 953 index tiles, 5,173 logical features, 902 packs, 15,193 river records, and 1,355 lake records.
- Preserve the current M3.1 fix: selection must be cleared before synchronous document-change notification (`168fb7c`). Do not put read-only hydro work into `CommandJobRunner` or modify the document during viewport loading.
- Real-world datasets are not committed to `PandoEditor`. Commit a tiny format-compatible fixture; use an optional external path to the full `world-map` dataset for the separate integration gate.

## Repository map and interface contract

| Responsibility | Existing files | Planned files |
|---|---|---|
| Binary index, pack, geometry, widths and packet DTOs | `core/CMakeLists.txt` | `core/include/pandoeditor/hydroformat.h`, `core/src/hydroformat.cpp` |
| Manifest, local assets, gzip, hashes | `app/hydrodataprovider.{h,cpp}`, `app/CMakeLists.txt` | `app/hydromanifest.{h,cpp}`, `app/hydroassetreader.{h,cpp}` |
| Viewport, cache, asynchronous results | `app/editorcontroller.h`, `ui/common/MapView.qml`, `renderer/mapprojection.{h,cpp}` | `app/hydroruntimeprovider.{h,cpp}`, `app/hydroviewportcontroller.{h,cpp}`, `app/hydroloadscheduler.{h,cpp}` |
| Render and visibility | `renderer/maprenderitem.{h,cpp}`, `app/editorcontroller.cpp`, `app/editorselection.cpp` | `core/include/pandoeditor/maprenderorder.h`, `core/src/maprenderorder.cpp` |
| Built-in selection, search, editing | `app/editorpicking.cpp`, `app/editorselection.cpp`, `app/editorcontent.cpp`, `core/src/content.cpp`, `ui/common/ContentPanel.qml` | `app/hydroobjectresolver.{h,cpp}` |
| Oracle and test gates | `app/selection.cmake`, `tests/map_render_tests.cpp`, `tools/m5-content-oracle.mjs` | `tests/hydro_format_tests.cpp`, `tests/hydro_runtime_tests.cpp`, `tests/hydro_editor_tests.cpp`, `tests/render_order_tests.cpp`, `tests/hydro_probe.cpp`, `tools/m5-hydro-oracle.mjs`, `tools/m5-render-order-oracle.mjs`, `tests/fixtures/web-hydro/` |

Suggested stable interfaces (final signatures may follow the codebase's existing error/result conventions):

```cpp
// core/include/pandoeditor/hydroformat.h — pure C++17, no Qt
HydroIndex decodeHydroIndex(ByteView bytes);           // exact consumption
HydroPack decodeHydroPack(ByteView bytes, PackId id,
                          const HydroMetadataIndex& coreMetadata);
HydroFeature mergeLogicalFragments(std::vector<HydroFeature> fragments);

// app/hydromanifest.h — local-only input, canonical paths and asset specs
HydroManifest readHydroManifest(const QString& manifestPath);

// app/hydroruntimeprovider.h — read-only; no ProjectDocument mutation
void openDataset(const QString& manifestPath);
void requestViewport(const HydroViewportRequest& request);  // revisions + project instance
std::optional<HydroDescriptor> resolveBuiltin(const QString& awId) const;
void requestLogicalFeature(uint32_t logicalFid, Completion callback);

// core/include/pandoeditor/maprenderorder.h
MapRenderOrder mapRenderOrder(const ProjectDocument&, const ObjectRef&,
                              RenderPrimitiveRole);  // fill/line/boundary/point/label
```

`HydroViewportRequest` carries `datasetGeneration`, `projectInstanceId`, `viewportRevision`, viewport dimensions, zoom, map scale, origin, and projected/geographic bounds. A completed job applies only when all three identity fields still match. `HydroDescriptor` uses `awId` as public identity and `logicalFid` for a logical river; `fid` remains the fragment identity. `HydroRenderPacket` contains river segments with start/end widths and border-aligned flag, plus lake rings/triangles and boundary segments with IDs. No global `GeometryStore` entry is made for each built-in record.

## Review focus and non-negotiable failure cases

1. A selected manifest's relative paths can traverse to sibling `v0.13.0`, but may not escape the selected hydro dataset tree or become network URLs; Task 2 tests both.
2. A verified shard can change after verification: on opening/reading a pack, compare file identity/size or revalidate, and keep the last good frame if it changed; Tasks 2 and 13 test this.
3. Antimeridian, high latitude, and exact zoom thresholds can underfetch tiles; Tasks 5 and 11 compare viewport tile sets against the web oracle.
4. A stale background callback can refer to a destroyed controller or a different project; Tasks 5 and 13 use explicit generation and cancellation tests.
5. A logical river spans multiple packs and can be evicted between selection and copy; Tasks 6, 8 and 9 protect or re-read every logical pack and test one Undo restoration.

## Task 1 — Pin the current web source and build the hydro oracle fixture

**Files:** Create `tests/fixtures/web-hydro/README.md`, `tests/fixtures/web-hydro/source/{hydro-tile-worker.js,hydro-shard-store.js,hydro-tile-window.js}`, `tests/fixtures/web-hydro/manifest.json`, a miniature `v0.13.0`/`v0.13.1` binary tree, `tests/hydro_probe.cpp`, `tools/m5-hydro-oracle.mjs`; modify `docs/m5-web-oracle.md`, `app/selection.cmake`.

- [ ] Pin the exact `c0bd31d` files and Git blob IDs: worker `2974a788`, store `37c344da`, tile window `2c84b806`, manifest `67c648a2`. Record every copied file's full SHA, license, origin, and byte-for-byte check in the README. The JavaScript oracle must invoke the actual web functions; where worker globals are private, run the worker with mocked `importScripts`, `fetch`, `postMessage`, and deterministic fixtures, rather than hand-coding expected results.
- [ ] Generate a **real format v4** mini index and packs using `world-map/tools/build-hydro-tiles.py` or its existing encoder, then keep only the miniature fixture: normal river, border-aligned river with varied vertex widths, simple lake, holed lake, and one logical river fragmented across two packs. Include golden JSON for decoded index, metadata, mesh, viewport tiles, and merged logical feature; no full dataset copied.
- [ ] First run `node tools/m5-hydro-oracle.mjs --fixture-only` with a missing native probe: expected failure. Add a native probe that accepts fixture paths and emits decoded JSON but never embeds golden answers. Register `m5_hydro_web_parity` in CTest; require actual web output equals native output once Tasks 3–5 and 9 land.
- [ ] Commit fixture, pinned source, manifest/hash contract, and an oracle that exercises web decoding. Keep future native parity cases red until their matching task; CI for the commit must not be left failing (stage separate CTest registration until the probe works).

## Task 2 — Manifest, relative paths, SHA-256, gzip, local file access

**Files:** Create `app/hydromanifest.{h,cpp}`, `app/hydroassetreader.{h,cpp}`, `tests/hydro_manifest_tests.cpp`; modify `app/hydrodataprovider.{h,cpp}`, `app/CMakeLists.txt`, `tests/presentation_editor_tests.cpp`.

- [ ] First add failing cases for valid `v0.13.1` sibling paths; schema/version/CRS mismatch; missing file; URL scheme; `..` outside the hydro parent; symlink escape; wrong compressed byte length/SHA; corrupt gzip; and a file changed after verification. `ready=true` requires parsed manifest **and** verified index/core; verify shard byte length and full SHA once before reading its first pack, using streaming I/O. Verify detail only on first detail request. Display validation state separately from viewport load state.
- [ ] Resolve URL against the manifest's directory, canonicalize the final local path, enforce containment in the common hydro data root, reject any URL with a scheme/authority, and use `QCryptographicHash::Sha256` plus exact manifest `bytes`. To preserve `QFile::seek` without repeated 4 MiB reads, cache each verified shard's identity and invalidate on observed change.
- [ ] Use `find_package(ZLIB REQUIRED)`, `ZLIB::ZLIB`, and `inflateInit2(MAX_WBITS + 16)` with checked output cap and truncated/multi-member handling matching the web fixture. Do not use Qt private compression APIs.
- [ ] Run `ctest --test-dir build-m5 -R 'hydro_manifest|presentation_editor' --output-on-failure`. Cross-compile check is a later platform gate. Commit.

## Task 3 — Index and core/detail metadata parsers

**Files:** Create `core/include/pandoeditor/hydroformat.h`, `core/src/hydroformat.cpp`, `tests/hydro_format_tests.cpp`; modify `core/CMakeLists.txt`, `app/hydrodataprovider.cpp`, `tools/m5-hydro-oracle.mjs`.

- [ ] Red tests: index magic `0x34495741`, v4, all count and offset truncations, duplicate/out-of-range references, extra trailing byte, and real index counts (953/5173/902). The 20-byte header is followed by 7-byte tile records + pack IDs, 6-byte logical records + pack IDs, and 15-byte pack specs. Validate `pack.offset + pack.length <= shard.bytes` without overflow.
- [ ] Decode metadata-core v5 at dataset open into `fid → {logicalFid, awId, name, source, sourceId, systemId, role, bounds, layerId}`. Reject malformed duplicate IDs, mismatched counts and invalid field types. Keep detail gzipped/unread until selection/copy; merge detail into metadata without replacing canonical core IDs.
- [ ] `hydro_format_tests` compares exact parsed structs and rejects every damaged input; `m5_hydro_web_parity` compares index and metadata JSON from the same bytes. Commit.

## Task 4 — Pack, geometry, width and render-packet decoding

**Files:** Extend `core/hydroformat.*`, `tests/hydro_format_tests.cpp`, `tests/hydro_probe.cpp`, `tools/m5-hydro-oracle.mjs`; modify `app/hydroassetreader.cpp`.

- [ ] Red tests: magic `0x46485741`, v4, 12-byte header, 44-byte feature record, exact end offset, metadata `logicalFid` equality, invalid geometry kind, cut varint, overflow, geometry/width length out of range, width count mismatch. `geometryKind 1/2/3/4` must map to LineString/MultiLineString/Polygon/MultiPolygon, as the pinned worker does.
- [ ] Decode delta zigzag varints at 1e-6 degrees. Widths use a first unsigned integer plus signed deltas, divided by 1,000; keep per-vertex values. Build variable-width river segment packets and separate border-aligned packets. Preserve every lake ring and hole; triangulate for render without filling holes. Check decoded segment IDs, bounds and packet sizes against the web `readPack`/`buildMesh` oracle.
- [ ] Read only `pack.length` at `pack.offset` using `QFile::seek/read`, then gunzip the pack member. Validate the decoded length and exact parser consumption. Run `ctest --test-dir build-m5 -R 'hydro_format|m5_hydro_web_parity' --output-on-failure`; commit.

## Task 5 — Geographic viewport and read-only scheduler

**Files:** Create `app/hydroviewportcontroller.{h,cpp}`, `app/hydroloadscheduler.{h,cpp}`, `tests/hydro_runtime_tests.cpp`; modify `app/editorcontroller.{h,cpp}`, `ui/common/MapView.qml`, `renderer/mapprojection.{h,cpp}`, `app/CMakeLists.txt`.

- [ ] Red tests for zoom 5.99/6/6.7/7/7.5, pan, resize, antimeridian, high latitude, A→B viewport while A decodes, Project A→B while A decodes. Expected tile IDs come from the pinned `hydro-tile-window.js` oracle. The Qt `zoom` property is a fit-relative multiplier; compare the actual web `hydroVisibilityThreshold()` input and add a single adapter if direct equivalence fails.
- [ ] QML reports `zoom, panX, panY, width, height` and revision; controller computes the screen corners via `MapProjection::unproject`, forms geographic bounds and asks the viewport controller for stage/tile/pack IDs. The current projection is flat; document and test its limits against web flat mode, without claiming globe parity.
- [ ] Use `QtConcurrent`/bounded worker scheduling in a dedicated read-only `HydroLoadScheduler`, never document jobs. Apply a complete packet set only if dataset generation, project instance, and viewport revision still match; keep prior valid render until replacement. Connect with weak lifetime guards. Run `hydro_runtime_tests` and oracle viewport cases; commit.

## Task 6 — 96/48 MiB active-pack cache

**Files:** Create `app/hydroruntimeprovider.{h,cpp}`; extend `tests/hydro_runtime_tests.cpp`, `app/CMakeLists.txt`.

- [ ] Red tests count actual owned decoded buffers, desktop 96 MiB/mobile 48 MiB, rapid pan/zoom, duplicate concurrent pack request, and a protected selected/copying logical pack. Eviction must never remove active viewport packs or protected logical packs; if all entries are protected, allow a measured temporary over-budget state and release it after the operation rather than corrupting data.
- [ ] Publish immutable `HydroDescriptor`/`HydroRenderPacket` from verified packs; LRU removes least recently used inactive packs. Cache persists only in memory; local original files are the durable source. Surface byte count, active pack IDs and last error for diagnostics.
- [ ] Run `hydro_runtime_tests` with both budget profiles, then commit.

## Task 7 — Display actual river/lake pixels

**Files:** Modify `renderer/maprenderitem.{h,cpp}`, `app/editorcontroller.{h,cpp}`, `ui/common/MapView.qml`, `tests/map_render_tests.cpp`, `tests/ui_tests.cpp`; extend provider files.

- [ ] Add red offscreen pixels for variable river width, border-aligned river, lake hole, opacity, each category hidden, selected built-in, edited copy, and original hidden after copy. Keep the existing frame while the next viewport is loading or an asset fails.
- [ ] Pass a separate runtime packet property into `MapRenderItem`; project coordinates on render and preserve feature IDs. Use existing group visibility/opacity for `rivers` and `lakes`; draw built-in and editable hydro in the same presentation group. Preserve the QML label layer and M3.5 selection emphasis.
- [ ] Run `map_render_tests`, `ui_tests`, hydro runtime tests; commit. A QPainter approximation is acceptable only where pixel and order oracles confirm parity; the provider packet must remain independent of QPainter for later M7 GPU use.

## Task 8 — Built-in resolution, hit testing, search and focus

**Files:** Create `app/hydroobjectresolver.{h,cpp}`; modify `app/editorpicking.cpp`, `app/editorselection.cpp`, `app/editorcontroller.h`, `ui/common/ObjectSearch.qml`, `tests/hydro_editor_tests.cpp`, `tests/selection_editor_tests.cpp`.

- [ ] Red tests: visible line and polygon hit, lake hole miss, hidden object miss, border-aligned river hit, multi-fragment logical river resolves once, search from core metadata without pack loading, focus, chooser identity, stale selection across project reopen. Web oracle fixes hidden-hydro search policy (record whether result is returned or filtered, then assert it).
- [ ] Add a resolver for runtime descriptors alongside `ProjectDocument.index()`. Restrict exact hit testing to active pack descriptors after bounds filtering; no world scan on click. Use `awId` for UI identity and logical grouping; `existingObjectRef()` needs an explicit runtime-hydro branch without adding every built-in to the document index. Search the core metadata index; focus via descriptor bounds and load geometry only when required.
- [ ] Run `hydro_editor_tests`, selection tests, and chooser UI cases; commit.

## Task 9 — Logical feature load and atomic copy-on-edit

**Files:** Extend `app/hydroruntimeprovider.*`, `app/hydroobjectresolver.*`, `app/editorcontent.cpp`, `core/src/content.cpp`, `tests/hydro_editor_tests.cpp`, `tests/migration_tests.cpp`.

- [ ] Red test: select built-in logical river spanning two packs, load detail metadata and all `logicalPacks`, sort fragments by `fragmentIndex`, preserve widths, convert to canonical `HydroFeature`, add its `awId` to `physicalData.hiddenHydroIds` in **the same command candidate**. Undo must remove copy and show original; Redo must hide original again. Include lake, missing pack, failure after first fragment, save/reopen, and a dataset switch while copying.
- [ ] Extend the existing `content.edit` prepare/confirm transaction, rather than doing a separate visibility mutation. Keep original built-in read-only; copied geometry enters the normal `GeometryStore` only on successful confirm. Reject incomplete or duplicate fragment indices; detail metadata remains lazy until this path or explicit object inspection.
- [ ] Run `hydro_editor_tests`, `migration_tests`, `command_allocation_tests`; commit.

## Task 10 — Centralize draw order without changing M3.4 objectOrder

**Files:** Create `core/include/pandoeditor/maprenderorder.h`, `core/src/maprenderorder.cpp`, `tests/render_order_tests.cpp`; modify `core/CMakeLists.txt`, `app/editorcontroller.cpp`, `renderer/maprenderitem.cpp`, `tests/presentation_tests.cpp`.

- [ ] Red tests for every pair in the brief: Country–River/Lake; Hydro–Religion; Religion–Ethnicity; Ethnicity–Language; Language–Subunit; Subunit–Region; Region–Generic; Generic–Place; Place–Label. Exercise fill/line/boundary/point/label roles independently. Oracle task 11 supplies the expected top pixel; do not pick numbers from intuition.
- [ ] Replace `countryVisuals()`'s `50/60/70/100` rank constants and reconcile territorial `-1000/3000/4000` through `mapRenderOrder(document, ref, role)`. The return type includes domain/group/order/within-group tie key and layer order; `MapRenderItem` sorts the same tuple it receives. Preserve web-supported territorial `objectOrder`; do not add arbitrary reorder for content objects.
- [ ] Run render/order and presentation tests; commit.

## Task 11 — Measure web draw order and chooser order separately

**Files:** Create `tools/m5-render-order-oracle.mjs`, `tests/fixtures/web-m5-order/` fixtures, `tests/render_order_tests.cpp`; modify `app/editorpicking.cpp`, `app/editorselection.cpp`, `app/selection.cmake`, `docs/m5-web-oracle.md`.

- [ ] In the pinned web app at `c0bd31d`, overlap the ten pairs and capture **three independent outputs**: top rendered pixel/object, first map hit, and chooser array. Include selected emphasis, flag, point marker and QML label text as separate layering cases. Use actual web render/pick code or a browser scene where importing the code is impossible; record screenshot coordinates, color IDs and any platform/browser assumptions.
- [ ] Give picker/chooser a separate policy from `MapRenderOrder`, based on the actual `app-object-picking.js:106–116,183` behavior and GPU hydro/country hit paths. A visually frontmost object need not be the first chooser row. Update runtime hydro and editable hydro together; respect visibility and existing name tie break.
- [ ] Register `m5_render_order_web_parity`; run browser oracle, native pixel tests, `render_order_tests`, `selection_editor_tests`, then commit. If a web pair cannot be constructed, mark that pair unverified in the matrix rather than inventing its answer.

## Task 12 — Existing M5 labels, flags, distributions and generic regression

**Files:** Extend `tests/presentation_editor_tests.cpp`, `tests/migration_tests.cpp`, `tests/ui_tests.cpp`, `tests/territorial_geometry_tests.cpp`, `tests/map_render_tests.cpp`, `tools/m5-content-oracle.mjs`; update `tests/fixtures/web-content/source/` and `docs/m5-web-oracle.md` only for changed current-web source.

- [ ] Make failing end-to-end cases for label create/edit/delete/move/pin/auto/collision/Undo/reopen; flag Default/None/Embedded/invalid/missing default for country/subunit/region; distribution language/ethnicity/religion, typed territorial/free geometry, independent 60+70, dominant/intensity, equal-share first-input winner, visibility, parent delete/reopen; generic operations **only within the web-supported edit range**.
- [ ] Refresh web goldens from the M5-relevant `bc46720..c0bd31d` diff, especially updated label layout, flags, layer presentation and physical settings. Keep existing behavior that still matches; fix only observed divergences. Add typed-reference and whole-object translation cases called out in `docs/m5-implementation.md`.
- [ ] Run focused suites and `m5_content_web_parity`; commit. Do not treat focused reruns as a full pass.

## Task 13 — Corruption, stale work and allocation failures

**Files:** Extend `tests/hydro_format_tests.cpp`, `tests/hydro_manifest_tests.cpp`, `tests/hydro_runtime_tests.cpp`, `tests/hydro_editor_tests.cpp`, `tests/command_allocation_tests.cpp`.

- [ ] Execute every corruption case in the brief: wrong schema/version, relative asset missing, SHA/length mismatch, gzip, index/pack magic and versions, truncated varint, geometry length overflow, metadata mismatch. Assert error classification, no crash, unchanged `ProjectDocument`, valid selection, retained last good render frame.
- [ ] Fault-inject allocations at manifest/index/core metadata/pack decode/logical merge/cache swap. Test viewport A→B completion order and Project A→B replacement; stale results must not publish or mutate the new project. Include changed shard after SHA validation, corrupted detail on first selection, temporary cache over-budget, and worker/controller destruction.
- [ ] Run the five focused test targets with sanitizers where supported; record sanitizer configuration and any unavailable platform. Commit.

## Task 14 — Fresh clean build, full CTest and all executable oracles

**Files:** Modify `app/CMakeLists.txt`, `core/CMakeLists.txt`, `app/selection.cmake`, `.github/workflows/m3-selection-validation.yml` or a dedicated M5 workflow, `docs/m5-validation.md`.

- [ ] Register hydro, order and M5 end-to-end targets in CTest. Inventory M3.1 selection, M3.2 property, M3.3 structure, M3.4 presentation, M3.5 interaction (create an executable oracle if missing), M4 geometry, M5 content/hydro/order. Existing scripts include `m3-selection-oracle.mjs`, `m32-property-oracle.mjs`, `m33-structure-oracle.mjs`, `m34-presentation-oracle.mjs`, `m4-geometry-oracle.mjs`, `m5-content-oracle.mjs`; a named M3.5 oracle does **not** exist in the current tree. Distinguish newly added from existing checks.
- [ ] From a **new** directory run:

```bash
cmake -S . -B build-m5-clean -DBUILD_TESTING=ON -DPANDOEDITOR_BUILD_APP=ON
cmake --build build-m5-clean --parallel
ctest --test-dir build-m5-clean --output-on-failure
ctest --test-dir build-m5-clean -N
```

- [ ] Save command, source SHAs, CTest passed/failed/skipped totals, each oracle status and logs. If Node is absent and CMake silently omits an oracle, the M5 gate fails rather than recording it as green. Run the optional full `0.13.1` dataset integration separately using a local `world-map/assets/data/hydro/v0.13.1/manifest.json` path; verify reported 953 tiles/902 packs/16,548 records and representative worldwide river/lake results. Commit validated report and workflow changes.

## Task 15 — Visible Windows run and Android device evidence

**Files:** Update `docs/m5-validation.md`, `docs/android-review-brief.md` and, if a real integration bug is found, the owning code/test file.

- [ ] On real Windows GUI, run the 12 actions in the brief: launch, select local dataset, see river/lake, increase detail by zoom, click each, search, copy, save, restart/reopen, toggle visibility, and use distribution/label/flag UI. Record Qt/toolchain, data path, screenshots or screen capture, result per action, and defect IDs. Offscreen QML is a different test.
- [ ] Android cross-build with zlib and test, on a **physical device** if available: dataset path access, viewport pan/zoom/stale, touch select/copy, SAF, rotation, Korean IME, physical Back. Record device/API version and per-case result. If unavailable, mark `Android physical: unverified`; do not report M5 platform validation as completed by the cross-build.
- [ ] Fix discovered bugs with focused red tests and rerun Task 14's full gate after code changes. Commit evidence and any fixes.

## Task 16 — M5 closure audit and report

**Files:** Create `docs/m5-feature-matrix.md`, `docs/m5-completion-report.md`; update `docs/m5-implementation.md`, `docs/m5-v6-validation.md`, `docs/m5-web-oracle.md` only to reflect observed outcomes.

- [ ] For every M5-A/B/C/D requirement, record `Implemented`, `Verified`, `Unsupported by web`, or `Deferred explicitly`, linked to code file, test, oracle/run and platform evidence. Include whole-object translation, exact primitive order, all-domain hover/render cache, allocation failure and typed references. No unclassified WIP rows.
- [ ] Separate code complete, Windows verified and Android verified statuses. Explicitly list what remains for M6/M7, including network download/packaging and GPU rewrite, without silently moving an M5 acceptance item out of scope.
- [ ] Final gate: real v5 decode, active river/lake pixels, stage/width/hole, logical pick/search/focus/copy/hide/Undo, data-error containment, draw **and** pick web parity, M5 content regression, native v6 roundtrip and supported web import, M3.1–M4 oracles, clean full CTest, and recorded platform evidence. Set `M5 complete` only if all required rows are verified or an explicitly accepted scope change is recorded. Commit the report.

## Execution rules

For each task: write the named failing test first; run it and retain the failure; make the smallest implementation; rerun the focused suite; inspect the diff and `git diff --check`; commit with the task number. Run dependent tasks sequentially (A → B → C → D). Rebase from the latest `main` before Task 1 and compare `world-map` before every oracle refresh. Do not push into `main` until the clean build/CTest and review gates pass. Any failure found in Task 14 or 15 returns to its owning task, then reruns the clean full gate.

**Status of this document:** implementation plan only. No M5 code, tests, Windows GUI, Android device check, or M5 completion claim was made while writing it.
