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
| M6.6 GIS import | Partial | Generic and territorial typed import plans. Stage 5 maps country/subunit/region GeoJSON, web ZIP and GeoPackage rows with explicit IDs and ownership. The M4 kernel prepares donor differences, country union and coastline choice against a detached snapshot. Candidate validation checks references, locks, country overlap and one Confirm/Undo. GIS wizard wiring, distribution mapping, coast decision UI, larger cancellation/allocation matrix and visible platform validation remain for later slices. |
| M6.7 GIS export | Partial | Read-only selected-layer GeoJSON materialization with territorial provenance, distribution references, labels. No ZIP, SQLite GeoPackage or project save/open yet. |
| M6.8 integration | Partial | Stage 4 code head `1752cf6` passed fresh Linux configure/build and full CTest 65/65 with zero failures or skips (run `36114997891`). Stage 5 adds a core and a Qt integration test; fresh full CI is required before claiming this slice verified. The remaining GIS work, corrupted file matrix, allocation cases, visible Windows and physical Android still require verification. |

The 1·2회차 implementation passed the full Linux workflow at `0de7588` (run
`36099458471`, 60/60 tests, zero skips), including historical transaction,
controller, 1100/360 px QML, all registered prior Oracles, and full hydro
dataset integration. Windows and Android are not verified.

The GIS format gate cannot be closed by a codec self-roundtrip: inspect an
actual web-produced GeoPackage with an independent SQLite reader, compare its
geometry blobs, and verify the GIS-only versus project table contracts.
