# P1–P8 / M9.8 execution follow-up

Fixed Web: `ebcfae4d27b29cbbea6416a7045a4806930204be`.
App base: `2e65cf9f4a1d29e01d406029de60c5c9ff0b6eb5`.
Evidence root: `D:/Codex/evidence/p1-p8-m98-20261006`.

This file distinguishes actual dirty-tree executions from final-commit gates.
The final App candidate, final exchange and repeated device measurements are
not established by the executions below. Failed raw logs remain preserved.

| Execution | Actual result | Scope and limit |
| --- | --- | --- |
| `p8-globe-handoff-red-02` | 7 pass, 1 fail; PID 19152, exit 1 | Reproduced the erroneous horizon closing chord |
| `p8-globe-handoff-green-03` | 8 pass, 0 fail/skip; PID 7484, exit 0 | Controller presentation mechanism; canonical geometry unchanged |
| `p8-actual-presentation-device-01` | 4 pass, 0 fail/skip; PID 17548, exit 0 | Actual Windows D3D11 GPU and painted renderer receipts, nonempty capture, unreferenced archive retained; no manually emitted completion |
| `p8-historical-disconnected-green-02` | 5 pass, 0 fail/skip; PID 1312, exit 0 | Disconnected components omitted only from unrelated Boolean operands |
| `p8-snapshot-original-canonical-04` | 11 operations, 28.5176 seconds; PID 21304, exit 0 | Original snapshot transaction, actual production kernel |
| `p8-snapshot-bounded-canonical-05` | 11 operations, 17.819 seconds; PID 6040, exit 0 | Bounds-filtered snapshot transaction, same actual kernel |
| `snapshot-bounds-exact-comparison-01.json` | 1 comparison, 0 mismatches | All JSON fields, original array order and exact coordinates; no exclusions, sorting or tolerance |
| `p8-catalog-current-04` | 29 pass, 0 fail/skip; PID 13296, exit 0 | Actual catalog/controller execution; original 20-second snapshot timeout maintained |
| `p8-ui-one-window-full-09` | 14 pass, 0 fail/skip; PID 16848, exit 0 | Actual Windows desktop/mobile input and period editing; predates subsequent test-controller reuse |
| `p8-ui-registered-headless-10` | 1 registered CTest process, exit 0, 88.74 seconds | Registered 90-second timeout unchanged; this CTest summary alone does not establish 14 internal rows |
| `boundary16-web-worker-source/result.json` | 4 processed, 0 mismatches; Node exit 0 | Original fixed-Web production Worker planning/validation accepts 2/4/8/16 adjacent owners; Node platform adapter, not browser frame timing |
| `m98-editing-current-03` | 14 processed, 13 diagnostic measured, 1 fail; native exit 1 | Shared-boundary 16-owner preview failed; all failures retained |
| `m98-editing-kernel-reproduction-04` | 14 processed, 1 fail; native exit 1 | Same failure, rejected microscopic intersection recorded in stderr |
| `boundary-sixteen-red-02` | 2 pass, 1 fail, 0 skip; PID 17644, exit 1 | Minimal actual-kernel preview repro: `INVALID_GEOMETRY: degenerate ring` |
| `boundary-sixteen-green-04` | 3 pass, 0 fail/skip; PID 13384, exit 0 | Same 16-owner repro; every resulting draft coordinate and owner checked exactly, project unchanged during preview |
| `boundary-area-geometry-green-02` | 11 pass, 0 fail/skip; PID 4148, exit 0 | Canonical input validation, cancellation, strict degenerate-storage rejection and scalar calculation |
| `place-provider-native-confirmed-03` | 10 pass, 0 fail/skip; PID 19260, exit 0 | Actual provider native process and exit now confirmed |
| `m98-structure-current-16` | 14 pass, 0 fail/skip; PID 15120, exit 0 | Production metrics and worker lifecycle structure |
| `boundary-engine-current-01` | 11 groups, 0 failures; PID 11256, exit 0 | Callback ownership, error and cancellation contracts |
| `m98-editing-area-correction-05` | 14 processed, 0 failures, native PID 21960, exit 0 | 1,200 actual pointer queries; Annex, river, four original complex splits, 2/4/8/16-owner controller edits, Apply/Undo/Redo/cancel; DIAGNOSTIC |
| `p4-real-dem-controller-current-03` | 12 internal checks, 3 Qt rows, 0 failures/skips; PID 21280, exit 0 | Actual prepared DEM and Windows D3D11 gray/color/theme/pan/reversal/dateline/globe/hidden/None transitions, eight captures; capture is not an independent pixel oracle |
| `m98-structure-actual-mutations-02` / `03` | 5 mutants caught by named runtime assertions, native negative exit 1, no skip; generator/executor exit 0 for completed groups | Milliseconds, cancellation elapsed, unobserved null, latest versus cumulative, dropped-event reporting; compile failures excluded from caught count |
| `place-density-prefilter-red-01` | 24 processed, 21 pass, 3 fail; PID 5252, exit 1 | Reproduced all three application quality-prefilter differences |
| `place-density-prefilter-green-02` | 24 processed, 24 pass, 0 fail/skip; PID 22320, exit 0 | Reduced/full density, protected labels and stable source order; old direct-layout cases preserved |
| `place-density-cross-comparison-04.json` | 42 compared, 0 mismatches; native PID 8668, exit 0; Node exit 0 | Previous 39 cases plus three fixed-Web application quality cases; four font geometry differences reported without tolerance |
| `p8-ui-current-counted-11` | 14 pass, 0 fail/skip; PID 16728, exit 0, 88.353 seconds | Current controller-reuse UI harness with explicit Qt file logger; desktop/mobile period editing, focused Undo, project replacement and GPS selection |
| `place-density-controller-current-05` | 10 pass, 0 fail/skip; PID 15428, exit 0 | Current production label-density wiring preserves readonly selection, explicit copy, history and persistence mechanisms |
| `m98-structure-density-current-18` | 14 pass, 0 fail/skip; PID 9584, exit 0 | Current production controller structure after density wiring |
| `m98-parser-current-17` | 16 Node tests, 0 failures/skips; exit 0 | Measurement assessment and incomplete-evidence rejection |
| `final-discovery-checkpoint-01.json` | 205 registered CTests discovered, native CTest exit 0 | Discovery only; not 205 executed tests. Required new terrain, presentation, place, period, structure and timeline gates present |

## Boundary mismatch responsibility

The valid source partitions the square `[-4,4] × [-4,4]` into 16 triangles
sharing `(0,0)`. Moving only that shared node to `(1,0)` preserves the outer
union. The input is recorded in both the fixed-Web Worker result and native
editing raw JSONL. The earliest divergence is native preview overlap checking,
before commit. Web returns a nonblocking successful result; native rejected an
ephemeral intersection whose computed ring is
`[[-0.6666666666666676,-0.6666666666666672],[1,0],[-0.6666666666666667,-0.6666666666666666],[-0.6666666666666676,-0.6666666666666672]]`.

Responsibility: App. Web applies its existing area significance predicate to
the calculation result and strictly validates the proposed stored geometry.
The App correction separates scalar predicate output from persistent geometry.
It uses the same pinned production kernel and strict canonical operands,
returns only an area scalar, and retains strict persistent validation. No
stored coordinate, precision, significance threshold, fixture, expected data
or schema is changed. The exact 16-owner preview correction is green as listed
above. The full 14-case controller editing diagnostic also completed without
failures. This scalar correction does not establish frame latency or full-data
performance acceptance.

## Remaining gates

- Production places are the original byte-identical `empty-v1` manifest.
  Actual populated-data P5/P7 and full-data M9.8 acceptance are BLOCKED.
- Final App/Web production exchange, M32 counts, source/binary hashes, final
  discovery audit, structural mutations and repeated device diagnostics are
  pending and must be recorded separately when executed.
- Retained terrain pressure/source/context/mask mechanisms now have the actual
  Windows execution and caught device regressions below. Official DEM pixel
  parity is a separate corpus gate. Cap/join/antialiasing visual differences
  remain incomplete. Density prefilter
  implementation and the listed fixed-Web mechanism comparison are green.
- Android physical-device execution is NOT RUN; no connected physical device
  was observed. No package is authorized.
- Earlier independent agent reviews do not cover subsequent root edits.

Work remains on `codex/m97-web-editing-parity`. No main merge, deployment,
tag, release, installer or portable package is part of this continuation.

## Regional edit pixels and current source checkpoint

App `6afe93deb83363c69674276eb7d5c2b4fdf11a18`, fixed Web unchanged.
The fixed Web `app-map-projection.js` uses `d3.geo.equirectangular` for flat
rendering and editing. Native regional edit coordinates previously applied a
separate latitude cosine while the geographic renderer used uniform axes.
An adopted camera was also clamped while merely deriving display coordinates.
These App defects put Germany's edit points outside the actual visible map.
The correction aligns regional local coordinates and leaves interactive zoom
bounds on zoom mutations. Stored geometry, schema and exchange inputs did not
change. The selected integer vertex coordinates were not moved or loosened.

| Execution | Actual result | Scope |
| --- | --- | --- |
| `native-geometry-pointer-diagnostic-02` | PID 11992, exit 1; 2 Qt pass, 1 fail, 0 skip | Real D3D11 window: selection and edit button work, vertex selection fails; nearest old overlay point was outside the viewport |
| `regional-projection-red-02` | PID 19644, exit 3 | Independent geographic pixel formula detects native mismatch |
| `regional-projection-green-03` | PID 15844, exit 3 | Latitude-only correction insufficient: adopted-view display clamp still mismatched |
| `regional-projection-green-07` | PID 15068, exit 0; 4 checks, 0 failures | Regional geographic pixel formula after both corrections |
| `regional-camera-green-08` | PID 7120, exit 0; 5 groups, 3 restored centers, 0 failures | Cursor geographic anchor and existing camera controls |
| `regional-edit-coordinates-green-09` | PID 21944, exit 0; 4 groups | Existing pure expression/snap contracts unchanged |
| `regional-edit-controller-green-10` | PID 17012, exit 0; 56 Qt pass, 0 fail/skip | Territory/content pointer and component state |
| `native-geometry-pointer-draft-green-04` | PID 11284, exit 0; 3 Qt pass, 0 fail/skip | Actual D3D11 point selection, changed draft, edit Undo availability, preview and cancel; document/dirty/Undo/Redo preserved; fixed vertex difference 0.482465 CSS pixels |
| `web10-current-6afe93d-06` | Node exit 0; 16/16, 0 mismatch/fail/skip; 24/24 traces, 28/28 real native processes | Same fixed Web; web-app-web 6/6, app-web-app 6/6, rejection 4/4; intermediate production codec output compared |
| `m32-current-6afe93d-05` | Actual native exit 0; expected/processed 2525/2525, mismatch 0 | Historical property oracle, separate from current-Web exchange evidence |
| `timeline-storage-current-6afe93d-03` | PID 22396, exit 0; 74 checks, 0 fail/skip | Native and production Web intervals, full archive and strict storage |
| `timeline-focused-current-6afe93d-03` | PID 20616, exit 1; 0 selected, 1 failure | Shell argument list was interpreted as one Qt function; no selected tests executed, not passing evidence |
| `timeline-focused-current-6afe93d-04` | PID 13776, exit 0; 26 Qt pass, 0 fail/skip | Correct argument array; atomicity, history/archive and explicit native-only Web export refusal |

The ordinary measurement harness additionally requires the real pointer drag
to change the draft and preserves dirty/Undo/Redo on cancel. Its per-input
snapshot includes the real `geometryEditChanged` revision. GUI edit/menu fields
are captured after input and attached at the same window's synchronization;
the immutable base render frame and owner are independently observed. This is
not a claim of independent edit-overlay pixel latency, GPU fences or DWM.

## Retained terrain device mechanisms

| Execution | Actual result | Scope |
| --- | --- | --- |
| `retained-terrain-handoff-native-01` | PID 20720, exit 1; 2 Qt pass, 1 fail | Test mask omitted its geographic viewport transform; harness corrected, production unchanged |
| `retained-terrain-handoff-native-02` | PID 18344, exit 1; 2 Qt pass, 1 fail | Read a transitional retirement statistic; settle now requires bytes, zero overflow and zero staging together |
| `retained-terrain-device-mutations-01` | Negative PID 8572, exit 0; mutant survived | Earlier post-adoption readback could conceal the broken first gray frame. This execution is FAIL and superseded by the strengthened test, not counted as caught |
| `retained-terrain-device-mutations-02` | 2/2 compiled device mutants caught, generator exit 0 | Baseline PIDs 3828/20668, exit 0, 3 Qt pass each; negative PIDs 21748/11272, exit 1, named 2 pass/1 fail/0 skip assertions; production/shared object/library/binary hashes unchanged |
| `retained-terrain-owner-full-04` | PID 12576, exit 0; 8 Qt pass, 0 fail/skip; new internal checks 8/8 | Actual Windows GTX 1650 D3D11 retained owner/material/mask/window path with explicit synthetic pixels |
| `retained-terrain-device-mutations-03` | 2/2 caught after explicit ready prefetch was added; generator exit 0 | Baseline PIDs 10980/19108, exit 0, 3 Qt pass each; negative PIDs 4088/19484, exit 1, named 2 pass/1 fail/0 skip assertions; shared inputs unchanged |
| `retained-terrain-owner-prefetch-full-05` | PID 5848, exit 0; 8 Qt pass, 0 fail/skip; new internal checks 8/8 | Current detail plus prefetch pressure inputs and complete original owner test list |

The eight new checks cover baseline receipts, detail deferral at a 96-byte
nominal backing budget (including a separately prepared prefetch), incomplete
new-source reserve, delayed physical mask,
first gray sea suppression while old source backing is still resident,
settled zero overflow/staging, actual replacement window/context re-upload and
old receipt refusal, and final resource release. GUI adoption is held only
after a real current display receipt so readback cannot retire the old source
and mask a same-frame defect. Two isolated regressions disable that suppression
or evict the adopted reserve before replacement; both fail actual RHI pixel
assertions. These are synthetic mechanism checks, separate from the preserved
1,282-file DEM inventory and real controller/corpus results.

## Registered native geometry execution receipt

The Windows CTest registration now writes an explicit Qt text result file,
because the GUI executable's stdout was empty through the installed CTest
launcher. This changes logging only, not assertions or production behavior.

| Execution | Actual result | Judgment |
| --- | --- | --- |
| `native-geometry-registered-a3942bb-05` | CTest exit 0, but no internal Qt count or native PID in stdout | Missing execution evidence; not a completed gate |
| `native-geometry-registered-counted-06` | CTest exit 0; Qt 3 pass/0 fail/0 skip; program PID 9352; native OS exit unobserved | INCOMPLETE: direct parent lookup missed the launcher grandchild |
| `native-geometry-registered-counted-07` | CTest PID 19860/exit 0; actual native PID 21960/exit 0; Qt 3 pass/0 fail/0 skip; declared/processed 1/1; D3D11 | PASS, actual process and complete internal receipt |

The successful observation identifies the unique freshly created native
process by its exact executable path and named Qt function, retains its process
handle, and records its OS exit code separately. Source checkpoint was
`a3942bb184d7046b5edd88059419c20e865aff8b` plus this CMake logging change.
Executable SHA256 was
`86f63581b789d18f04a2465a452dd29aaa15892bc1e1d62786e3e17bc7326493`.
Raw stdout, stderr, Qt log and receipts, including both rejected observations,
are preserved in `D:/Codex/evidence/p1-p8-m98-20261006/`.
