# M2 web import implementation plan

**Goal:** Import actual pandolab-project-state / pandolab-autosave-full schema 3–5 without substituting features or losing unsupported data.
**Base:** PandoEditor 91dbd31313922cdd98b89318dc4a077e73b802e2; web 58e4087f85aa51884bc4ab80959e05010d94d7d5.
**Architecture:** lossless JSON boundary → source-equivalent migrations → typed v3 plus retained fragments → immutable report/candidate → guarded atomic session replacement. No changes to web, main or distributions. Qt file I/O remains separate from web import.

- [x] M2.1–2 tests: explicit formats, full/delta, 3→4→5 golden output from original JS, flags and legacy country override priority, object key/style/order/visibility migration, nested opaque JSON precision, malformed/future/duplicate keys.
- [x] M2.1–2 implementation: app/webmigration and lossless helpers. Original JS oracle runs separately; expected fixtures never generated from C++ output.
- [x] M2.3 tests: countries, nested units and dated relations, duplicate/dangling/cycle failures, geometry/ID preservation, default/none/embedded flags, every root classified, unknown references/absent datasets reported; Qt save/reopen preserves payload exactly.
- [x] M2.3 implementation: app/webimport mapping/report and capability barriers. Missing dependencies are never substituted. Retained objects are not claimed as rendered/editable.
- [x] M2.4–5 tests: preparation/cancel/failure leave project/drafts/selection/revision/history unchanged; stale report after edit/undo/reopen; wrong candidate hash; double confirm; save failure; successful import new session/history; same PC/mobile result; source file cannot be overwritten by import.
- [x] M2.4–5 implementation: shared EditorController async import and common report dialog/menu entry. Save-before-import stages current draft in a temporary candidate before I/O, leaving live state untouched on failure.
- [x] Regression: baseline/new CTest, sanitizer core, actual desktop/360px QML and retained roundtrip; update roadmap and implementation evidence with explicit platform limitations.

## Interface contracts

`webimport::classify(bytes)` returns QtProject or WebFull only; throws `BASE_DATA_REQUIRED` for delta and structured codes for unsupported/corrupt input.
`webimport::migrate(bytes)` returns lossless normalized schema5 bytes and original schema/format; no live state.
`webimport::prepare(bytes, cancelled)` returns a complete document/report/source hash/candidate hash; throws before any state changes.
`EditorController::prepareWebImport(url)` loads asynchronously without committing drafts. `confirmWebImport(hash, disposition, saveUrl)` accepts only the reviewed candidate and original edit context. `cancelWebImport()` invalidates pending workers/report, not drafts. Supported dispositions are cancel/discard/save and never implicit discard.
