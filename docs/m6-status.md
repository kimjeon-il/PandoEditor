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
| M6.5 GIS format gate | Partial | Stage 3 web GeoJSON ZIP/zlib reader; stage 4 pinned web worker GIS/project GPKG with independent SQLite oracle and read-only Qt SQLite/geometry loader. Full browser GDAL seed interchange and Windows/Android driver deployment remain unverified. |
| M6.6 GIS import | Linux integration verified for tested cases | Stage 5 maps country/subunit/region GeoJSON, web ZIP and GeoPackage rows with explicit IDs and ownership through M4 geometry preparation. Stage 6 adds generic and language/ethnicity/religion distribution plans, preserving territorial references or free geometry, IDs and source properties. A shared desktop/360 px GIS panel separates file read, layer/field/ownership/coast mapping, impact preview and single-use confirmation; parse and plan run in background with session/revision guards. Candidate validation and one Confirm/Undo prevent partial live mutation. A dedicated allocation sweep covers generic, distribution and territorial GIS plan creation plus preview and commit; the core executable passed locally and in CI. Windows/Android operation is unverified. |
| M6.7 GIS and project export | Stage 10 Linux integration verified for tested cases | Selected-layer GeoJSON ZIP and GIS-only GeoPackage remain separate. A project GeoPackage adds native v7 settings, territorial provenance and binary embedded-flag assets alongside EPSG:4326 vector layers. The project open path recognizes the distinct web `project_state`, reconstructs countries from the vector table, aligns the other layers by ID, restores flags from the asset table and validates the candidate before replacement. Native and web packages open through the same controller without making the GPKG an ordinary JSON Save target. The pinned worker fixture and a full-schema web state, three flag policies, corrupt assets and web-to-native roundtrip passed Linux regression. |
| M6.8 integration | Stage 10 Linux regression verified | Code head `4f5ae4f` passed a fresh Linux configure/build, full CTest **79/79** and the M3–M6 regression audit with zero failures and zero skips. The audit requires GIS allocation failure and Oracle/UI gates. Corrupt GeoJSON/ZIP/GPKG, invalid CRS and stale GIS confirmation retain the live document/selection/revision. Visible Windows/physical Android verification remains unavailable in this environment. |

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
The pinned web fixture stores a web state projection instead. The stage 10
reader reconstructs its missing `countriesData` from the 4326 vector table,
cross-checks every other supported vector ID against its saved state and
restores embedded flags from `pandolab_country_assets`. The existing web
importer validates and maps the resulting candidate. Unrecognized vector
layers, mismatched IDs/source information, malformed assets and any incomplete
promotion reject the package before the controller replaces the live project.
The web writer omits `countriesData` from project settings and places only
country ID, effective name, validity and geometry in the vector table. Other
original country feature properties are absent from the file and cannot be
recovered by the native reader; this is a limitation of the source package.

Stage 8 code commit `a9477b1` passed [Linux workflow 36133556179](https://github.com/kimjeon-il/PandoEditor/actions/runs/36133556179):
fresh configure/build, full CTest **77/77**, zero failures. This includes
the independent Python SQLite oracle, missing-asset and mismatched-country
rejection, controller no-mutation checks, and offscreen 1100/360 px UI.

Stage 9 code commit `d60a288` passed [Linux workflow 36136750879](https://github.com/kimjeon-il/PandoEditor/actions/runs/36136750879):
fresh configure/build, pinned hydro dataset, full CTest **78/78**, zero
failures and **zero skips**. The separate regression audit checks the JUnit
result and Qt test log for missing, failed or skipped M3–M6 gates. The new GIS
failure matrix verifies that bad JSON/ZIP/SQLite/CRS inputs and stale Confirm
do not mutate the document, selection or revision.

Stage 10 code commit `4f5ae4f` passed [Linux workflow 36143642019](https://github.com/kimjeon-il/PandoEditor/actions/runs/36143642019):
fresh configure/build, pinned hydro dataset, full CTest **79/79**, zero
failures and **zero skips**. The web-produced project GPKG fixture, full-schema
web state, flag policy and corruption checks, web-to-native GPKG roundtrip,
controller no-mutation check and GIS allocation failure sweep are included.

This execution environment is Linux and has no Windows GUI, Android SDK or
physical Android device. Windows visible operation, Android Storage Access
Framework and QSQLITE packaging are unverified. Full browser GDAL seed
interchange also remains unverified. Keep the PR in draft and do not merge
into `main` while those acceptance items are open.
