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

1. Generate small GIS-only and project GPKG examples with the pinned web
   implementation; preserve original SQLite bytes and inspect required tables.
2. Decode and encode the `GP` binary header, flags, 4326 SRS and WKB
   Polygon/MultiPolygon with an independent SQLite reader and the
   [OGC GeoPackage specification](https://www.geopackage.org/spec/).
3. Check ZIP central directory, paths, CRC and compressed size with an
   independent reader. Reject unsupported CRS unless the pinned web adapter
   demonstrates an explicit transform.
4. Confirm SQLite3 and ZIP dependency availability and packaging on both
   Windows and Android. Test malformed headers and allocation/cancel behavior.

No GIS import/export or native GeoPackage capability is claimed at this gate.
