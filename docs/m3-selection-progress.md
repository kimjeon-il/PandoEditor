# M3.1 selection execution ledger

Base: M2 493f30cbf8675c68ce14bdffbd311556e92e6a7c; resumed isolated branch codex/m3-1-selection at d173869b681b8135f5d151d0aae81b35052db978.
Scope: user-approved M3.1 selection, primary/order/scope anchors, hover/search/focus session isolation. No M3.2/M3.3 editing features or main merge.
Plan: PandoEditor-M3-plan.md T0/T1 selection prerequisites and T2. Binding reference: docs/web-parity-contract.md.
Ruling: container and Python each returned TransportTimeoutError; use authorized GitHub branch and fresh hosted CI, not claim a local checkout/build.
Ruling: reuse the pre-existing branch after verifying its only change was CI configuration. No rewrite of previous commits.
Preflight: the current selectCountry/selectAt/selectLayer call commitPendingEdits. Selection switching must preserve drafts by their editing target, not move them to another object or discard them. Field commit remains a separate edit event.
Task 1: failing controller regression staged; CI result pending. Tests must demonstrate revision/Undo/Redo/dirty preservation with an outstanding draft.
