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
- Additional actual terrain pressure/source/context cases and
  cap/join/antialiasing visual differences are not complete. Density prefilter
  implementation and the listed fixed-Web mechanism comparison are green.
- Android physical-device execution is NOT RUN; no connected physical device
  was observed. No package is authorized.
- Earlier independent agent reviews do not cover subsequent root edits.

Work remains on `codex/m97-web-editing-parity`. No main merge, deployment,
tag, release, installer or portable package is part of this continuation.
