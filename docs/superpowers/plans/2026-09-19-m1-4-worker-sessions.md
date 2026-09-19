# M1.4 Worker Sessions Implementation Plan

> Use executing-plans and test-driven-development for each step. This executes the approved sequential M1.4 scope, with the user's new web-fidelity constraint.

**Goal:** Execute candidate preparation off the editor thread, cancel/coalesce safely, reject obsolete results, and use the existing command commit contract unchanged.

**Architecture:** An immutable ProjectSnapshot is captured on the editor thread. A Qt-free, single-running-job scheduler follows the web map edit scheduler's priority/FIFO/latest-key rules. QtConcurrent executes snapshot-only tasks; completion is delivered on the editor thread and must pass both the scheduler session check and CommandProcessor::confirm. Workers never own a Project or UI pointer.

**Tech Stack:** C++17, Qt >= 6.5 Core/Concurrent/Quick, existing CMake/CTest.

**Spec:** docs/web-to-qt-roadmap.md M1.4; docs/qt-command-implementation.md; web worker-job-scheduler.js and map-edit-worker-client.js at 58e4087f85aa51884bc4ab80959e05010d94d7d5.

## Global constraints
- Do not change the web repository or either existing branch/main.
- Preserve domain command IDs/arguments, geometry, value ranges, locks, preservation rules and snapshot Undo semantics. No substitute geometry algorithms or simplified features.
- Keep pre-existing Qt prototype UI separate from proof of web UI equivalence.
- Same PC/mobile command path and 360px layout tests; no Android/Windows-native completion claim without execution.
- Failed, cancelled, coalesced, stale and NoOp work cannot mutate document, revision, saved state, draft or history. An accepted Apply is one ChangeSet.
- Cancellation is cooperative: mark obsolete immediately, retain the running slot until actual completion, discard any late candidate.

## Tasks
- [x] Baseline: verify exact archived tree of upstream 2b0440f; isolated checkout; run existing core regression.
- [x] Add assertion-based failing core tests for web latest-key/coalescing, priority/FIFO, cancellation, stale after edit/Undo/Redo/reopen and snapshot reference lifetime. Compile against minimal API scaffolding, execute and retain the failing log.
- [x] Implement ProjectSnapshot + prepare overload and scheduler. Re-run existing 4 core suites and new job suite.
- [x] Add failing Qt tests through meta-object entry points for async Apply/cancel/stale input; run against the pre-adapter tree in CI. Add semaphore-controlled runner lifetime tests and execute with the adapter.
- [x] Implement snapshot-only QtConcurrent runner, common controller async Apply entry and progress/cancel presentation. Preserve synchronous command helpers and immediate command transaction boundaries.
- [x] Verify desktop/mobile same document, revision, history, save/reopen and QML Apply/cancel path. Use controlled completion gates instead of sleeps for concurrency assertions.
- [x] ASan/UBSan core, full Qt regression, exact tree comparison, separate final self-review and document validation limits.
- [x] Update web parity policy/README/roadmap and implementation record; publish only isolated branch; leave main and web unchanged.

## Web fidelity correction
Country name/notes use independent web change commits, not mandatory batched Apply. Remaining prototype controls must not be labelled full web parity. Source paths and the checked SHA are in docs/web-parity-contract.md.
