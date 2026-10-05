# M9.7.4 bounded source-history lifecycle gate

This follow-up extends the indexed snap/shared-boundary checkpoint. It does not claim universal M9.7.4 parity, equal pixel-input queries, or full application/render history equivalence.

## Ownership and behavior

`geometrysnap::Provider` remains the only owner of insertion ranks. An actual mapped client request observes current canonical sources before enqueue; a successful current completion observes them again. Stopping preserves immutable old rank snapshots and READY snap candidates, cancels pending snap work, and defers reseeding until the next genuine request. A same-key READY cache hit does not restart or rebase. Root sync notifications while stopped are inert; a new project installation overrides the deferred state.

Boundary cancellation distinguishes an outstanding client result from settled READY or invalid-topology ERROR. Native boundary adoption is synchronous, so the web's asynchronous render-adoption interval is not represented as another native pending state.

Selection's component-source-prepared marker records whether the corresponding web client preparation has occurred. It is not another geometry cache. Native setup precomputation does not set it. Source and archived-part changes invalidate it; stale completions cannot mark a replacement prepared. Mapped request counts distinguish explicit Clear/Back/source/method cancellation from internal selection replacement and timer-only waiting.

Only mapped jobs are canceled on an explicit Worker stop. Native-only empty selection recomputation uses a different scheduler key, so it cannot make an older mapped request appear settled early. Root receipt Apply is not treated as a new execute. Root polygon preprocessing is also a no-execute control; child direct polygon preparation is counted separately.

## Frozen web source

The source oracle remains `kimjeon-il/Pando` at `ad78780f79f4f38fcd7c2a3c2fb1fe0ba8e37c32`, through the previously approved chain originating at `53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47`.

The new source-history suite reuses the existing immutable M9.7.4 archive and adds only separately pinned production modules. Original source-order, snap, boundary and supplemental fixtures are unchanged. New input fixtures contain synthetic geometry and workflow stimuli, not browser-calculated golden outputs.

## Evidence and gate

- `tools/m974-snap-boundary/source-history-browser-runner.mjs` captures actual production controller modules, client, Worker, snap resolver and indicator in pinned Chromium/V8. It requires genuine exact-commit GitHub Actions identity before browser launch.
- The trace records client invocation/settlement, complete Worker envelopes, delivery holds, cancellation, stop and rebase. Held results are already Worker-complete; they are not evidence of a still-executing Worker CPU phase.
- `tools/m974-native-source-history.mjs` compares eleven explicitly paired public native observations with authenticated browser observations. Extra web-only transport observations and native-only diagnostics remain separately listed.
- Native observations authenticate full document bytes before and after Undo and derive source geometry from those bytes. The web observed canonical view contains materialized territorial entities, generic features, hydro edits, labels and distribution objects; it is not a full persistence/timeline/presentation snapshot.
- The new `m974_source_history_contract` CTest is mandatory in the aggregate omission audit. Existing gates are retained.

Native and web query tool/owner/camera contexts are not equalized. The paired assertion is exact source geometry plus the **actual selected snap indicator**, not full candidate-query equivalence. Both reports retain `rawParity:false`, `queryContextEquivalent:false` and `selectedIndicatorOnly:true`.

The paired cases cover generic deletion/Undo with no observation, pending versus settled boundary cancellation, stopped root delete/Undo, setup-only controls, READY component preparation, and pre-request versus in-request cancellation. Provider tests separately cover READY cache reuse after stop, immutable old maps, deferred reseeding and project replacement. Controlled stop/cut/preview and request-replacement diagnostics are labeled as such.

## Explicit remaining integration limits

These are not passes and must remain visible in later parity acceptance:

- Web asynchronous boundary render adoption and boundary-move phases have no identical native public interval.
- Native cut calculation occurs at Finish; web assessment may execute/cache it earlier. No second cut cache or new engine was introduced to manufacture equal timing.
- Native component/selection and river/sliver calculations fold some separate web request boundaries.
- Child Apply validation, unassigned setup-source, coast/library and renderer-owned boundary/highlight source events do not have complete paired public-stimulus coverage in this gate.
- GPU error-recovery mesh rebuild and rollback after an external Worker commit have no established native counterpart. Ordinary adaptive quality changes and ordinary preview cancel must not be used as fabricated rebase hooks.
- Native scheduling rejects revision-stale work before completion; the web can fulfill territorial read-only work, observe current sources, and then reject. Post-success/cancel browser diagnostics document this distinction rather than claiming native equality.
- Existing projection, typed-invalid-state, full DOM/GPU, presentation and private-history observation limits from the preceding checkpoint remain.

Later engine/presentation integration must resolve or explicitly retain these limits. This patch neither changes the approved web behavior nor advances the source oracle to current main.
