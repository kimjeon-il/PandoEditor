# M9.3 dirty delta

M9.3 extends the uncommitted M9.2 work on `codex/integration`, based on
`762b4dc8b1fcd169f9886d0730f4d8b83cf3f1df`. No M9.4 GPU lifetime/cache redesign
or M9.5 cache consolidation is included.

## Contract and ownership

`ChangeImpact.sceneDirty` carries explicit geometry, presentation, interaction and
dataset/resource causes, `affectedObjects`, `geometryObjects`, and `fullRebuild`.
Empty object lists mean no identified object changes, not an unknown global change.
The core owns document causes; selection/hover are supplied by the UI interaction
packet and handled by the existing M9.2 interaction route.

`calculateChangeImpact(before, after, targets)` compares actual validated immutable
documents, not command names. Geometry differences include object creation/removal,
both old and new bindings, immutable shape identity and fallback preparation policy.
Distribution entries inherit their referenced territory's effective geometry changes.
Metadata, relation/ref rewrites, layer/group presentation and distribution scaling
changes conservatively include objects from both endpoints. This broad presentation
closure favors correctness while retaining unaffected geometry storage.

Dataset, version, source and project document identity changes require a full rebuild.
Hydro hidden IDs are a presentation cause. Opaque extension changes conservatively
require full preparation. Command preparation calculates impact before mutation;
presentation rebasing retains a staged full fallback.

`MapSceneBuilder::refresh` owns the scene decision. It compares its retained prepared
snapshot with the current document, covering separate presentation commands and
multiple commits even when only the last pending impact is available. Pending full
and resource invalidations remain conservative. Undo/redo classify their actual
restored state, rather than replaying stored forward impact.

Delta eligibility retains project instance, prepared scene identity, projection,
effective LOD and world/hydro resource guards. Coarse fallback protection changes
still require preparation. Completing an ordinary territorial edit can remove the
edit target without disabling its geometry delta.

## Packet behavior

The delta rebuilds affected draw metadata (style, visibility, order, layer opacity,
label text/settings) while taking unchanged geometry directly from the previous
scene. A cache eviction cannot force color/opacity changes to regenerate geometry.
Only geometry-dirty objects, newly visible objects without retained packets and
missing highlighted boundary strokes visit the geometry cache/preparation route.
Picker bounds and derived geometry label anchors receive geometry objects only.

`sceneDeltaUpdateCount` and `scenePresentationUpdateCount` are exposed in render
quality diagnostics. Presentation and preparation counters are work counters, not
mutually exclusive path counts: missing geometry during a presentation update may
increment both. The three M9.2 frame routes continue to describe publication.

## Regression evidence

The new zero-cache-budget test failed before direct packet reuse: unchanged fill
storage was replaced. After the fix, measured color and opacity changes each have
geometry cache build/hit deltas **0/0**. A single territorial geometry edit builds
exactly **2** packets (fill and boundary), with **0** unrelated cache hits and stable
unrelated buffers. A referenced distribution geometry changes with its territory.

Controller style apply/undo/redo measures **3** deltas with preparation/cache build
deltas **0/0**; the separate presentation opacity command measures **1** delta with
the same zero increments. Tests compare delta behavior with an independent full
build for layer opacity/order, group settings, merge/ref rewrites and coalesced
commits. Delete/undo/redo, hidden hydro and source/version invalidation are covered.

Logs use `D:/Pandoeditor-release-ui-20261003/logs/m93-*`; the configured build is
`C:/Users/taeeu/Qt/Pandoeditor-build` (Debug `-O1 -g0`, Qt 6.8.3/MinGW 13.1), with
binaries under `D:/Pandoeditor-mechanism-bin-20261003`.

```text
cmake --build C:/Users/taeeu/Qt/Pandoeditor-build -j2
ctest --test-dir C:/Users/taeeu/Qt/Pandoeditor-build --output-on-failure
  -E "^(ui_tests|world_corpus_tests|world_projection_diagnostics|builtin_world_policy_tests|hydro_full_dataset_tests)$"
```

These are CPU/headless QSG and software UI checks. They do not establish native
driver uploads or RHI GPU performance. Long GPU stress probes are excluded; four
full-world/data tests and the full 114-test CI audit are separate unexecuted gates.
No commit, push, tag, release or deployment is authorized/performed in this stage.

Final general regression: **109/109 passed, 0 failed, 0 skipped**, 245.49 seconds
(`m93-local109.xml` and `m93-local109.txt`), after rebuilding all targets.
The focused controller suite passes **14/14**, with the measured delta counters
above (`m93-view.txt`). The builder executable exits 0 with its cache-budget-zero,
dependency, history, merge/ref rewrite and coalescing assertions (`m93-builder.txt`).

Native agent roles were split without shared-file conflicts: `frame_flow` owned
the core classifier and reviewed final invalidation; `frame_tests` owned builder
behavior regressions; `gpu_flow` owned controller/history integration and view
tests, then reviewed packet/resource guards. Root implemented the builder delta
and ran all builds and checks. Independent final reviews found no introduced
blocking issue.

Final functional UI: **30/30 passed**, 0 failures/skips, 86.942 seconds
(`m93-ui-final.txt`, offscreen/software). Limited Windows/software UI: **4/4
passed**, 0 failures/skips, 25.734 seconds (`m93-windows-ui.txt`; two functional
cases plus init/cleanup). Ten images are saved in `m93-windows-captures`; the
compact light editor capture was visually inspected. Product `--smoke-check`
exits 0 with QML, Pretendard/Korean and SVG checks passing, autosave/world off.

The final rebuilt renderer suite passes **19/19** (`m93-render.txt`). Its real
builder/frame/node fixtures retain the M9.2 flat/globe warm gate: preparation,
scene copy, QSG upload/bytes, tree and attachment deltas **0**, uniform delta
**16** for each projection. This remains CPU structural evidence, not driver GPU
certification. `git diff --check` passes and the original dump is unchanged.
