# M9.2 frame pipeline

Scope: M9.2 only, based on `762b4dc8b1fcd169f9886d0730f4d8b83cf3f1df` on
`codex/integration`. M9.3 through M9.8 are not implemented by this change.

## Publication and preparation

`MapFrame` atomically publishes immutable prepared `RenderScene` storage together
with the current `MapViewState` and current world culling/wrap plan. Both software
and scene graph renderers consume this coherent frame. Camera changes retain the
exact scene pointer, avoiding packet vector copies, scene publication and backend
draw-sequence scans.

| Trigger | Route |
| --- | --- |
| Pan, zoom, rotation, resize with unchanged resources, projection and effective LOD | ViewOnly; current culling/wrap plan and uniforms |
| Selection, hover or edit target with already prepared packets | InteractionOnly; existing geometry storage |
| First highlight requiring a missing hidden-boundary stroke | FullPreparation |
| Coarse fallback feature entering/leaving protected selection/editing | FullPreparation for actual LOD promotion/demotion |
| Project instance/document, projection, effective background LOD, world or hydro frame replacement | FullPreparation |
| Semantically identical view and identical scene publication | No publication |

Resource identities participate in signatures even when numeric revisions coincide.
Project replacement preserves monotonic publication revisions while forcing fresh
preparation. Camera movement does not disable an otherwise eligible geometry patch.
Labels, viewport resource scheduling and autosave retain their existing view signals.

## Evidence and limits

Regression fixtures use the real builder, frame pipeline and scene graph node,
including flat and globe camera sequences. With a warmed stable draw set, measured
preparation, scene-copy, geometry-upload, uploaded-byte, tree-reconciliation and
attachment deltas are zero; uniform delta is 16 for each projection fixture.
Controller fixtures measure 23 view publications with zero scene publications,
preparations or scene copies.

Separate bounded world fixtures verify that culling changes follow the current
view and antimeridian wrapping creates the required slots. Changing the visible
draw set can legitimately reconcile the tree or construct new QSG geometry.
Pending upload continuation keeps its existing path.

The scene graph counters measure CPU QSG construction/copy bookkeeping. They do
not certify actual QRhi/driver uploads, native GPU performance or GPU lifetime.
No long native performance/GPU stress probe is run. `viewFrameCount` counts view
publications, including views subsequently requiring full preparation.

Build/test artifacts are stored under
`D:/Pandoeditor-release-ui-20261003/logs/m92-*`; binaries are built to
`D:/Pandoeditor-mechanism-bin-20261003`. The configured Debug build uses `-O1 -g0`.

Commands (Qt 6.8.3 and MinGW 13.1 on PATH):

```text
cmake --build C:/Users/taeeu/Qt/Pandoeditor-build -j2
ctest --test-dir C:/Users/taeeu/Qt/Pandoeditor-build --output-on-failure
  -E "^(ui_tests|world_corpus_tests|world_projection_diagnostics|builtin_world_policy_tests|hydro_full_dataset_tests)$"
```

The local regression command uses `QT_QPA_PLATFORM=offscreen` and
`QT_QUICK_BACKEND=software`. The excluded full-world/data tests require their own
asset/diagnostic environment. Their earlier baseline CI result is not evidence
for this uncommitted change. Limited UI checks use Windows with the software
backend and do not exercise the native GPU performance probe.

Final local general regression: **109/109 passed, 0 failed, 0 skipped** in
238.49 seconds (`m92-local109-final.xml` and `m92-local109-final.txt`). All targets
were rebuilt before this run. An intermediate new fallback test omitted required
presentation data; its fixture was corrected. A separate hydro identity test
failed before the signature fix and passes in the final run.

Final functional UI: **30/30 passed, 0 failed, 0 skipped** in 87.239 seconds
(`m92-ui-final.txt`, offscreen/software).
Limited Windows/software UI: **4/4 passed, 0 failed, 0 skipped** in 24.219
seconds (`m92-windows-ui.txt`; includes init/cleanup and two functional cases).
Ten Windows desktop/compact captures are saved in `m92-windows-captures`; the
desktop light editor image was visually inspected. The rebuilt product's
`--smoke-check` exits 0 with QML, Pretendard/Korean and SVG checks passing;
world bootstrap and autosave are disabled (`m92-smoke-stderr.txt`).

No full 114-test CI/audit claim is made for this change: four full-world/data
tests are excluded locally, and the full regression audit requires them.
No commit, push, tag or release was performed for M9.2.
