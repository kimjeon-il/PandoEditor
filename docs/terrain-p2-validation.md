# P2 terrain LOD and demand validation

Overall Web pin: `ebcfae4d27b29cbbea6416a7045a4806930204be`.
App feature baseline: `c27e3a04d77dad4e43a17ea7ea6f6f09448b9053`, branch `codex/m97-web-editing-parity`.

P2 uses logical/CSS map scale, source DPR clamped to 1–2 for a window at most799px wide or 1–3 for a wider window, and the fixed Web1.12 source-resolution margin. Both flat and globe choose the first sufficient manifest level or the highest available. Panel width, OS/input mode and country mesh quality do not choose source LOD. Native translation-based flat pan and center+rotation globe coordinates are adapted to the same geographic demand before applying Web padding/cap predicates.

Planning is passive. Complete canonical L0, directly requested target tiles and bounded neighbor prefetch have separate inventories and priorities; planning does not decode, enqueue downloads or mutate cache protection. Draw replicas share canonical asset paths. P3 controls admission and actual displayed ownership; this P2 checkpoint does not certify GPU handoff.

## Actual execution evidence

External raw evidence: `D:/Codex/evidence/p1-p8-m98-20261006/`.

| Check | Executed result | Raw evidence |
| --- | --- | --- |
| Original native RED, production provider |32 processed,10 pass,22 fail,0skip; exit1|`p2-lod-native-red.log`|
| First expanded native run |44 processed,43 pass,1fail,0skip; exit1|`p2-lod-native-green-01.log`|
| Corrected expanded native provider |44 processed,44 pass,0fail/skip; exit0|`p2-lod-native-green-02.log`|
| Current fixed-Web production owner in synthetic Node services |53 processed/pass,0fail/cancel/skip; exit0|`p2-current-web-oracle-01.tap`|
| Controller/layout content ownership and canonical QML startup, offscreen software |4 Qt rows pass (2functional+init/cleanup),0fail/skip; exit0|`p2-ui-demand-current-01.txt`|
| Actual Windows D3D11 window, DPR1.5 and exact799/800 CSS widths |4 Qt rows pass (2functional+init/cleanup),0fail/skip; nativePID8508, exit0|`p2-ui-rhi-dpr15-03.txt`, `.receipt.json`, `.stdout`, `.stderr`|

The one expanded-test error omitted real-grid column41 at the dateline. Independently, its center174.133333 is5.866667degrees from180, below half-span4.266667 +2degree padding +viewport half-span0.011173. Column41 belongs in the literal demand alongside columns0 and42. That authored test input expectation was corrected from the fixed mathematical predicate; production code, fixed Web source and existing exchange expected were unchanged. The failed run remains available.

The native tests also inspect window799/800 sourceDPR, saturation at DEML5/rasterL4, fixed threshold191.8385/.6, directL5/no intermediate requests, complete2-tile base reserve, bounded neighbors, canonical wrapped copies, translated pan, globe rotation and polarcap, and invalid-view no mutation. UI checks preserve encoded document, dirty and Undo/Redo through transient demand changes.

`tests/fixtures/web-p2-p3-current-source/source-manifest.json` contains eight exact raw Git blobs from the overall fixed Web. Its SHA256 is `1545469046c47808454f91dc9ff949f0cc005db0d6abb10235f6faff5034cfdd`. The original phase fixture manifest remains byte-identical SHA256 `180ee9179395cc3cb91d8692471d97e16a080d6ac94f613fdbb48ca4c498fc0a`. Production terrain owner, upload scheduler, workspace layout, mesh helper and original unit tests have identical Git blobs at both pins. Current map-visual-frame adds an optional canvas-path field; environment changes stroke/catalog/UI references, preserving the window799 mobile query. The current-source harness retains every original behavioral assertion and validates its newly pinned provenance independently.

## Reproduction and boundaries

```powershell
cmake --build D:/build/Pandoeditor-p1-p8-m98 --target terrain_lod_demand_tests ui_tests --parallel 2
D:/build/Pandoeditor-p1-p8-m98/terrain_lod_demand_tests.exe assets/world/terrain/v0.13.3/manifest.json assets/world/terrain/v0.12.6/manifest.json
node --test tools/p2-p3-terrain-contract/current-web-oracle.test.mjs tests/fixtures/web-p2-p3-current-source/source/tests/unit/gpu-terrain-preparation.test.mjs tests/fixtures/web-p2-p3-current-source/source/tests/unit/gpu-upload-staging.test.mjs
```

Native environment: Windows11, Qt6.8.3 MinGW64 Release, GCC13.1.0; Web Node24.21.0. Native assertions remain active. The metadata-only LOD fixtures and mocked Node upload services test mechanisms; they are neither actual terrain display nor performance acceptance. Multi-monitor DPR migration remains unobserved. P3/P4 and aggregate M9.8 remain pending.

The actual RHI log identifies NVIDIA GeForceGTX1650 as the selected adapter and reports window DPR1.5 equal to map DPR1.5, with both799/800 cases processed. The first actual-window run (`p2-ui-rhi-dpr15-02`, nativePID10124) failed because WindowsFrame's first frame refresh changed requested1280px to actual1291px; the app correctly reported1291. The fixture now sets and checks its actual CSS client size after initialization. It retains exact799/800 and actual-window DPR assertions. The successful run also retains Windows geometry warnings: an800px logical window height at DPR1.5 exceeds this monitor's permitted client height and is clipped. These layout checks are diagnostics, not the specified1920×929 final performance fixture or timing acceptance.

Independent implementation review covered provider planning and separately reviewed controller/QML source-DPR wiring. No remaining important P2 findings were reported. Monitor migration remains a validation gap. Project schema, timeline contracts, historical oracle and existing exchange expected are unchanged.
