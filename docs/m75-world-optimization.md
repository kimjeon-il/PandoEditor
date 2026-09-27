# M7.5 — world rendering optimization implementation

Source contract: `world-map@c0bd31d13dc8495593d78cf51f7cc195de7c9469`.
`tests/fixtures/web-m75/manifest.json` pins the relevant source blobs; the
local Node oracle verifies those blobs before deriving LOD, country-range and
quality examples. The canonical M7.4 packet, mesh and ProjectDocument remain
authoritative. This phase does not change their coordinates or resolution.

## Connected paths

| Area | Runtime behavior | Observable state |
|---|---|---|
| Display LOD | Independent generic background geometry can use coarse/medium derived geometry. Territorial, distribution, labels, selected and edited objects use exact/high packets. Dateline chains skip simplification; globe preparation densifies coarse/medium edges. | `renderQuality.tier`; packet cache builds/hits |
| Countries | Country bounds reject offscreen flat copies and conservative rear hemisphere candidates. World fit uses all ranges; visible countries retain the canonical mesh. Selection outline can bypass base culling. | `GpuMapItem.visibleCountryCount`, `drawIndexCount`, `fullIndexCount` |
| Packet cache | LRU keyed by object, GeometryRef, primitive, LOD and preparation policy. Style, selection and viewport coordinates are absent. Budget varies by tier; selected/edit entries are protected. | `renderQuality.packetCacheBytes`, `packetCacheBudgetBytes`, `packetCacheEvictions` |
| Upload | New scene graph nodes are staged over update frames with interaction/settle budgets. Existing compatible node buffers are reused. A protected node or a single indivisible node may exceed the soft per-frame budget. New scene generations drop unfinished old nodes. | `GpuMapItem.uploadBytesThisFrame`, `uploadsPending`, `geometryUploadCount` |
| Terrain | Qt Quick image requests use the pinned terrain provider's byte LRU; visible tile paths are protected. Qt Quick's separate image cache is disabled. Quality changes alter the decode budget, not source resolution. | `renderQuality.terrainCacheBytes`, `terrainCacheBudgetBytes` |
| Hydro | Existing active and copy-on-edit protections continue; the selected logical feature is also protected. The byte budget changes by quality tier. | `renderQuality.hydroCacheBytes` |
| Quality | Mobile starts Medium; desktop starts High. Twenty frame samples per evaluation, two expensive windows to degrade, five comfortable settled windows to upgrade, 2500 ms cooldown. Explicit pan/pinch/wheel interaction changes phase revision. | `renderQuality.revision`, `p95FrameMs`, `p99FrameMs`, `longFrameCount` |

Budgets follow the pinned web profiles: packet cache 48/96/192 MiB,
terrain 32/64/128 MiB, hydro 40/72/96 MiB, interaction uploads
256/512/768 KiB and settled uploads 2/4/8 MiB for Coarse/Medium/High.
These values control resource scheduling. They are not product performance
pass/fail thresholds. Selected/edit geometry and canonical country base remain
high/exact in every tier.

## Known boundaries for the later gate

- The GPU upload budget is soft for a protected node and for one indivisible
  node larger than the configured budget. The geometry buffer byte estimate
  is reported; Qt Quick owns the actual GPU transfer.
- The Qt Quick scene graph manages backing GPU allocations. The profile's
  overlay GPU budget and DPR cap are exposed, but Qt does not currently use
  them as an allocation or offscreen resolution limit.
- Progressive uploads can show a partial new scene while pending. M7.6's
  last-good-scene contract needs atomic patch/promotion across those frames.
- Globe terrain still has no textured sphere channel; M7.4 only connected
  flat terrain. No terrain source level is lowered during interaction.
- Country culling is conservative at country bounds granularity; the pinned
  web source additionally supports optional spatial block ranges. These
  blocks are not present in the M7.4 mesh decoder.
- Automatic frame samples measure map `updatePaintNode` through frame swap
  when the GPU visual owner is active. CPU fallback and physical-device
  performance need separate measurement.

## Deferred checks

| Check | Status |
|---|---|
| Pinned Node Oracle versus C++ cases | NOT RUN |
| LOD/dateline/globe/hole structural suites | NOT RUN |
| Country culling and upload semantic suites | NOT RUN |
| World fit, Europe, Pacific, pole, hydro, distributions, rapid pan/zoom, globe and edit scenarios | NOT RUN |
| Fresh Qt Debug/Release build and full CTest | NOT RUN — Qt 6 development toolchain unavailable here; validation deferred by request |
| Windows GPU and Android physical device | NOT RUN |

M7.5 implementation is recorded here without a performance completion claim.
The later Codex gate must inspect the known boundaries, run the suites and
record actual hardware metrics before M7.5 can be declared verified.
