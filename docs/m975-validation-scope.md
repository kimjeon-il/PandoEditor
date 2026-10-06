# M9.7.5 validation scope and remaining limits

## Acceptance rule

The implementation requires the normal, exact-commit GitHub Actions regression and actual-Chromium exchange gates. A local diagnostic pass or this document is not a final acceptance result. The UI suite retains its original 180-second limit; no warnings, tests, comparisons, or cases are suppressed.

## Exact-candidate CI and presentation follow-up

The [2026-10-06 run for feab14b8](https://github.com/kimjeon-il/PandoEditor/actions/runs/37391751350) passed the complete native regression: 163/163 tests, zero failed/skipped/disabled, in 1096.66 seconds. The original UI gate passed in 173.975 seconds within its unchanged 180-second limit. All nine non-model-exchange jobs passed. Independent checks reconciled the downloaded JUnit and LastTest records against all 163 CTest registrations.

The actual Chromium model-exchange job failed on the first Undo checkpoint whose explicitly empty `itemVisibility` groups were lost by native decoding/normalization. This candidate is therefore not accepted. The failed artifact and its complete Chromium checkpoint report remain unchanged.

A subsequent diagnostic exercised all 100 checkpoints with 111 invocations of that exact CI binary, using the 51 captured Chromium saves and the pinned production readers/serializer in Node. It found 80 passing checkpoints and 20 failures: eleven lost empty visibility groups, eight rejections of retained object-order hints for deleted entities, and one absent `overlayOrder` emitted as an empty array. All 49 native-origin checkpoint returns passed. This diagnostic is not actual Chromium reopen acceptance; it batches the three representation fixes before the next complete exact-commit CI run.

The corrective candidate preserves empty-group and optional-field presence, accepts unique well-formed territorial object-order rank hints without treating them as live typed references, and keeps deliberate cleanup for actual native edits. Typed object styles, label settings, and content references remain subject to their existing ownership validation. No-op requests must not trigger that cleanup. Files written by the corrected candidate with an absent native `overlayOrder` are not forward-readable by the earlier feab14b8 candidate, which required that field; native v9 input compatibility remains supported. Native v10 is still an unreleased candidate, and web exchange remains v9.

The corrected local probe passed all 100 diagnostic checkpoints and 111 native invocations with zero mismatches. The final focused native rerun passed 16/16 suites, including presentation, persistence, migration, ownership, command-allocation/history, boundary cleanup, and the exchange-probe contract. Independent source review found no blocking defects. These focused and Node-assisted results do not replace the required final exact-commit full regression and actual Chromium exchange.

## Focused verification before the final CI candidate

- Native provenance core: 21 cases and 13 focused CTest targets passed.
- Native codec: 55 cases passed, including real saved-file 5→8→5 archive preservation, v9 input, repeated native/web saves, malformed provenance, equal-shape identities, unsafe opaque values and invalid creator IDs.
- Persistence integration: 81 Qt cases and additional native suites passed, covering actual QFile/controller save, immutable autosave workers, native GeoPackage assets/markers, invalid activation atomicity and rich timeline storage with static-only activation unchanged.
- Browser/native accounting diagnostics: 74 Node cases, all 49 actual native saved-stage returns, and 11 independent adversarial checks passed. These native replays are not substituted for actual Chromium.
- Existing M9.7.4 source-order/history native-version consumers: 25 Node cases passed; original v9 cases and pinned source fixtures remain intact.
- A genuine native-v10 save was rejected by the unchanged old v9 reader with `UNSUPPORTED_VERSION: expected Qt v9`, confirming the disclosed compatibility boundary.

## First complete local regression

The full build succeeded. The unfiltered local run registered and accounted for exactly 163 tests: 161 passed, two failed, zero skipped/missing/disabled. This run is retained as a failed run, not relabeled after focused reruns.

1. `m6_project_gpkg_sqlite_oracle` still expected current native saves to have version 9. Its one-literal assertion was corrected to 10 after the full run, and the exact SQLite/actual-writer oracle then passed unchanged otherwise.
2. `ui_tests` exceeded its unchanged 180-second budget and also reported a mobile annex method-confirmation assertion failure. These are separately investigated facts.

## Pointer-test correction and unresolved input behavior

The original pointer test could pass even when one of its two map taps was rejected during `selectionPending`. Instrumentation found this in 13/14 passing desktop/mobile rows. A new exact two-vertex assertion failed with actual one, then passed after adding the same ready-state precondition used later in the test. All original confirmation/document/Undo assertions remain; taps are not retried. The corrected desktop/mobile rows and all four cases cut off by the whole-suite timeout passed in focused execution.

The same strict two-point assertion failed on the pre-v10 baseline too. The production pending-input behavior was not changed. Its reproduction, cause and required actual-web comparison remain open for M9.7.6/7 in `m975-current-model-bridge.md` and the earlier source-history integration limits.

## Same-environment timeout comparison

A separate clean checkout/build of the exact pre-v10 `395c1d70def1443028c5317a68820c90b4c509f8` source used matching Debug/toolchain configuration and canonical-world assets. Its complete original UI suite also exceeded the unchanged 180-second limit, with 40 reported passes and no reported assertion failure before termination. Its canonical world case took 53.521 seconds, versus 55.109 seconds in the current diagnostic run; pointer pairs took 25.328 and 25.311 seconds respectively.

This establishes a pre-existing cumulative-budget limitation on this local machine, not a passing local whole-suite result. On the actual CI runner, the same pre-v10 source passed the original UI gate in 162.03 seconds and its full 161-test regression passed. The final candidate must still pass that original CI gate and the complete actual-browser exchange gate; baseline timing is not an exemption from either gate.
