# P8 stroke implementation and current execution

Fixed Web: `ebcfae4d27b29cbbea6416a7045a4806930204be`.
App execution base: `1428714` with the recorded scope-owned changes.
Evidence root: `D:/Codex/evidence/p1-p8-m98-20261006`.

Production rendering now derives connected topology without changing source
geometry, IDs, archive, contracts or project schemas. The GPU uploads bounded
body/join/cap geometry only when geometry changes. Cap, join, dash, AA and blend
styles update material/presentation state. Painted strokes use connected paths
and geographic dash phase. World-owner chains reset independently. The exact
fixed-Web sources are supplemental pins; prior oracle sources stay unchanged.

`stroke-topology-01`: actual native PID6040, OS exit0; declared/processed40/40,
zero mismatch/skip. Original Web production `buildGpuStrokeInstances` is compared
against the runtime native topology helper without tolerance or array sorting.
This is topology evidence, not an actual browser framebuffer comparison.

`stroke-pixel-red-02`: PID284, exit2, Qt2pass2fail0skip, reproduces missing GPU
round caps and painted restarted dash gaps. The shader change initially crashed
D3D11: `stroke-pixel-green-03` PID13760/exit-1073741819, same under the basic render
loop. GDB and direct D3DCompile isolated optimizer stack overflow in a horizon
loop nested beneath the node/body branch. Moving the unchanged 16-step search
outside that branch and calculating only depth in the search compiles with
normal optimization and renders successfully. Failed intermediate hypotheses
and logs are retained; no production compiler-optimization bypass is used.

`stroke-style-final-23`: PID10092/exit0, Qt4/4, GPU12 and painted11 displayed
pixel predicates. `stroke-dpr2-24`: PID20740/exit0, same counts at actual DPR2.
Covers round/butt caps, connected round/miter join, continuous dash/gap,
GPU analytic 1 CSS px AA, AA-off, alpha, Multiply and interpolated endpoint width.
The earlier `stroke-style-22` overlaps an unfinished build and is excluded.

`stroke-horizon-red-26`: PID18232/exit2, Qt4pass2fail0skip. The initial GPU window
was not exposed (harness); painted clipping added a false round cap at the
geographic horizon (runtime defect). Explicit window exposure and a joined,
flat-ended clipped shape preserve the original visible cap without adding a
new cap at the cut. `stroke-horizon-green-28`: PID6668/exit0, Qt6/6; extra3 horizon
pixel predicates per backend. Captured screenshots and native/binary receipts
are retained. Later DPR2/final execution records follow separately.

Each hardware test requires D3D11; the explicit registration option defaults
off so headless success is not presented as hardware execution. CPU geometry
handoff was separately executed (`stroke-handoff-cpu-04`, PID13412/exit0,Qt5/5).
Whole-browser framebuffer equality and driver fences/VRAM remain unobserved.

Final related execution before commit:
- `stroke-topology-final-38`: PID12336/exit0;40/40/mismatch0/skip0,
  supplemental source manifest `b23dce9e764de1819d77afc12dc09c0a9fed46814083f48432c008fb42cd96fc`.
- `stroke-final-37`: PID2376/exit0 and `stroke-final-dpr2-39`: PID18240/exit0,
  Qt6/6 each, GPU15 + painted14 explicit pixel predicates each. Binary hashes
  are in the receipts. These supersede earlier results for the latest changes.
- `stroke-map-green-36`: PID16492/exit0,Qt7/7; flat/globe view-only reuse,
  unchanged attachment reuse, variable-width/hole and Multiply/selection pixels.
  Failed `stroke-map-related-30` and `stroke-cpu-diagnostic-34` are preserved.
  Variable-width regressions were fixed (base width0 allowed; cap/body winding
  matched). The pure Multiply pixel remains exact black; selected overpaint
  expects independently executed fixed-Web .105 alpha/#cda95d tint with the
  existing one-code-value quantization bound. No shared oracle expected changed.
- `stroke-presentation-exposed-41`: PID8864/exit0,Qt4/4, actual GPU/painted
  successor receipts retire retained predecessor strokes and preserve archive.
  `stroke-presentation-final-40` failed initial GPU exposure under hidden native
  startup. The explicitly exposed rerun, not that failed run, supplies evidence.

Independent final review found an obsolete 4x7 endpoint-width test layout,
omitted clipped-painted stroke identity, and missing variable-width miter joins.
RED48 (PID20400/exit1) and RED49 (PID20904/exit2) reproduced those failures.
The layout now checks all12 vertices/14 floats and keeps upload/retirement
assertions. Actual clipped draws publish their identity; the painted variable
body uses the same .08/4 bounded-miter endpoint rule as the fixed GPU path.
GREEN50 (PID10064/exit0) and DPR2 GREEN51 (PID15016/exit0) each run6 Qt rows,
16 GPU/15 painted pixel predicates and an actual current-frame painted horizon
inventory assertion. Related63 (PID22232/exit0) runs8 Qt rows with no failure/skip.
The same independent reviewer confirmed all three fixes by source/log inspection;
no reviewer build or device execution ran concurrently. These checkpoint results
are separate from the forthcoming frozen-source executions.
