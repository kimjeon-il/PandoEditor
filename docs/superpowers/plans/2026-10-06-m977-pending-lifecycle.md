# M9.7.7 pending-input lifecycle implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Parent approved bounded scope; preserve existing shared checkout and coordinate build ownership.

**Goal:** Continue matched preparation-phase taps through Finish, preview, Cancel/retry, Apply and exact project Undo/Redo.

**Architecture:** A separately identified two-profile corpus reuses the existing current-model reference fixture, source closures, model serializer and lifecycle APIs. Production web handlers/editing domain and real Worker execute every transition; the native probe uses public controller operations. Existing raw observations stay immutable.

**Tech Stack:** Node 24.19.0 diagnostics, Qt 6.8.3, existing Playwright 1.62.1 / Chromium 151.0.7922.34 pin.

**Spec:** docs/m975-current-model-bridge.md:37–41; docs/m977-pending-input-observations.md:107–112.

## Global Constraints

- No product-policy changes, UI source edits, source repins, portable packaging or publication.
- Preserve old 14-case diagnostic corpus and every existing fixture/manifest.
- Browser only in separately authorized exact-commit CI; Node remains diagnostic.
- Cancel and native history require exact whole-model bytes. Web history retains the established exact documentKeys scope and seven enumerated raw serialization/presentation deltas. Cross-engine raw differences remain reported.
- Native private history depths and simultaneous worker CPU cancellation remain unclaimed.

## Review Focus

- A missing or failed Finish/preview/Apply/history stage must fail, never skip.
- Pending inputs cannot leak into the ready draft or next retry.
- Preview/Cancel cannot change canonical references or project history.
- Apply must make a real intended annex edit; Undo/Redo must restore exact snapshots.
- Source/corpus/harness/browser/native provenance must bind the actually executed bytes.

## Task 1: Add matched production lifecycle diagnostic

Files: new tools/m977-pending-lifecycle/{corpus.json,sources.mjs,runtime.mjs,contract.mjs,node-runner.mjs,runtime.test.mjs,contract.test.mjs}.

- [x] Write a two-profile lifecycle test and run RED before implementation: missing sources/runtime modules.
- [x] Implement verified source union and immutable reference-fixture/corpus identity.
- [x] Reuse actual model install/checkpoint, editing domain, handlers, Worker, and history APIs.
- [x] Verify complete stages, exact own-runtime model/reference/history, transport and mutation rejection; run GREEN.

## Task 2: Add native public-controller lifecycle probe

Files: new tests/m977_pending_lifecycle_probe.cpp; native.test.mjs and compare.mjs; additive app/CMakeLists.txt.

- [x] Write native collection/byte/history mutation tests; run RED before probe implementation.
- [x] Replay identical declared taps with pending readiness barriers and real Finish/Apply/Undo/Redo.
- [x] Build under exclusive shared-build ownership; run focused tests and record exact evidence.

## Task 3: Add exact-CI capture gate and independent review

Files: new suite.mjs, browser-runner.mjs, gate tests, README.md, .github/workflows/m977-pending-lifecycle.yml, tests/m977-pending-lifecycle-integration.test.mjs, docs/m977-pending-lifecycle-observations.md.

- [x] Test fail-closed runtime/source/harness/native/capture identities RED, then implement GREEN.
- [x] Register one aggregate CTest and verify inventory additive.
- [x] Run focused and related existing contracts; report raw differences without promoting a parity claim.
- [ ] Obtain independent review and fix important findings through RED/GREEN.
- [ ] Hand off unpublished candidate/evidence for complete aggregate and separately authorized exact-commit Chromium collection.


## Approved matched-input refinement

The initial scale-1000 diagnostic preserved its raw evidence but did not prove paired geometry, because actual desktop D3 inverse yielded x=3.9999999999999987 while native input yielded x=4. Parent approved a separately identified unpublished corpus v2, with explicit view scale 572.9577951308232 and unchanged geographic points. Actual D3/native inverses and viewport bounds are verified, not repaired.

- [x] Preserve original corpus bytes and raw diagnostic evidence under its original SHA-256.
- [x] Use unchanged existing split exactGeometryBoundary for every paired archive/candidate/part geometry; no epsilon, rounding or clipping.
- [x] Compare shared whole-model nongeometry fields, identity/parent/reference/archive-version order and lifecycle/history outcomes, allowing only documented public phase spellings and six empty web visibility groups.
- [x] Reject coherent one-runtime one-ULP geometry mutation even when its own snapshot/history invariants still pass.
- [x] Verify matched full local suite: 33/33 tests, zero skips. Actual Chromium remains pending external authorization and exact-commit CI.
