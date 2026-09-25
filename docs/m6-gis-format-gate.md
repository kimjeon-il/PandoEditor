# M6 GIS format gate (in progress)

Pinned web revision: `c0bd31d13dc8495593d78cf51f7cc195de7c9469`.
The adapter requested as `gis-adapters.js` lives at
`assets/js/gis-adapters.js` (blob
`be606ee43b39b1b1ddc15dc166908f966217f960`), outside
`assets/js/modules/`. Its byte-identical copy is in the M6 fixture.

The pinned browser GIS test (blob `c5deb098a9b2be56266a12fc7289cda53c418a9a`)
reads a produced GeoPackage with SQLite and asserts:

| Mode | Table/field evidence |
| --- | --- |
| Project GeoPackage | `countries`, `territories`, `administrative`, `regions`, three distribution tables, `pandolab_project_settings`, `pandolab_country_assets` |
| GIS-only GeoPackage | Selected vector tables, without `pandolab_project_settings` or `pandolab_country_assets` |
| Territorial fields | `pandolab_id`, `pandolab_name` among country columns |
| Distribution fields | `entry_id`, `layer_id`, `source_mode`, `territorial_unit_id`, `share`, `certainty` |
| CRS | `gpkg_geometry_columns.srs_id=4326` in its distribution assertion |
| Asset roundtrip | A `None` flag remains null; an embedded SVG is restored from `pandolab_country_assets` |

The pinned east-Prussia GeoJSON input (blob
`e263faa8aad38c18de4263ca8655bc3c20003b4b`) is also copied
byte-for-byte. The browser test creates GeoPackage output at runtime;
there is no static `.gpkg` file at the guessed fixture path.

Proposed dependency is SQLite3 plus a portable ZIP reader/writer. This is a
**candidate**, not an approved codec yet. Before linking it into the product:

The core now has a dependency-free, 2D EPSG:4326 GeoPackageBinary/WKB codec
with roundtrip and malformed-byte tests. This is an incremental format check,
not evidence that a whole GeoPackage produced by the web opens in Qt.

### Stage 3 ZIP evidence

The pinned web `gis-io.js` blob `4d62291181721ba15f94f35c202f6acf6d58f01a`
and web `fflate.min.js` blob `df762372e3ff721a72a8e9b5cf1e58f2f3ee4ea8`
are copied into the fixture directory. `node tools/m6-gis-zip-fixture.mjs`
calls the real `PandoLabGIS.exportGeoJsonBundle` with a fixed clock. Its
`web-gis-geojson.zip` has five selected layers (countries, subunits,
generic, language distribution, labels) and `manifest.json` with
`pandolabExport=true`, schema 3 and EPSG:4326. An independent Python
`zipfile.testzip()` validates its six deflate entries and CRCs.

`readGisZipArchive` parses central directory, raw deflate, CRC and safe
paths in core. `parseGisGeoJsonZip` reads manifest and GeoJSON without
changing the live document; it preserves targets, distribution type and
feature properties for later import mapping. The reader rejects damaged
offsets, signatures, sizes, paths, CRCs, compression flags, manifest
schema/CRS, missing declared layers and count mismatches. Plain GeoJSON
ZIPs can be inspected with an unspecified target. The existing zlib
dependency handles ZIP decoding without adding a new library.

1. Generate small GIS-only and project GPKG examples with the pinned web
   implementation; preserve original SQLite bytes and inspect required tables.
2. Decode and encode the `GP` binary header, flags, 4326 SRS and WKB
   Polygon/MultiPolygon with an independent SQLite reader and the
   [OGC GeoPackage specification](https://www.geopackage.org/spec/).
3. ZIP central directory, paths, CRC and size are checked with the web
   fixture and corrupt variants. The native reader rejects unsupported CRS;
   the web GDAL transformation path belongs to the later import slice.
4. Confirm SQLite3 availability and packaging on Windows and Android before
   linking a GeoPackage reader. ZIP uses existing zlib. Cross-build,
   cancellation and allocation failure verification remain open.

The GIS exchange plan/target Oracle and a strict local GeoJSON feature
collection adapter are implemented separately. Neither is connected to the
GIS import wizard or the document transaction. The ZIP reader is read-only
and is not yet wired to the wizard. SQLite handling, actual web-generated
GPKG inspection and Windows/Android packaging remain open. No end-to-end
GIS import/export or project GeoPackage capability is claimed at this gate.
