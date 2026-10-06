# M9.7.7 paired public boundary-session replacement

This additive corpus contains exactly two cases:

- `preparation-completed-reenter`: the old real boundary-preparation calculation has finished, but its owner delivery has not occurred.
- `preview-completed-reenter`: the original preparation and move have run, and the old real downstream canonical-preview calculation has finished, but its owner delivery has not occurred.

Both use unchanged A/B/C polygons, ordered selection `[A,B,C]`, initial primary/seed A, and the existing exact dyadic move `(1,1) → (1,1.25)`. The shared public sequence is whole-tool Cancel, explicit replacement selection `[A,B]` with primary A, then public root boundary entry. Entry changes the visible primary to B. The replacement-entry snapshot is captured before releasing/draining the old delivery. Final observations must retain owners A/B, primary B, no canonical preview, and the now-fixed former triple junction `(1,1)` with incident owners A/B/C. Full canonical bytes within each platform and public project history remain unchanged at every stage.

## What is actually observed

Web uses unchanged production country modes, public selection UI controller, map-edit client and real Worker, plus the exact archived production begin/move/commit gesture callback span. Only the old result's exact `(worker, requestId)` is held. The replacement request cannot be accidentally held even when it reuses the same Worker. Full outgoing/incoming/delivery/settlement envelopes are retained, with per-request source revisions, job identity, exact action order and observation cursors. Preparation Cancel stops the old Worker; downstream-preview Cancel reuses it. Both routes are validated.

Native uses the real public `EditorController`, public selection and boundary gestures. Its global pool is allowed to finish the old real calculation without processing owner events. Cancel, selection replacement and re-entry then run synchronously. Only after the replacement-entry snapshot does the probe process the queued owner callbacks and settle the new preparation. Actual projected controller handles, public states, signals, ordered selection, document bytes and history availability are recorded. The native canonical storage graph (units, metadata, lifetimes, parent/geometry bindings, geometry records and styles) is checked against the declared input, and every flattened stage is bound to that authenticated baseline. Native private request/epoch IDs and private payload interception are explicitly unobserved. There is no fabricated native asynchronous boundary-move interval and no topology-helper substitution for the replacement handle.

This establishes two matched public-session lifecycle stimuli. It does not establish CPU-in-flight cancellation, full DOM/QML pointer reachability, rendered/GPU frames, raw draft packet equivalence, private native history depth, or whole-project serialization parity. Owner-lock/project-stale cases remain separate future coverage.

## Immutable inputs and source provenance

The approved 106-source supplemental closure, manifest SHA-256 `200cf832af6d359c3d97cf48e323f9802dcaeb8107dd2ca42d558c84ad40f18e`, and approved correction chain through `ad78780f79f4f38fcd7c2a3c2fb1fe0ba8e37c32` are unchanged. No old source or archive is repinned. The original 33-case boundary input file remains SHA-256 `abb2345c9e0b744709a293512721dc107ca36474b455eb8a68c1ff9ef0ea9945` and is authenticated independently. New input definitions and their ordered two-case identity are hashed separately.

Suite/report schemas are version 1 with prefixes `pando-m977-boundary-session-replacement-{suite,web,native,comparison}`. The suite authenticates the entire transitive executable harness import closure, the native probe and reused observation helper, CMake/workflow configuration, pinned browser package locks, production callback extraction and input/runtime hashes. Native request IDs are never synthesized to match web IDs.

The CI browser collector refuses non-CI, mismatched, dirty or uncommitted harness bytes before importing Playwright or launching Chromium. It checks the existing pinned Playwright/Chromium/V8 versions, verifies every served production source, transfers complete raw JSON in bounded byte-exact chunks, and binds both suite and report hashes and lengths. No local browser is required or allowed by this collector.

## Local diagnostic and tests

```sh
cmake --build BUILD_DIRECTORY --target m977_boundary_session_replacement_probe
M977_SESSION_REPLACEMENT_PROBE="$PWD/BUILD_DIRECTORY/app/m977_boundary_session_replacement_probe" \
  node --test tools/m977-boundary-session-replacement/*.test.mjs
node tools/m977-boundary-session-replacement/node-runner.mjs /tmp/session-replacement
BUILD_DIRECTORY/app/m977_boundary_session_replacement_probe \
  < /tmp/session-replacement/cases.json > /tmp/session-replacement/native-report.json
node tools/m977-boundary-session-replacement/compare.mjs \
  /tmp/session-replacement/node-report.json /tmp/session-replacement/native-report.json \
  /tmp/session-replacement/comparison.json
```

Without `M977_SESSION_REPLACEMENT_PROBE`, the native-dependent test explicitly skips. Such a run is not a complete native contract. CTest `m977_boundary_session_replacement_contract` supplies the compiled production-linked probe. Node reports are diagnostic only and are rejected by actual-browser authentication.

## Bounded acceptance and exact-commit CI

Only after publication authorization may the exact-commit workflow run the browser collector:

```sh
node tools/m977-boundary-session-replacement/browser-runner.mjs OUTPUT_DIRECTORY
```

The combined `gate.mjs` accepts authenticated raw legacy and additive browser captures, raw newly built native receipts, explicit matching commit/run identities and binary hashes. It recomputes the two comparisons instead of trusting serialized `passed` or difference arrays. CI build logs and artifact provenance must separately establish that both binaries were built from that exact commit; report fields cannot prove it.

Required coverage means exactly “30 legacy observable contracts within their recorded limits plus 2 matched public-session replacement cases.” The original 33-row raw comparison remains `passed:false`, with its three explicitly non-equivalent stale-project-versus-public-selection diagnostics retained and validated. The gate cannot erase, relabel or waive those rows into raw parity. `rawParity:false`, `parityAccepted:false` and combined `fullParityAccepted:false` remain explicit even when all required bounded coverage passes.

Retain raw suite/browser/native reports, transfer and capture bindings, native binary hashes, clean build/CTest logs, the untouched legacy raw comparison and the separate matched-coverage report. A local Node result is not exact-commit Chromium evidence.
