# M6 progress and open acceptance gates

M6 remains in progress. This branch is based on `codex/m5-completion` and is
kept as a draft PR. The M5 closure audit is postponed at the user's request;
neither branch is merged to `main`.

| Slice | State | Evidence and remaining work |
| --- | --- | --- |
| M6.0 pinned source | Implemented | SHA and fixtures in `docs/plans/m6-execution.md`; actual web functions drive Temporal, catalog and GIS target/plan Oracles. |
| M6.1 Temporal | Implemented and Linux verified | Common BCE/extended-year/date-precision parser, interval operations, `effectiveRelationAt`; Oracle and full CI. |
| M6.2 catalog | Implemented and Linux verified | Schema 2, historical pilot loader, M4 materialization, version/search/snapshot and child expansion. Incomplete source geometry is recorded. |
| M6.3 instantiation | Implemented for reviewed transactions; Linux verification pending | Typed origin/native v7, independent and territory-replacement batch/snapshot addition, country name updates, M4 difference and dependent geometry reconciliation, full country transfer with typed label/distribution/relationship redirection. One Confirm/Undo. Explicit ownership, partial approval and stale/cancel guards. Ambiguous full absorption and country assets that cannot be safely inherited are rejected before mutation. |
| M6.4 UI | Implemented; visible platform verification pending | Common desktop/360 px library panel with local file selection, filters, snapshots, boundary version and shape preview, source/approximation details, ownership choice with parent reset, impact preview and single-use confirmation. Read-only preparation runs off the GUI thread. |
| M6.5 GIS format gate | Partial | GIS target/plan Oracle, 2D 4326 GeoPackageBinary/WKB codec. Actual web-generated GPKG, ZIP, SQLite dependency and Windows/Android packaging gate remain. |
| M6.6 GIS import | Partial | Strict local GeoJSON FeatureCollection parser and corruption cases. Generic-only typed import plan, candidate validation and one-Undo command are implemented locally; latest CI is pending. Territorial/distribution mapping, wizard, ZIP and GPKG reader remain. |
| M6.7 GIS export | Partial | Read-only selected-layer GeoJSON materialization with territorial provenance, distribution references, labels. No ZIP, SQLite GeoPackage or project save/open yet. |
| M6.8 integration | Partial | Earlier M6 baseline passed 58/58 fresh Linux CTest. The first 1·2회차 CI build succeeded but four UI test executables failed because of a QML layout property; the property is fixed and the full CI rerun is pending. Corrupted file matrix, allocation cases, visible Windows and physical Android remain. |

The M6 baseline passed the full Linux workflow at `61220ae` (run
`36095566600`, 58/58 tests, zero skips). New 1·2회차 acceptance evidence is
tracked by the current draft PR workflow; Windows and Android are not verified.

The GIS format gate cannot be closed by a codec self-roundtrip: inspect an
actual web-produced GeoPackage with an independent SQLite reader, compare its
geometry blobs, and verify the GIS-only versus project table contracts.
