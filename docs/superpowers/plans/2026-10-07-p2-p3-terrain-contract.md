# P2/P3 Terrain Contract Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make terrain LOD independent of country mesh quality and preserve displayed geographic coverage through progressive GPU preparation under bounded resource ownership.

**Architecture:** A pure demand/coverage state consumes immutable view snapshots and independently tagged readiness/display observations. A window-owned render-thread resource owner prepares candidates; the GUI controller accepts authenticated frame receipts before releasing fallback. PhysicalDataStore continues to own verified immutable asset bytes.

**Tech Stack:** C++17, Qt Quick scene graph, existing native resource policy, Node synthetic fixed-Web oracle and independently authored trace checks.

**Spec:** Original P1–P8 attachment `C:/Users/taeeu/.codex/attachments/0b553b8b-cdfc-4fdb-96f8-a315eb1ac293/붙여넣은 텍스트.txt`; fixed Web commits `e1ae1fab1f195aba898e59066812322c5d1b30a6`, `8d449c41341bf5f8c61c0631a4a55fa14ff4ec36`, `fa37a222fb7a577de860b4e3902fafbc36b29c22`. Parent-approved P1 contract must stabilize before implementation below.

## Global Constraints

- Preparation only in this deliverable: no production/provider/controller/QML/CMake edits, builds, native execution, benchmarks, commits or Web checkout mutations.
- All fixture source bytes are exact `fa37a222` Git blobs; preserve old M97/timeline fixture pins.
- New files are isolated under `tests/fixtures/web-p2-p3-terrain-source/`, `tools/p2-p3-terrain-contract/`, and this plan.
- Node tests use fake fetch, decoded bitmaps and GL/uploads. They do not prove browser/native rendering, measured GPU completion, hardware performance or VRAM use.
- Native state trace expectations are handwritten synthetic dyadic tiles; they are not output regenerated from native code.
- P2/P3 implementation remains pending. Authored native trace checker self-tests are never native parity evidence.

## Review Focus

- Detail before both complete-world base tiles: first display remains empty until the reserve is GPU-ready; candidate upload must not depend on visible delegates.
- Same CSS view under frame/backing DPR changes and window width799/800: country quality must not select terrain LOD.
- Late completions/receipts with old source, window, context, project, view, mask, candidate or request identity: no publication or fallback release.
- Partial replacement and reverse zoom: fallback remains until an accepted display receipt proves replacement coverage, then obsolete leases are released.
- Texture/QImage ownership and indivisible uploads: reserve bytes before work, count shared allocations once and temporary allocations separately; do not claim submission is GPU completion or nominal RGBA bytes are measured VRAM.

## Prepared Files and Evidence Boundary

`tests/fixtures/web-p2-p3-terrain-source/source-manifest.json` pins eight exact source blobs,149,907 bytes. The oracle test pins the manifest SHA256 `180ee9179395cc3cb91d8692471d97e16a080d6ac94f613fdbb48ca4c498fc0a` and checks every blob's SHA256, Git blob SHA1 and length. Copied source modules/tests keep their original relative imports; no source rewrites occur.

`tools/p2-p3-terrain-contract/web-fixture.mjs` adapts only synthetic fetch, bitmap and upload services around the pinned production terrain owner. `web-oracle.test.mjs` adds literal real-grid inputs from the published DEM/P1 attachment contract; six levels have2,6,18,66,242,946 tiles. These inputs are distinct from the original Web test's synthetic2048px grid.

`native-receipt-cases.mjs` supplies12 proposed native scenarios/33 observations: cold detail first; progressive west/full handoff; reverse zoom; eight stale identity dimensions; dateline copies. Inventories are lexical key order; draw order is semantic and must be preserved. Each draw includes its actual submitted bounds and resource content key; a later native adapter must observe these values from the render candidate, never synthesize them from fixture IDs. `native-trace-checker.mjs` checks exact observations and rectangle arrangement coverage. Its self-tests reject495 single-field/inventory/bounds/resource mutations and narrow geographic gaps. They do not implement the native transition algorithm.

External evidence directory `D:/Codex/evidence/p1-p8-m98-20261006/p2-p3-contract-preparation/` contains `preparation-02.tap` (pre-manifest-pin run), `preparation-03.tap` (manifest-pin run), and `preparation-04.tap` (final draw-bounds/resource mutation run): each71pass,0fail,0skip,exit0. Its `evidence-source-checksums.json` records unchanged log SHA256 and the fixture/harness source checksums. Generated logs remain outside the repository. An earlier terminal-only run had59pass/2fail: a missing copied upload-test dependency and a harness assertion before Web `prepare()` refreshed counts. Both were corrected without rewriting fixed source or expected behavior.

Run synthetic checks only:

```powershell
& 'C:/Program Files/nodejs/node.exe' --test --test-reporter=tap tools/p2-p3-terrain-contract/web-oracle.test.mjs tools/p2-p3-terrain-contract/native-trace-checker.test.mjs tests/fixtures/web-p2-p3-terrain-source/source/tests/unit/gpu-terrain-preparation.test.mjs tests/fixtures/web-p2-p3-terrain-source/source/tests/unit/gpu-upload-staging.test.mjs
```

## Task1: Pure LOD and Demand

**Later files:** extend `renderer/terrainprovider.h/.cpp`; add pure engine demand tests and register them only after P1 integration. Existing native `MapViewState.scale` is logical/CSS pixels.

**Interfaces:** Proposed `TerrainDemand planTerrain(view, mobileLayout, manifest)` returns target level, base keys, visible target specs, neighboring prefetch and priority. `mobileLayout` comes from authoritative window layout, not map-panel size, OS or touch.

- [ ] Port sourceDPR=`min(mobileLayout?2:3,max(1,deviceDpr))`; choose first width>=`1.12*max(1,2π*view.scale*sourceDpr)` or highest level for flat and globe.
- [ ] Port literal tests177.45CSSpx/sourceDPR3: window799→L1,window800→L2; frameDPR1/1.5/2/3 and preview/canonical unchanged. Check L0 threshold191.8385→L0,191.8386→L1 at sourceDPR1.
- [ ] Port flat wrapped-distance/2degree padding and globe great-circle cap with viewport/scale in matching units. Base is the complete L0 world; request target and neighbors directly, not intermediate levels.
- [ ] Preserve canonical asset key across flat world replicas; prioritize base50000, target30000-distance*1000, neighbor1000. Abort obsolete demand without resetting valid resident textures on pan.

## Task2: Pure Coverage and Generation State

**Later files:** replace `app/terraindisplaystate.h` with a tested pure state owner; connect explicit protections in `renderer/terrainprovider.*`. Do not let `finishPending()` release displayed fallback on CPU decode.

**Interfaces:** `Scope` contains sourceEpoch,windowEpoch,contextEpoch,projectGeneration,viewGeneration,maskGeneration,candidateSequence,requestSequence. Proposed events are CPU_READY,UPLOAD_SUBMITTED,CANDIDATE_BUILT,DISPLAY_RECEIPT,CANCEL,RESET. Resource readiness is distinct from current-view draw adoption. Immutable already-resident resources may be reused after a view change; stale asynchronous insertions/receipts may not mutate current state.

- [ ] Reproduce authored scenarios with native state observations before changing shared production transitions.
- [ ] Represent each tile interior as `[west,north,east,south]`; use strict interior overlap, dateline-split geographic domains and offset replicas only for drawing. Gutters do not enlarge coverage.
- [ ] Draw complete world base, overlapping ready nonbase fallback in ascending level, then ready target last. First display requires both base tiles even when all detail arrives first.
- [ ] Reject all eight stale identity classes. A receipt cannot release current leases merely because tile IDs resemble the replacement.
- [ ] Retain old detail until each covered region has current submitted replacement; release it after matching accepted display. Keep base protected when full targets cover the view and base is omitted from draw.
- [ ] Check full rectangle-arrangement coverage, narrow vertical/horizontal gaps, poles and dateline replicas. Later render tests must separately validate projected triangles/masks and screen output.

## Task3: Render-Thread Candidate Owner and Receipt Bridge

**Later files:** `app/terrainimageprovider.*`, `renderer/geographicimageitem.*`, a shared render-thread terrain resource owner, `app/editorpresentation.cpp`, `ui/common/MapView.qml`. Parent/renderer owner must settle file ownership before edits.

**Interfaces:** An immutable candidate snapshot carries scope, CPU resource leases, texture content keys and current mesh/mask/view identity. Render owner submits an exact resource/draw inventory; a queued GUI receipt carries the same scope/candidate/frame identity. Window/context invalidation rejects old callbacks and releases render-thread resources safely.

- [ ] Prepare base/tint candidates independently of visible QML delegates to avoid cold-start gating deadlock. Deduplicate textures per window/content key, including repeated-world copies.
- [ ] Treat QSGTexture creation as candidate allocation, `commitTextureOperations` as upload enqueue, frame submission as submitted-resource evidence, and matching frameSwapped as onscreen display receipt. Neither is a GPU fence. Do not publish based on CPU-ready bootstrap.
- [ ] Revalidate scope/candidate/view/mask on the receiver and release replaced fallback only from accepted current display inventory. Use a monotone window/context identity as in existing GpuMapItem.
- [ ] Keep valid fallback reprojected while current candidates prepare. Failed allocations/uploads retain old resources; reset/cancel releases candidates without restoring stale state.
- [ ] Serialize submitted snapshots as the proposed trace fields and run `validateActualNativeTrace` only with authenticated app binary,commit,run and raw log artifacts. Its provenance fields alone cannot prove execution.

## Task4: Bounded Reservations and Upload Scheduling

**Later files:** resource owner and existing cache policy adapters; focused pure admission/lease tests followed by actual Qt observation tests.

**Interfaces:** Reservations distinguish CPU resident,shared backing allocations,in-flight decoded bytes,texture nominal bytes,staging bytes,mesh bytes and retired leased bytes. Texture destruction/lease retirement acknowledgements belong to the render thread.

- [ ] Reserve mandatory baseRGBA3,666,632 and tintRGBA33,554,432 before detail; one nominal set totals37,221,064 bytes. Account CPU and texture/staging copies separately without calling nominal texture size measured VRAM.
- [ ] Reserve expected bytes before decode/upload; suppress prefetch first and defer inadmissible detail while retaining world coverage. Report memory pressure rather than target-complete. Release obsolete leases after handoff; do not accumulate partial viewport history.
- [ ] Shared synchronous frame scheduling must acknowledge indivisible QSG uploads: record a single admitted oversized operation/overrun and defer remaining work, or add chunked upload machinery. Object creation throttling alone is not an upload-byte proof.
- [ ] Match Web active-input/hidden/500ms settle behavior; hover does not renew the interaction delay. Port original fixed source upload staging tests before measuring native work.
- [ ] Only after pure and Qt gates run record actual counts,exit codes,retained/raw inventories,CPU/QSG allocation scopes and unobservable GPU/VRAM limitations. P1 must be stabilized first; no implementation is committed by this preparation deliverable.
