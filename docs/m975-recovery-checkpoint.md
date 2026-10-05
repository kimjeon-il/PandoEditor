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

Generated fixtures were reconstructed with exact original guards: v1 canonical manifest `6e7401e03d8fda69c634bce19a35e412c6b829c14daeac0fc5a2bfd4f1e8d931`, v2 canonical manifest `c0211b9af824c1a84d9a6b691eaa92802bb7bcbb42a222b6e0633afe9368f031`, v2 file SHA-256 `fbc90d137703b4647823b6c85157636baf44f73b1a687fe51083c6f555d0f6e7`, supplemental source manifest `0e7168846663110be98ae216baf7030d7d983756ad15113f4e54f269139dd104`. The four original preimage-error records hash to `cd7a61f8dbb2bb39cdc31603a84f9ab69809ca7819b2197dd687529b1d4b38b5`; their exact 53-byte stderr strings were reconstructed from the original throw/output statements. The retained v1 input archive hash is `a912ea04767ae650874c976d9870bfe2cd601321886da35673c14f30eed239c8`. These records document the original failures; the fresh native verification above uses the approved v2 inputs and does not claim to fix those earlier projection gaps.

A separate temporary data-only recovery workflow restores the existing full-regression dataset closure from the two already pinned Pando commits. Its immutable inventory contains 11 source files (30,878,542 bytes); files are checked against tracked path/mode/blob, size and available source SHA-256s, then uploaded as bounded source chunks. It does not build application or portable outputs. Remove the temporary fixture recovery workflow after verified restoration; it is not a product feature or a replacement for the normal regression gate.
