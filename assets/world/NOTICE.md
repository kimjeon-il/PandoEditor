# Pinned world-map data

The compressed country preview, PCG1 canonical country packet and two CMG
meshes are unmodified Git blobs from `world-map@c0bd31d13dc8495593d78cf51f7cc195de7c9469`.
The source country geometry is Natural Earth 5.1.1 Admin 0 at 1:10m.
The terrain manifest describes Natural Earth raster 3.2.0 at 1:10m; optional
tiles and the hydro 0.13.1 shards must be installed separately with their own
manifest hashes. The app must never substitute another dataset silently.

The exact blob and SHA-256 identities are in `manifest.json`. See the upstream
world-map repository's Natural Earth and hydro notices for source attribution.
