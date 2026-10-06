# M9.7.7 boundary preparation, adoption and move timing diagnostic

This is a bounded diagnostic, not a parity pass. Its fixed input corpus has three timing phases × no-op, whole-tool Cancel, and public selection replacement with existing object C. The pinned production source closures, input corpus and archived browser receipts are unchanged by the selection-ownership correction.

## Historical observation and current comparison

The actual Chromium capture at `74365a207c5770006424004f0c1c9388e19f0676`, run `37434242723`, observed:

- Canonical bytes remain unchanged within each platform in all nine cases, with no project Undo/Redo entry. Initial territorial geometry/metadata and settled ordered selection/primary compare exactly.
- Public replacement of `[A, B, C]` with `[C]` preserves primary C. Web preparation still owns A/B/C and remains READY in all three phases. The historical native selection-revision guard exposed ERROR in all three and rejected the move case's downstream preview. Those historical receipts retain three readiness differences and one preview difference.
- Cancel restores A, B, C with C primary on both sides, matching the pinned root-cancellation fallback. No old preparation or preview reappears.
- Web Cancel stops the Worker while preparation/move is outstanding. After client settlement during unfinished adoption, it does not stop the Worker.

The source-backed correction separates preparation ownership from canonical-preview validity. Preparation owns the captured boundary targets independently of ambient selection membership, order and revision. Canonical preview separately checks the primary ID captured when its request began. The fixed corpus keeps C primary, so all nine settled readiness/preview outcomes should match after correction. A new native capture must demonstrate that result; changing expected counters is not evidence.

The comparator derives every readiness/preview difference and every summary count from the supplied observations. It accepts historical selection outcomes for diagnosis rather than requiring them forever, retains exact differing status/preview values, and keeps Cancel, canonical bytes, public history, fixed owner targets and actual selection checks strict. Mutation tests distinguish a zero result from suppressed differences. The live native regression separately requires the corrected nine-case outcomes.

The historical browser identity contains `selectionDivergenceUnresolved:true`. That flag describes the archived harness at capture time and is preserved unchanged; it is not a computed verdict on a newer native implementation.

A fresh local native capture of the corrected controller was compared against that authenticated archived Chromium report: all nine settled readiness/preview outcomes match, with zero canonical/history mutations and all six native interval absences retained. This is explicitly cross-source evidence from a dirty local working tree, not a same-commit CI acceptance. The unchanged historical native receipt still recomputes to three readiness and one preview difference.

## What each interval proves

### Preparation queued before execution

The web transport queues the actual production `boundary-prepare` execute envelope before calling the actual Worker object's `postMessage`. Native reserves the only global-pool thread before invoking public `beginSharedBoundaryGeometry`; the real preparation task is queued behind it. The public Cancel or selection action runs before either calculation starts. Traces distinguish requested, queued, posted, incoming, delivered and settled calls. Neither gate is CPU-in-flight evidence, and neither holds an already completed result. Native final snapshots wait for actual pool completion, then process owner completion events while the controller is still alive.

These are analogous pre-execution phases with different scheduling mechanisms; no scheduler identity or full event-loop equivalence is claimed.

### Client settled, adoption genuinely unfinished

Only this phase uses a denser input: every edge of the same three dyadic polygons has 32 subdivisions. The actual client promise is observed without replacement. A microtask observes the unchanged production `adoptBoundaryRenderPacketAsync` at its first 512-object checkpoint. Exactly 512 objects are frozen and 1,174 are not yet frozen, while `workerPending=false`, preparation status is `pending`, and neither result nor render packet has been published. No clock, production source, returned result, or adoption function is modified.

No-op and selection finish with all 1,686 objects frozen. Cancel interrupts adoption before all objects freeze and prevents publication. This proves an unfinished CPU-side render-packet adoption phase. It does not prove a DOM event can arrive at this microtask, a rendered/GPU frame, or interruption while the Worker CPU is executing.

Native consumes the exact same dense input. Its session adoption is atomic in the owner completion callback, so the interval is explicitly unobserved. Its corresponding public action is performed after READY. That observation is not counted as a same-timing parity pass.

### Boundary move queued

The web's actual `boundary-move` execute is queued before Worker posting. The native public node fan-out is synchronous and already finished before release returns. The native probe records that absence, then places the separate downstream canonical-preview calculation behind the pool barrier before the same public action. It does not rename that preview job into a move job.

## Observation scope

- Web observations call real production controller modules and the exact archived gesture callback span. Public selection uses `selectionUiController.replaceMany`; native uses `selectObject(..., "replace", "countries")`. These are public APIs, not full DOM pointer/keyboard paths. Whether every selection action is reachable while these tools are open in the complete application UI remains unobserved.
- Web free-draft state, active-gesture coordinates/features/visual adapter events, preparation packet readiness, preview presence, selection, canonical JSON and numeric history are recorded. Native public projected draft paths, public edit/preview state, selection, full document bytes/hash and public Undo/Redo availability are recorded. The raw draft formats and native private history depth are not asserted equal.
- Web canonical bytes cover the existing lifecycle observation document, not complete persisted presentation state. Native full-document equality is independent evidence. Cross-platform initial territorial rows are compared exactly; serialized whole projects are not claimed equal.
- `rawParity:false` and `parityAccepted:false` remain mandatory. A passing diagnostic validator authenticates the stated observations. Zero settled outcome differences do not establish matching execution intervals or raw parity.

## Run locally without a browser

`node tools/m977-boundary-adoption/node-runner.mjs OUTPUT_DIRECTORY`

Build an isolated native diagnostic using a previously compiled build tree (all outputs go to the supplied separate directory):

`python3 tools/m977-boundary-adoption/build-local-native.py --build EXISTING_BUILD --output SEPARATE_OUTPUT --ninja PATH_TO_NINJA`

`SEPARATE_OUTPUT/native-probe < OUTPUT_DIRECTORY/cases.json > OUTPUT_DIRECTORY/native-report.json`

`node tools/m977-boundary-adoption/compare.mjs OUTPUT_DIRECTORY/node-report.json OUTPUT_DIRECTORY/native-report.json OUTPUT_DIRECTORY/comparison.json`

`M977_BOUNDARY_PROBE=ABSOLUTE_PATH_TO_PROBE node --test tools/m977-boundary-adoption/*.test.mjs`

Without `M977_BOUNDARY_PROBE`, the native test explicitly skips; that invocation is not a complete native gate. The local build adapter reads compile/link commands and existing product objects without writing shared build output. Its provenance explicitly says `cleanExactCommitCIBuild:false`.

## Exact integration and publication

CMake registers `m977_boundary_timing_probe` linked to `pandoeditor_editor`, with the existing sample resource and test helper include path. The required `m977_boundary_timing_contract` runs all four test files plus integration checks with a real probe and offscreen/software Qt. The aggregate omission audit and `.github/workflows/m977-boundary-timing.yml` retain this route. A final exact-source successor build and aggregate regression remain required after a product correction.

Only after authorized publication on a final exact commit, the approved CI path may run:

`node tools/m977-boundary-adoption/browser-runner.mjs OUTPUT_DIRECTORY`

The runner refuses non-CI identity or a mismatched/dirty tracked checkout and requires every executable harness dependency to exist in HEAD with matching committed bytes before importing Playwright or launching a browser. It requires existing pinned Playwright, Chromium and V8 versions; serves only authenticated archived production sources; authenticates source closure, runtime, harness and exact input order; transfers complete raw JSON in bounded chunks; and binds suite/report bytes and lengths.

The same CI job must build the new native target from that exact checkout and run it with capture `cases.json`, then:

`node tools/m977-boundary-adoption/compare-browser.mjs OUTPUT_DIRECTORY/suite.json.gz OUTPUT_DIRECTORY/browser-report.json OUTPUT_DIRECTORY/capture-verification.json OUTPUT_DIRECTORY/native-report.json OUTPUT_DIRECTORY/comparison.json`

Publish the full raw capture, transfer receipt, suite, CDP runtime, byte binding, native report, native build/CTest logs and comparison. The comparison authenticates browser capture; native build identity must also be proven by exact-checkout CI logs/artifact provenance. No browser, CI, push, merge or portable package was run/generated as part of this local diagnostic task.

## Comparing a corrected native build with the archived capture

`compare-browser.mjs` is intentionally the same-source authentication route. Its verifier binds every executable harness dependency, including the comparator and shared native probe source. After those files change, it must reject an old suite rather than relabel it as a new exact-commit capture.

For an explicitly cross-source diagnostic, first authenticate the old suite/report/binding with its immutable archived exact-source verifier. Then call the current `compareTimingObservations` with those authenticated raw web observations and a genuinely new native receipt. Carry the old commit/run, suite/report hashes, native receipt/binary hashes and new source provenance into the separate analysis. Do not rewrite either receipt or the historical identity. Fresh exact-successor CI is required for same-commit acceptance.

The legacy M9.7.4 `root-stale-preparation`, `root-stale-move` and `root-stale-preview` inputs are different: web changes project generation/state revision, while native performs public `selectCountry(C)` and preserves primary C. Native now records its actual preparation/preview outcome rather than hardcoded rejection. The legacy comparator reports the resulting raw outcome/readiness/preview differences and labels the non-equivalent stimuli; these differences are not parity regressions for matched selection input and are never waived into matching stale-project evidence. Real Cancel and actual completed-worker delivery checks remain required.

Do not fabricate native asynchronous states, substitute generation changes for public selection, or rename completed-worker result holding as these intervals. The six native adoption/move intervals remain absent even when all nine settled outcomes match. CPU-in-flight, complete application input reachability and GPU render observations require separately scoped evidence if M9.7.7 acceptance needs them.

The separate `../m977-boundary-session-replacement/gate.mjs` defines only 30 legacy observable contracts within their recorded limits plus two matched public-session replacement cases. It preserves the entire original 33-case raw comparison as false, validates the exact three non-equivalent diagnostics, and recomputes both comparisons from authenticated raw receipts. It does not accept arbitrary exclusions or establish matched owner-lock/project-stale, asynchronous move, full DOM, GPU or raw parity coverage.
