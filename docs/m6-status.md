# M6 progress and open acceptance gates

M6 remains in progress. This branch is based on `codex/m5-completion` and is
kept as a draft PR. The M5 closure audit is postponed at the user's request;
neither branch is merged to `main`.

| Slice | State | Evidence and remaining work |
| --- | --- | --- |
| M6.0 pinned source | Implemented | SHA and fixtures in `docs/plans/m6-execution.md`; actual web functions drive Temporal, catalog and GIS target/plan Oracles. |
| M6.1 Temporal | Implemented and Linux verified | Common BCE/extended-year/date-precision parser, interval operations, `effectiveRelationAt`; Oracle and full CI. |
| M6.2 catalog | Implemented and Linux verified | Schema 2, historical pilot loader, M4 materialization, version/search/snapshot and child expansion. Incomplete source geometry is recorded. |
| M6.3 instantiation | Partial | Typed origin/native v7, independent batch and independent snapshot addition with one Undo, explicit ownership and partial approval. Territory replacement, `countryUpdates` and complex child reconciliation remain. |
| M6.4 UI | Not implemented | Common desktop/mobile library search, preview, ownership and confirm flow remain. |
| M6.5 GIS format gate | Partial | GIS target/plan Oracle, 2D 4326 GeoPackageBinary/WKB codec. Actual web-generated GPKG, ZIP, SQLite dependency and Windows/Android packaging gate remain. |
| M6.6 GIS import | Partial | Strict local GeoJSON FeatureCollection parser and corruption cases. Generic-only typed import plan, candidate validation and one-Undo command are implemented locally; latest CI is pending. Territorial/distribution mapping, wizard, ZIP and GPKG reader remain. |
| M6.7 GIS export | Partial | Read-only selected-layer GeoJSON materialization with territorial provenance, distribution references, labels. No ZIP, SQLite GeoPackage or project save/open yet. |
| M6.8 integration | Partial | The last fully pushed commit (`c05d4540`) passed fresh Linux configure/build and 57/57 CTest with no skips. The new generic import has only a local core test until its CI run. Corrupted file matrix, stale/abort/allocation cases, 1100/360 UI, visible Windows and physical Android remain. |

The selected-layer GIS export and partial-source changes passed the full Linux
workflow at `c05d4540` (run `36082490210`, 57/57 tests, zero skips). This
verifies the implemented path on Linux; no Windows or Android claim follows.

The GIS format gate cannot be closed by a codec self-roundtrip: inspect an
actual web-produced GeoPackage with an independent SQLite reader, compare its
geometry blobs, and verify the GIS-only versus project table contracts.
