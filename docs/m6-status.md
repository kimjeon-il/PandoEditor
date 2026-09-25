# M6 progress and open acceptance gates

M6 remains in progress. This branch is based on `codex/m5-completion` and is
kept as a draft PR. The M5 closure audit is postponed at the user's request;
neither branch is merged to `main`.

| Slice | State | Evidence and remaining work |
| --- | --- | --- |
| M6.0 pinned source | Implemented | SHA and fixtures in `docs/plans/m6-execution.md`; actual web functions drive Temporal, catalog and GIS target/plan Oracles. |
| M6.1 Temporal | Implemented and Linux verified | Common BCE/extended-year/date-precision parser, interval operations, `effectiveRelationAt`; Oracle and full CI. |
| M6.2 catalog | Implemented and Linux verified | Schema 2, historical pilot loader, M4 materialization, version/search/snapshot and child expansion. Incomplete source geometry is recorded. |
| M6.3 instantiation | Implemented and Linux verified for reviewed transactions | Typed origin/native v7, independent and territory-replacement batch/snapshot addition, country name updates, M4 difference and dependent geometry reconciliation, full country transfer with typed label/distribution/relationship redirection. One Confirm/Undo. Explicit ownership, partial approval and stale/cancel guards. Ambiguous full absorption and country assets that cannot be safely inherited are rejected before mutation. |
| M6.4 UI | Implemented and Linux offscreen verified; visible platform verification pending | Common desktop/360 px library panel with local file selection, filters, snapshots, boundary version and shape preview, source/approximation details, ownership choice with parent reset, impact preview and single-use confirmation. Read-only preparation runs off the GUI thread. |
| M6.5 GIS format gate | Partial | Stage 3 web GeoJSON ZIP/zlib reader; stage 4 pinned web worker GIS/project GPKG with independent SQLite oracle and read-only Qt SQLite/geometry loader. Full browser GDAL seed interchange, Windows/Android driver deployment and cancellation/allocation verification remain. |
| M6.6 GIS import | Partial | Stage 5 maps country/subunit/region GeoJSON, web ZIP and GeoPackage rows with explicit IDs and ownership through M4 geometry preparation. Stage 6 adds generic and language/ethnicity/religion distribution plans, preserving territorial references or free geometry, IDs and source properties. A shared desktop/360 px GIS panel separates file read, layer/field/ownership/coast mapping, impact preview and single-use confirmation; parse and plan run in background with session/revision guards. Candidate validation and one Confirm/Undo prevent partial live mutation. Project replacement, deeper GIS cancellation/allocation cases and visible platform validation remain. |
| M6.7 GIS and project export | Stage 8 implementation, fresh CI pending | Selected-layer GeoJSON ZIP and GIS-only GeoPackage remain separate. A project GeoPackage adds full native v7 settings, territorial provenance and binary embedded-flag assets alongside EPSG:4326 vector layers. The project open path checks the package and restores a candidate before replacing the live document; imported packages require a new JSON Save path. Background export rejects stale snapshots and keeps local writes atomic. Qt roundtrip/controller/UI cases and an independent Python SQLite oracle are registered. The web-produced project GeoPackage has a different `project_state` JSON shape; direct web project replacement and cross-application roundtrip are not claimed. |
| M6.8 integration | Partial | Stage 7 code head `c9a96ef` passed a fresh Linux configure/build and full CTest 74/74 with zero failures. Stage 8 project GeoPackage Qt and independent SQLite checks are registered, with fresh CI pending. The remaining web project replacement, allocation cases, visible Windows and physical Android still require verification. |

The 1·2회차 implementation passed the full Linux workflow at `0de7588` (run
`36099458471`, 60/60 tests, zero skips), including historical transaction,
controller, 1100/360 px QML, all registered prior Oracles, and full hydro
dataset integration. Windows and Android are not verified.

The GIS format gate cannot be closed by a codec self-roundtrip: inspect an
actual web-produced GeoPackage with an independent SQLite reader, compare its
geometry blobs, and verify the GIS-only versus project table contracts.

GIS export currently uses the existing 64 MiB storage boundary. A selected
GeoPackage containing unsupported generic MultiPoint geometry is rejected
before writing instead of dropping those features. Large-file throughput and
visible Windows/Android save-provider checks remain in later verification.

The stage 8 native project GeoPackage uses the web table names but stores the
complete Qt native v7 JSON in `pandolab_project_settings.project_state`.
`pandolab_country_assets` holds binary embedded flags and the settings row
retains default/none/embedded policy. The native reader validates country IDs
against the vector table and requires every embedded asset to match a symbol.
The pinned web fixture stores a web state projection instead. Opening it as a
whole project still needs an explicit web-to-native mapping and is left to
the remaining GIS project-replacement work.
