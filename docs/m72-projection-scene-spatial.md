# M7.2 projection / scene / spatial implementation status

Status: **in progress**. This branch is
`codex/m7-world-rendering`; do not merge it into `codex/integration` until the
entire M7 is implemented and verified. Validation is deferred to a later Codex
session after the full M7 implementation.

## Inputs and ownership

- `tests/fixtures/world-rendering` pins `world-map@c0bd31d1` and preserves
  13 real Natural Earth countries, actual river/lake geometry and separate
  synthetic DATELINE/POLAR controls. M7.2 has not changed this corpus.
- `tests/fixtures/web-m72` pins upstream render modules by Git blob and an
  offline Node oracle. Expected world-copy offsets and packet/cache/order
  behavior come from those pinned modules.
- `ProjectDocument` and `GeometryStore` retain EPSG:4326 doubles. Derived
  float geometry packets unwrap longitude locally and carry view-independent
  unit-sphere positions without editing the source.
  Flat and Globe share those references. Geometry packet keys omit view,
  selection, style and opacity.
- `MapSceneBuilder` resolves existing `mapRenderOrder()` and publishes typed
  `RenderScene` snapshots via `MapSceneBridge`. Scene preparation currently
  runs synchronously on the GUI owner thread. The current QPainter path stays
  the production visual owner; a typed-scene CPU paint entry point is optional.

## Current implementation

| Area | Contract implemented | Remaining risk |
|---|---|---|
| Bounds/index | Minimal circular geographic bounds, split dateline parts, 5° grid, revisioned geometry/dataset rebuild, deduplicated candidates | Geographic bounds are conservative; exact hit testing is still authoritative. The existing flat picker uses a raw-bounds supplement for unsplit dateline paths. |
| Projection | Flat inverse and world copies; orthographic Globe front/back and inverse; polar-plane indices for cap geometry | Globe limb clipping remains incomplete. |
| Packets | Typed numeric buffers, hole-aware earcut `v2.2.4`, separate primitive/style/identity, view-independent cache | Seam continuity and complex polygon fill remain rendering risks. |
| Scene | Six revision domains, M5 draw key, immutable snapshots, mixed domain material, cache reuse | Labels still use existing layout/safe-area behavior in production. |
| Bridge/CPU | Atomic immutable scene publication, mutex-protected view state, optional QPainter typed adapter | Current QML still uses the legacy `QVariant` path. |
| Picking | Geographic candidates feed existing visibility and exact hit tests; chooser order remains the existing code | Globe screen-to-candidate seam is not yet wired into the legacy QML pick coordinates. |

The typed packets contain no `ProjectDocument*`, GeoJSON, `QVariantMap`,
`QVariantList` or SVG-like `QString` paths. The old CPU path retains its existing
transport until the renderer migration is verified; it is not the M7.3 GPU
transport.

M7.3 consumes `MapSceneBridge` and typed packets. M7.4 owns all 258 countries
at runtime; M7.5 owns culling, LOD and performance targets.
