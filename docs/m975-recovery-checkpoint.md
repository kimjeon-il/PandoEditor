# M9.7.5 recovery checkpoint (work in progress)

This checkpoint restores the pre-reset M9.7.5 model-exchange work. It is not a completed parity gate or a release.

## Verified baseline and restoration

The baseline is `bdf5f979e262b6dd61e705c233f562a27264c6c1`, tree `804f51ce4165bdb7b97bbb574607cd1c47183503`. Source-only recovery run [37379934789](https://github.com/kimjeon-il/PandoEditor/actions/runs/37379934789) archived only its Git-tracked files. All 1,405 file paths, modes and Git blob hashes were checked during restoration. The archive SHA-256 is `37c918441c07366a05283a5f68add8311aca688d369e6bea0c00ec6c7219cfd2`.

The strict codec changes, persistence tests, native probe, and CMake additions match their recorded pre-reset SHA-256 values. The browser runner, native contract and suite likewise match their retained hashes. Other browser source was replayed from the original source writes. Generated fixture recovery is guarded by original source/input manifest hashes. Reconstructed diagnostics are historical source-derived bytes, not a fresh execution result.

The CI workflow integration was reconstructed from the verified baseline and its contract tests. It also now preserves job identity and status when runtime authentication or browser installation fails before the exchange runner starts. The temporary source-recovery workflow and its two-path trigger filter are removed in this product checkpoint; normal validation triggers are restored.

## Still pending

- Native v10 ownership provenance for imported original archive rows versus adapter-generated inline geometry; current native v9 reading and web v9 exchange remain required.
- Sticky semantic ownership through commit, Undo/Redo, persistence, and subsequent edits.
- Exact individual accounting for derived rows, including deleted adapters and identical shapes with distinct references.
- Actual Chromium/native bidirectional saved-file exchanges and final exact-HEAD full regression/parity CI.

The strict exchange comparator intentionally exposes the known archive growth from five original rows to eight after ordinary web/native/web exchange. This difference must be fixed by the approved ownership contract, not ignored, normalized, or treated as a pass. Historical focused tests are not substituted for fresh post-recovery runs. Existing stage 6/7 projection, engine extraction and final parity gaps remain open.

## Fresh focused verification after recovery

- Restored Qt 6.8.3 Debug build of `m975_model_exchange_probe` and `timeline_editor_persistence_tests`: completed.
- Native probe self-tests: 41 passed, 0 failed, 0 skipped after restoring the required input fixtures. An earlier run correctly failed one missing-file case before fixture materialization.
- Timeline/editor persistence tests: 70 passed, 0 failed, 0 skipped.
- Model exchange Node diagnostic suite: 50 passed, 0 failed, 0 skipped, including three workflow wiring tests. This is diagnostic execution, not a Chromium result.
- Independent workflow review verified original triggers, matching native artifact identity, and failure-evidence shell behavior. Actual Actions and complete exchange acceptance remain pending.
- Fresh compiled-native public action corpus: 10/10 request/result contracts verified. The unchanged strict oracle still exposes archive additions (for example 5→8 rows); this is reproduction of the pending defect, not exchange parity success.
