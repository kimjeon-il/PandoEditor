# M9.7.4 bounded source-history lifecycle corpus

This is an additional observation corpus. Existing M9.7.4 source archives, source-order-v2 inputs, original 68 workflows, and supplemental 12 workflows are unchanged.

## Run

Authoritative capture, only inside authorized exact-commit GitHub Actions:

`node tools/m974-snap-boundary/source-history-browser-runner.mjs OUTPUT_DIRECTORY`

Non-authoritative Node discovery:

`node tools/m974-snap-boundary/source-history-node-runner.mjs OUTPUT_JSON`

Focused tests:

`node --test tools/m974-snap-boundary/source-history-*.test.mjs`

The browser runner verifies the exact checkout SHA/run identity before output or launch. Chromium, V8, Playwright, production sources, synthetic inputs, portable harness, report transfer, and output bytes remain separately authenticated.

## Coverage

24 cases: 11 explicitly paired native cases and 13 web-only diagnostics. `source-history-input-manifest.json` and the suite verifier require the exact paired list. Extra native rows are not implied matches.

- Public generic deletion and actual history Undo, with no intervening Worker work
- Boundary preparation READY, client-unsettled held-result cancellation, settled invalid-boundary ERROR, and stopped root deletion/two Undos
- Actual production pointer cache hit surviving stop, next-cell lazy restart, replacement and superseded result diagnostics
- Split/annex setup, component READY, pre-execute activation cancellation, and counted request cancellation
- Components cache-hit control, root/child line cut, and child polygon preview without Apply
- Held selection request ownership survives last-component deselection and two owner microtask drains; subsequent Clear still stops before lazy restart
- Labeled explicit-stop diagnostics before real cut, drawn-polygon, and annex-preview requests
- Successful scheduler resolution followed by source-revision rejection versus canceled ticket settlement: only the former observes a late generic deletion through post-result edit-sync

All calculations use the approved production client, RPC, Worker, source tracker, geometry operations, and controller modules. The selection helper's calculator stub is replaced with the same real client. Cut preparation uses the exact archived assessDraft/cancelPreparation callback span and production cache. Public root geometry notifications use actual app-spatial-index rather than manual syncPatch injection.

## Limits

`rawParity:false`, `queryContextEquivalent:false`, and `selectedIndicatorOnly:true` are required. Query context, selection/tool state, projection, tolerance, and candidate arrays are not universally equivalent between native and browser. Shared comparisons establish bounded insertion-order outcomes from explicitly matched geometry/stimuli.

Web canonical bytes cover materialized entities, generic features, hydro edits, labels, distribution layers, and distribution entries. They exclude identity/timeline storage and presentation state; `fullCanonicalProject:false` is required. Native full-document restoration is separate evidence.

The harness calls production controller modules with test platform ports; it does not operate the complete application DOM or GPU renderer. Rendering-triggered requests are unobserved. Held results are real Worker results delayed before client delivery, so the Worker has already completed; this does not prove CPU-in-flight or render-adoption-pending behavior. The native queued-work gate and web held-result gate are separately disclosed.

The case named `component-timer-only-cancel` matches native deferred activation semantics; its web stage explicitly says it cancels while production activation awaits source preparation, before any client execute, with no browser timer claim.

Controlled-stop, source-generation replacement, and post-settlement timing cases are diagnostics, not new public-app native parity cases. No Apply, renderer, rollback, library, or comprehensive scheduler-equivalence claim is made.

## Strict evidence checks

Every warm/final snap must link to a real client invocation, execute transport, incoming and delivered result, and client settlement. READY must have both incoming and delivered transport. Source patch geometry and metadata are checked against canonical state at the recorded phase, which must follow the exact input → deletion → Undo progression. Candidate and owner order is retained verbatim and checked against transported insertion history. Tests include resequenced omission, forged geometry/metadata, fabricated deletion, cache/stop, identity, runtime, and candidate mutations.
