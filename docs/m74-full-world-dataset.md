# M7.4 — pinned full-world runtime implementation

## Source and package contract

The bundled country assets, plus the original terrain manifest, are the exact Git
blobs from `world-map@c0bd31d13dc8495593d78cf51f7cc195de7c9469`.
The label-anchor correction and current hydro manifest come from
`kimjeon-il/Pando@a87f4d27fb1bc16528aa57242ecb52e28e4550b2`:

| Channel | Asset | Git blob |
|---|---|---|
| Preview country display | `countries-preview-v0.33.0.geojson.gz` | `ce5eecd8f4e1a611131f2117780c34ca3cf555b2` |
| Preview GPU base | `world-mesh-preview-v0.33.0.bin.gz` | `caff1ab319706f6bf90635ed5a992ffbf4f2a84a` |
| Canonical country document | `countries-canonical-v0.33.0.pcg.gz` | `54146d9eeb28e4af08e094f5061bf64689e6bdf3` |
| Canonical GPU base | `world-mesh-v0.12.6.bin.gz` | `8c73420b92e89ab64cbe75dcc5016efe2c0a22b6` |
| Terrain package manifest | `terrain/v0.12.6/manifest.json` | `6821c49315ffd381758f81dfe4d83b6b574f0ad4` |
| Hydro package manifest | `hydro/v0.13.1/manifest.json` | `9a9cdb719351e51d2ff509c410cae8741ac36a0a` |

`assets/world/manifest.json` also pins the SHA-256 bytes and versions.
`node tools/verify-world-assets.mjs assets/world` checks these six identities
without a world-map checkout. Four compressed assets are embedded in the app's
Qt resource; the two physical-data manifests are embedded as identities.
Terrain tiles and hydro shards are **not** bundled. The physical inventory keeps
original sources for unchanged assets and uses pinned per-asset `sourceUrl`
overrides for the refreshed hydro manifest and core metadata.

The canonical PCG1 packet contains 258 countries and 548,454 exact Float64
positions. CMG2 mesh coordinates are render-only Int32 microdegrees. The
preview mesh has 105,884 source positions and carries display-only unwrapped
longitudes; neither mesh replaces document geometry.

## Startup and replacement

The application executable opts into world bootstrap; the small sample fixture
remains available to the existing controller test setup. Preview mesh and
preview display paths are built in a worker. A second worker materializes the
PCG1 packet into a validated `ProjectDocument` and prepares CPU fallback
paths. The GUI replaces the new project's document once, after preparation.
Canonical mesh decoding follows in a worker. Until it completes, GPU display
retains the preview mesh and defers country override display. Promotion
publishes one immutable scene containing the canonical base plus edits made to
the canonical document in the meantime.

Opening a user project or confirming a web import invalidates pending world
worker generations and removes the built-in world base. A late result cannot
replace the opened document. Preview data is never an editable or saved
`ProjectDocument`.

The scene graph draws country-index ranges directly from the base mesh;
initial canonical countries are omitted from ordinary polygon packets.
Country geometry changes with newer `GeometryRef` versions become typed
overrides. CPU fallback keeps its independent QPainter paths.

## Optional physical data

`PANDOEDITOR_WORLD_DATA_ROOT` may name an installed data package containing
`terrain/v0.12.6/manifest.json`, its level directories, and
`hydro/v0.13.1/manifest.json` with the referenced shards and metadata. The
installed manifests must match the embedded pinned bytes. Missing or mismatched
hydro remains unavailable; the existing `HydroRuntimeProvider` is opened only
for a complete matching package on a new world project. A user's configured
hydro source in an opened project is preserved.

Flat terrain uses viewport-selected Qt Quick texture tiles. Missing tiles are
omitted and counted in `terrainDataStatus`. Globe terrain texturing is not
connected in this implementation; flat rectangles are hidden when the view
switches to Globe. Optional terrain also does not participate in picking or
canonical country geometry.

## Execution status

This environment has no Qt 6, CMake, Ninja, qsb, visible Windows session, or
Android device. The C++ build, CTest, Qt Quick rendering, Windows backend,
Android package, and physical-dataset operation have **not been run**. M7.4
cannot be called validated or closed here. GPU screenshot parity, globe
terrain, and full package behavior remain for the later validation/implementation
pass. No integration or main merge is part of this step.
