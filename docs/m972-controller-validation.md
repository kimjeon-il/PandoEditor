# M9.7.2 A–C controller and UI validation

This is the next verified substep on `codex/m97-web-editing-parity`, based on
`de89fce69f131d8a131d64335737314b46628ecb`. It does **not** complete M9.7.2,
real river partition generation, complex-cut parity or the final M9.7 gate.

## Fixed behavioral sources

- Original: `kimjeon-il/Pando@53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47`.
- Approved full-donor correction: `12cd8c8ec47c83cfb8c650e8f44c81cdfac10043`.
- Original fixture source hashes are unchanged. The correction remains a separate
  executed delta, not a replacement observation silently tailored to native output.

## Actual controller differential

The new executable `m972_selection_probe` drives public `EditorController` methods,
real timers, typed worker jobs and strict commands. Geometry is observed from the
production overlay paths and the canonical project serializer. It does not invoke
a parallel test selection reducer or read private session state.

Seventeen traces contain 108 ordered observations:

- 15 matched cases.
- Two original-pin cases have exactly four named `canAddPart: false → true`
  differences for the user-approved full-donor fix. The corrected cases match.
- No candidate geometry, candidate order, selected-ID order, persisted object/ref,
  Apply result or Undo/Redo geometry mismatch is waived.
- Four exact stale-empty-cache values in the original web are pinned separately.
  Native internal caches are unobserved by this public-interface probe, so these
  are not reported as successful native cache parity.

The corpus exercises equal-area and minimum-area cuts, reversed gestures,
reversed source winding and shifted ring starts; polygon/full-donor archival;
components and automatic method changes; empty-draft Finish with archived parts;
preview, strict Apply rejection for dangling distribution/label/label-settings,
and actual Undo/Redo. Native rejection preserves canonical bytes without a
fabricated apply/rollback event. Exact internal history depth and river provenance
remain outside this probe's observation boundary.

## Native and QML regression coverage

- Separate geometry receipt tests calculate real transfer/owner rows with four
  dangling-reference classes, then reject strict Apply atomically. Cancellation,
  stale receipts, unchanged geometry, overlap/conservation and numerical-fringe
  cases are included.
- Selection tests cover immediate, in-place ordered toggles; revision-matched
  derived installation; cooperative cancellation; source resets; original
  component provenance; archive removal and explicit confirmation preservation.
- Runner tests cover mixed command/calculation coalescing on the same worker,
  owner-thread completion, stale/cancel disposal, exceptions and destruction.
- Controller tests cover rapid clicks, source confirmation, cancel/restart of the
  same target, review/Back/Apply, empty polygon draft preservation, unsupported
  hole/self-crossing cut rejection, nested-unit donor picking/highlighting and
  asynchronous component-method failure recovery.
- All four dangling-reference controller fixtures enable real autosave, settle
  the initial write, then assert zero `ProjectAutosave::saved` events and identical
  persisted bytes across preview and rejected Apply.
- Eight actual QML pointer cases run at desktop and 360 px widths. They exercise
  all three methods, map/list candidate selection, accumulation, middle removal,
  confirmations, review, Back and toolbar Undo/Redo. Canonical bytes remain
  unchanged at every pre-Apply interaction. Screenshots were inspected; no QML
  engine warnings were reported in these flows.

The existing polygon-clipping Qt `H used before declaration` warnings remain.
Cloud tests use Qt 6.8.3 with the offscreen/software backend; they establish
functional behavior, not a real-GPU/device performance pass.

## Final local gate

- Full build succeeded; CTest **129/129 passed**, failed **0**, skipped **0**.
- Elapsed CTest time: **688.88 seconds**. JUnit XML and the complete Qt log were
  audited with `tools/m6-regression-audit.py`; no SKIP markers were found.
- Full actual UI suite: **44 passed**, failed/skipped **0**, 163.55 seconds.
- Offline pinned-web checks: **74/74**. Actual-controller comparator: **6/6**,
  including the fresh 17-trace / 108-observation replay.
- Independent review findings were reproduced and fixed with regression tests;
  the final empty-draft Finish regression and differential lifecycle both pass.
- These results precede branch publication. Exact-commit remote CI is checked
  separately after publication; this document does not infer remote success.

## Reproduction

Build all targets with `BUILD_TESTING=ON`. Run the complete CTest suite with the
same pinned full hydro/historical data as `.github/workflows/m97-editing-parity.yml`.
The registered `m972_controller_web_differential` supplies the actual built probe
path and runs the six Node comparator/controller checks. A missing probe is a
failure, never a skipped success. The workflow runs the 74 offline oracle checks
before compilation and the executable-dependent differential within CTest after
compilation.

A–C leaves river source loading/partition/coordinator/sliver work for slice D,
complex cut graphs for M9.7.3, and engine extraction for M9.7.6. No main merge,
release, installation or additional web-source correction is included here.
