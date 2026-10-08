# Pinned world-map data

The compressed country preview, PCG1 canonical country packet and two CMG
meshes are unmodified Git blobs from `world-map@c0bd31d13dc8495593d78cf51f7cc195de7c9469`.
The source country geometry is Natural Earth 5.1.1 Admin 0 at 1:10m.
The country label anchors and hydro 0.13.1 manifest are now pinned to
`kimjeon-il/Pando@a87f4d27fb1bc16528aa57242ecb52e28e4550b2` (correct BJN/SER positions
and updated hydro core-metadata integrity values). Other bundled world assets
retain their original c0bd31d1 source identity. The physical inventory records
explicit source URLs for the two updated hydro files while its unchanged assets
continue to use the older pinned commit.
The terrain manifest describes Natural Earth raster 3.2.0 at 1:10m; optional
tiles and the hydro 0.13.1 shards must be installed separately with their own
manifest hashes. The app must never substitute another dataset silently.

The exact blob and SHA-256 identities are in `manifest.json`. See the upstream
world-map repository's Natural Earth and hydro notices for source attribution.

The optional DEM relief assets are unmodified Git blobs from
`kimjeon-il/world-map-terrain-v0.13.0@c3c18d167dae2dd9639844e5174dd68f745c6832`.
Its 0.13.3 manifest and tint accompany 1,280 DEM tiles in `terrain/v0.13.0`.
The DEM source is NOAA ETOPO 2022; the color tint is Natural Earth raster.
The exact per-file Git blob IDs, sizes and SHA-256 hashes are recorded in
`terrain-dem-provenance-c3c18d1.json` and
`physical-inventory-terrain-dem-c3c18d1.json`. These optional data remain
separate from the pinned world/hydro inventory. A 1 m encoded height spacing
is the storage interval, not the accuracy of the original elevation source.

## Source-aware bundle synchronization (stage 4)

The authoritative native asset contract is `assets/world/manifest.json` schema 2.
It contains per-asset local path, exact stored-byte SHA-256, Git blob SHA-1,
byte size, required policy and original upstream commit/path. No runtime file
may be chosen from GitHub's moving main branch.

The current installed world is still the **legacy mixed snapshot**:
country preview/meshes/canonical packet and terrain from Web commit
`c0bd31d13dc8495593d78cf51f7cc195de7c9469`;
corrected BJN/SER anchors and hydro manifest from
`a87f4d27fb1bc16528aa57242ecb52e28e4550b2`.
No map geometry or optional physical dataset was changed during this migration.

`node tools/sync-world-data.mjs --web-ref <40-character-commit>` performs a
read-only comparison against Web `world/current.json` and content-addressed
files. It validates every prospective file and reports the exact bundle SHA-256.
To apply an explicitly reviewed change, pass both
`--apply --approve-bundle <the-reported-64-character-SHA-256>`.
Changes to the pinned hydro/terrain manifests abort the apply until their
separate physical inventory and optional data layout are reviewed.
Updating the source manifest alone is not permitted. The old assets are not
deleted; the Qt resource list is derived from the approved manifest at build
configuration time. A new application build is required after an approved
change. The offline bundled world works without a network connection.
