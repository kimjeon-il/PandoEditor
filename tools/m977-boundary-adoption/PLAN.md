# M9.7.7 boundary lifecycle diagnostic implementation plan

> For agentic workers: implement the bounded diagnostic in the assigned engineering checkout; preserve original source archives and existing fixture/manifests. This is an isolated task under the already authorized M9 plan.

**Goal:** Expose cancellation and public selection changes before preparation completion, during the web client-settled/render-adoption interval, and during boundary move.

**Architecture:** Reuse the pinned production module loader and actual Worker, exact boundary callbacks, and public native controller. Add observation-only transport scheduling and promise observation outside product code. Native queued work is genuinely not started; web queued transport is not sent. Neither is labeled CPU-in-flight. Native adoption/move interval absence is an architectural result rather than a manufactured pending state.

**Tech stack:** Node diagnostic, browser-safe JavaScript, Qt public controller probe, Python isolated local build adapter.

**Spec:** Parent assignment, docs/m974-source-history.md, docs/m974-snap-shared-boundary.md.

## Global constraints
- Do not change or repin committed oracle sources/manifests.
- Do not modify app/CMakeLists.txt, CI workflow, pending-input files, or shared build outputs.
- Do not launch a local browser; authoritative browser execution remains exact-commit CI only.
- No product changes, push, main integration, or portable packages.

## Review focus
- A held completed Worker result must not be reported as CPU-in-flight.
- A client promise resolution must be distinguished from render packet adoption.
- Selection must be actual public selection, not a forged generation/revision change.
- Canonical byte equality must be accompanied by preview, draft and history observations.
- Absent native intervals and public selection divergences must remain explicit.

## Tasks
- [x] Add failing diagnostic tests for immutable source loading, phase evidence, real cancel/selection stimuli, and canonical/history immutability.
- [x] Implement 3-phase × 3-action web diagnostic with no-op controls. Observe actual production promises and asynchronous adoption; preserve complete transport evidence.
- [x] Implement native public-controller counterpart with queued-worker barrier and explicit absent-interval fields. Build into sibling scratch directory using existing controller objects without rewriting shared build outputs.
- [x] Add fail-closed receipt validation and mutation tests; run Node/native diagnostic and source-integrity checks.
- [x] Document exact findings and browser-CI/aggregate integration requirements. Do not claim browser parity from local diagnostics.

## Execution notes

- RED: missing runtime/runner modules; GREEN after isolated implementations.
- RED: small fixture had all 105 result objects already frozen; corrected the diagnostic fixture to require a genuine unfinished production checkpoint, then GREEN at 512 frozen / 1,174 unfrozen. This was a test-input correction, not a product or source change.
- Local native build reused existing product objects; exact-commit clean CI remains required.
- No implementation acceptance or browser parity claimed.

## Independent review corrections

- Missing Worker transport could pass a consistency-only verifier. Added a failing full-transport-omission mutation, then required every resolved client to link to real posted/incoming/delivered transport.
- Public-action events could be moved after settlement. Added a failing late-action mutation, then bound the action to ordered stage cursors and immediate public state.
- Native final cancellation snapshot could precede outstanding queued completion. Added a real pool-completion barrier followed by owner delivery before the snapshot; the receipt validator requires this evidence.
- A clean tracked diff did not reject an untracked harness in CI. Added a failing temporary-Git-fixture test, then required every harness file to match its HEAD blob before launch.
