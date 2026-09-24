# M3.1 selection execution ledger

Base: M2 493f30cbf8675c68ce14bdffbd311556e92e6a7c; resumed isolated branch codex/m3-1-selection at d173869b681b8135f5d151d0aae81b35052db978.
Scope: user-approved M3.1 selection, primary/order/scope anchors, hover/search/focus session isolation. No M3.2/M3.3 editing features or main merge.
Plan: PandoEditor-M3-plan.md T0/T1 selection prerequisites and T2. Binding reference: docs/web-parity-contract.md.
Ruling: container and Python each returned TransportTimeoutError; use authorized GitHub branch and fresh hosted CI, not claim a local checkout/build.
Ruling: reuse the pre-existing branch after verifying its only change was CI configuration. No rewrite of previous commits.
Preflight: the current selectCountry/selectAt/selectLayer call commitPendingEdits. Selection switching must preserve drafts by their editing target, not move them to another object or discard them. Field commit remains a separate edit event.
Task 1: failing controller regression staged; CI result pending. Tests must demonstrate revision/Undo/Redo/dirty preservation with an outstanding draft.

## QML completion (2026-09-19, local verified result)

The above entries are the historical start of the earlier core/controller session.
Continuation base is remote commit 1aeb400e9738b518deef1c5c4806bfc76ad6a40a, restored from its CI source archive.
A local isolated worktree now contains the completed search tab, actual modifier selection,
overlap chooser, camera/hover wiring and selection-only draft guards.
The final local Qt 6.8.3 CTest run passed 19/19 suites (0 failures/skips);
12 UI data cases cover PC 1100px and mobile mode 360px, and 2066 transitions match the pinned web reducer.
See qt-selection-ui-implementation.md for exact scope and platform limitations.
A GitHub tree write was blocked by the tool's security determination. No remote update/merge/release was performed.
Deliver the locally verified patch/source/evidence, without bypassing the blocked write.
