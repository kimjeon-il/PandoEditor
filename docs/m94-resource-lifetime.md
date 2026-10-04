# M9.4 resource identity and lifetime

This change extends the uncommitted M9.2/M9.3 work on `codex/integration`, based
on `762b4dc8b1fcd169f9886d0730f4d8b83cf3f1df`. It formalizes existing retained
scene graph entries, without adding a GPU cache or implementing M9.5 policies.

## Identity and ownership

`MapResourceIdentity` identifies one QSG geometry allocation by object/ref,
geometry revision, effective engine LOD, projection preparation, consumed buffer
ownership/counts and any baked manual position. Stroke endpoint widths participate
in identity because they are copied into vertices. Draw packets now carry LOD and
preparation policy from the builder; the enum is shared with the existing cache.

Each Entry strongly owns its consumed immutable buffers. The previous scene also
remains alive throughout reconciliation, preventing raw allocation-address reuse
during comparisons. Equal object IDs/revisions in a replacement project do not
alias different source buffers. World resources retain mesh identity and source
slots/slices; overseas slots with the same logical owner remain distinct.

Styles are separate from geometry identity. Existing entries update material
uniforms, and a blend change replaces its material while retaining its geometry.
Same-partition world opacity/color/blend changes retain base buffers. Existing
batch split/merge changes the actual slot/index composition and can construct new
resources; this batching policy is unchanged. Selection overlays can require new
resources independently of the retained base geometry.

The preparation identity fast path still relies on immutable scene/preparation
contracts. Replacing packet buffers requires new preparation identity or relevant
revisions. No packet scan is added to ordinary view updates.

## Window and generation lifetime

GpuMapItem tracks separate bridge, window and resource generations. Scene graph
initialization/invalidation advances the resource generation; queued statistics
and upload continuations from older generations are ignored. Old-window lifecycle
notifications cannot change a newly attached window's state.

Scene graph errors and frame-swap samples capture both window and resource
generations at emission, before queued GUI delivery. A same-window old-context
error cannot overwrite the status of a newly initialized context. The software
regression observes every diagnostic transition, not only the final status.

The initial window is attached even when supplied to the C++ constructor. Its
destructor disconnects callbacks before QQuickItem's base destructor can emit
windowChanged. A software regression exposed the former typed-slot assertion;
the explicit disconnection fixes that process-local teardown failure.

The render thread discards old nodes/statistics on resource generation changes
and rebuilds from the retained immutable frame. Backend fallback and invalid view
retirement clear current live/draw/pending statistics. Existing pending uploads
rescan the current frame, so view changes do not apply an obsolete packet queue.

## Measurement meaning

MapGpuStats separates base and interaction geometry constructions, resource
creation/retirement, and current live count/bytes. Upload counts/bytes measure CPU
QSGGeometry construction and copying, not QRhi or driver operations or VRAM.
Creation/retirement counts cover node reconciliation within a resource generation;
window/context generation reset starts fresh statistics. Destructor release is
also checked through weak buffer ownership, without retaining unsafe statistics
references past node destruction.

The width-only test failed before the fix (one upload instead of two). Regressions
cover changed endpoint widths, revision/ref/LOD/preparation, stable unrelated
resources, base versus selection overlays, pending budget completion, deletion,
equal-ID project replacement, and CPU node destruction/recreation. Bounded world
fixtures verify stable same-partition style updates without loading full assets.

The software window test verifies a genuinely queued old-window invalidation by
asserting generation advancement before moving the item, then checks CPU painting
after window move and hide/release/show with a current view update. This does not
exercise GPU context loss or RHI resource reconstruction.

Build/test logs use `D:/Pandoeditor-release-ui-20261003/logs/m94-*`; the configured
Qt 6.8.3/MinGW Debug `-O1 -g0` build is `C:/Users/taeeu/Qt/Pandoeditor-build`, with
binaries in `D:/Pandoeditor-mechanism-bin-20261003`.

```text
cmake --build C:/Users/taeeu/Qt/Pandoeditor-build -j2
ctest --test-dir C:/Users/taeeu/Qt/Pandoeditor-build --output-on-failure
  -E "^(ui_tests|world_corpus_tests|world_projection_diagnostics|builtin_world_policy_tests|hydro_full_dataset_tests)$"
ui_tests softwareWindowRecreationDropsOldWindowCallbacks
```

The normal regression uses offscreen/software; the bounded live window check uses
Windows/software. Four full-world/data gates, the full 114-test CI audit, real RHI
context recreation, driver GPU counters and long stress tests remain unexecuted.
The PC/kernel shutdown investigation remains unresolved. No commit, push, tag or
deployment is performed in this stage.

## Verification evidence (2026-10-03)

The final build completed successfully. `map_render_tests` passed 30 cases;
the focused software lifecycle regression passed 3, the complete offscreen UI
suite passed 31, and bounded Windows/software UI passed 5. The portable smoke
check exited 0 with QML, Pretendard/Korean glyphs and SVG all PASS. Its existing
polygon-clipping used-before-declared warnings remain visible in the log.
Windows captures are in `D:/Pandoeditor-release-ui-20261003/m94-windows-captures`;
the desktop light editor capture was visually inspected.

Measured CPU/QSG resource deltas: same-partition world opacity/color/blend each
had base upload, overlay upload, create and retire deltas 0; endpoint width
replacement had base upload/create/retire 1 each; selecting an overlay had base
upload 0 and overlay upload/create 1; deletion retired 1 and left live count/bytes
0, with weak scene/position ownership released. Flat and globe ordinary view
fixtures retained zero preparation/copy/upload/tree/attachment deltas and 16
uniform updates. These are not driver GPU allocation measurements.

One final local 109-test run passed 108 and failed `presentation_editor_tests`
without a captured assertion. That executable immediately passed all 13 cases
with a file log; the same CTest path then passed three consecutive repetitions.
No source was changed to mask the unreproduced failure. The first failed run is
preserved in `m94-local109.txt/.xml` and retries in `m94-presentation-*`.
The final full rerun passed **109/109**, failures/skips 0, in 214.01 seconds
(`m94-local109-retry.txt/.xml`). The transient first-run failure remains an
unresolved observation rather than a claimed fix. `view_navigation_tests` passed
14; its 23 camera publications caused 0 scene publications/preparations/copies.
Builder checks passed with color/opacity geometry build/hit deltas 0, and one
geometry change building 2 packets with 0 unrelated cache hits.
`git diff --check` passed; branch/HEAD and the original 2,696,729-byte dump remained
unchanged. No long GPU load or native performance probe was executed.

Three native agents independently handled lifecycle analysis/review,
resource-identity implementation and regression coverage. The primary worker
integrated window generation guards and executed the build/UI checks. Their
reviews found no remaining blocking issue in the reviewed ownership and
retirement paths; real RHI/context testing remains outside this evidence.
