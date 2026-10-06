# M9.7.7 boundary preparation, adoption and move timing diagnostic

This is a bounded diagnostic, not a parity pass. No product code or committed oracle source manifest is changed. The new input corpus consists of three timing phases × no-op, whole-tool Cancel, and public selection replacement with existing object C.

## Verified local findings

At base `218b1a56cf6c1fad75779e19ef8107b9f61cfccf`:

- Canonical bytes remain unchanged within each platform in all nine cases, with no project Undo/Redo entry. Initial territorial geometry/metadata and settled ordered selection/primary are compared exactly.
- Public replacement of the object selection with C leaves the web boundary preparation READY in all three phases. Native binds the boundary session to selection revision and exposes ERROR in all three. During the move case, the web opens a preview and native rejects the downstream preview. These are recorded divergences, not expected-parity exceptions or product fixes.
- Cancel restores A, B, C with C primary on both sides, matching the pinned root-cancellation fallback. No old preparation or preview reappears.
- Web Cancel stops the Worker while preparation/move is outstanding. After client settlement during unfinished adoption, it does not stop the Worker.

These results are from Node production-module diagnostics and a locally compiled native probe linked to pre-existing build products. They are not actual Chromium evidence or a clean native exact-commit build.

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
- `rawParity:false` and `parityAccepted:false` remain mandatory. A passing diagnostic validator means the stated divergences and architectural differences were observed.

## Run locally without a browser

`node tools/m977-boundary-adoption/node-runner.mjs OUTPUT_DIRECTORY`

Build an isolated native diagnostic using a previously compiled build tree (all outputs go to the supplied separate directory):

`python3 tools/m977-boundary-adoption/build-local-native.py --build EXISTING_BUILD --output SEPARATE_OUTPUT --ninja PATH_TO_NINJA`

`SEPARATE_OUTPUT/native-probe < OUTPUT_DIRECTORY/cases.json > OUTPUT_DIRECTORY/native-report.json`

`node tools/m977-boundary-adoption/compare.mjs OUTPUT_DIRECTORY/node-report.json OUTPUT_DIRECTORY/native-report.json OUTPUT_DIRECTORY/comparison.json`

`M977_BOUNDARY_PROBE=ABSOLUTE_PATH_TO_PROBE node --test tools/m977-boundary-adoption/*.test.mjs`

Without `M977_BOUNDARY_PROBE`, the native test explicitly skips; that invocation is not a complete native gate. The local build adapter reads compile/link commands and existing product objects without writing shared build output. Its provenance explicitly says `cleanExactCommitCIBuild:false`.

## Exact integration and publication still needed

The parent integration owns CMake and workflow files. Add an ordinary `m977_boundary_timing_probe` executable from `tools/m977-boundary-adoption/native-probe.cpp`, include the repository `tests` directory, link `pandoeditor_editor`, and add the sample resource exactly as the existing `m974_boundary_probe` target does. Add the four `.test.mjs` files as a required Node CTest with `M977_BOUNDARY_PROBE=$<TARGET_FILE:m977_boundary_timing_probe>` and the offscreen/software Qt environment. Include that test in the required aggregate omission audit.

Only after authorized publication on a final exact commit, the approved CI path may run:

`node tools/m977-boundary-adoption/browser-runner.mjs OUTPUT_DIRECTORY`

The runner refuses non-CI identity or a mismatched/dirty tracked checkout and requires every executable harness dependency to exist in HEAD with matching committed bytes before importing Playwright or launching a browser. It requires existing pinned Playwright, Chromium and V8 versions; serves only authenticated archived production sources; authenticates source closure, runtime, harness and exact input order; transfers complete raw JSON in bounded chunks; and binds suite/report bytes and lengths.

The same CI job must build the new native target from that exact checkout and run it with capture `cases.json`, then:

`node tools/m977-boundary-adoption/compare-browser.mjs OUTPUT_DIRECTORY/suite.json.gz OUTPUT_DIRECTORY/browser-report.json OUTPUT_DIRECTORY/capture-verification.json OUTPUT_DIRECTORY/native-report.json OUTPUT_DIRECTORY/comparison.json`

Publish the full raw capture, transfer receipt, suite, CDP runtime, byte binding, native report, native build/CTest logs and comparison. The comparison authenticates browser capture; native build identity must also be proven by exact-checkout CI logs/artifact provenance. No browser, CI, push, merge or portable package was run/generated as part of this local diagnostic task.

A successful capture still leaves the three readiness differences and one preview difference unresolved. Do not fix them by fabricating native asynchronous states, substituting generation changes for public selection, or treating the previously held completed-worker results as these intervals. CPU-in-flight, complete application input reachability and GPU render observations require separately scoped evidence if M9.7.7 acceptance needs them.
