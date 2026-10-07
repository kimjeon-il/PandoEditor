# Immutable latest Web territorial catalog

Source commit: `ebcfae4d27b29cbbea6416a7045a4806930204be`.

`assets/territorial-library-v2/` contains the exact index and 284 gzip Git blobs, copied without newline conversion. The index has 284 entities, 262 lineages and two snapshots. It is 555,560 bytes with SHA256 `63c072095fd6c95034365f6d7f9d4e4ff99f71684890bffc8e678fe741b4334c`; compressed chunks total 6,350,850 bytes. The source manifest records 299 exact Git blobs including the catalog, modules, build metadata, assembly and reference tests. Its SHA256 is `1f429460614e1e392e0a3c1c880decd6c45e6c5b9a3a540cea5dd2828bedb0fe`.

`source/` is immutable reference material. `web-oracle.test.mjs` supplies local fake fetch responses to the original source loader; its all-284 test checks gzip/hash/decode and indexed metadata matching through that source. This is a Node oracle, not native Qt, UI, browser or rendering evidence. Some copied reference tests require additional original source-data helpers and are retained for provenance only; the runnable isolated command is:

```powershell
& 'C:/Program Files/nodejs/node.exe' --test --test-reporter=tap tests/fixtures/web-territorial-library-v2/web-oracle.test.mjs tests/fixtures/web-territorial-library-v2/source/tests/unit/territorial-library.test.mjs tests/fixtures/web-territorial-library-v2/source/tests/unit/territorial-library-service.test.mjs
```

The forthcoming native target links `Pandoeditor::Core`, `Qt6::Core` and `ZLIB::ZLIB`, compiles `app/territoriallibrarycatalog.cpp`, includes `app/`, and passes the asset directory as its sole argument. Controller/QML integration and resource registration belong to the parent task.

The native reader requires the index SHA before parsing and bounds stored/decoded assets at 64 MiB each. Every lazy chunk must match indexed stored length and SHA, pass gzip CRC/ISIZE with exact decoded length and full input consumption, parse valid UTF-8 and duplicate-free JSON, pass the existing structural geometry validator, and match normalized index identity/metadata before entering the cache. As in the exact Web loader, `sourceInfo` is omitted from the metadata comparison because the chunks contain richer records; its bytes are protected by the indexed stored SHA. Failed requests leave no cache entry and can retry. The handwritten native cases include correctly rehashed malformed input so decoder and semantic checks are exercised independently of the SHA guard.

Catalog search is grouped by lineage, filters entity lifetime and keeps alive entities whose geometry version is missing with `selectedVersionId: null`. Preview and instantiation reject that gap explicitly. The reference date retains its given precision; year/month cursors denote their end. There is no nearest-date lookup. Search/version inspection loads no geometry; preview and descriptors load only the requested verified chunks.

Descriptors preserve the exact source `entityId`, `geometryVersionId`, `parentEntityId`, localized name, geometry, instantiation mode and provenance. Their runtime `validFrom`/`validTo` are null; source intervals remain under metadata. The integration owner must create fresh logical/project geometry IDs, map descriptor `entityId` to model6 `sourceEntityId`, resolve source-parent ownership without inventing ancestors, apply requested territory-replacement semantics through existing preview/confirmation commands, and commit through one authorized transaction. This catalog never edits a project or history. UI stale request/project ownership, flag URL resolution, live preview rendering, undo/reopen/exchange and storage validation remain separate integration responsibilities.
