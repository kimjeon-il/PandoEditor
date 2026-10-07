# M9.8 current input and restore instrumentation

Fixed Web `ebcfae4d27b29cbbea6416a7045a4806930204be`; original input
manifest remains immutable. New separate revision `m98-native-v3-prepared-drag`
preserves all 15 scenarios, 120 pan/90 zoom samples, wheel delta5 and the original
four project/view/asset/expected-final-state contracts. A real drag-start move
(20,0) is explicit setup and retained in raw input/journal evidence; measured
moves start at sample1. DEM zoom-out starts from a recorded scale2 preparation
so the same90 delta-5 events do not hit the production zoom floor. This new
starting sequence must not be labeled as the original v2 sequence.

The opt-in probe tags bootstrap/warm-up/cleanup with phase-1. Those original
raw heartbeat events remain; only actual Idle frames carry phase0. Budgets,
required counts, assertions and final assessment rules are unchanged.

`tools/native-performance-prepared-inputs.mjs` validates original hashes and
creates a fresh fixture directory. It refuses existing output and never writes
old fixture/expected/source files. `m98-prepared-input-preservation-05.json`
independently compares all4 roles: original12 script fields, project/view/assets,
editing input and expected-final-state preserved, mismatch0. Previous manifest
`9af0e521085c093596c4b1ab051c90fe1ceb2bc82d73ed4bdaedcafcbfb129e4`;
new manifest `f3251b4625beb79c82c478c7e3ac1740aec8c47e27ed35e53b20b5277f4ad6b8`.
Builder01 failed a duplicated-path check before writing fixtures; builder02
fixes bookkeeping for shared original input and completes. Failed log preserved.

Actual production restore timers record decode/static validation, project
validation, projection, project switching/catalog, publication and hydro sync.
These owner-thread events measure real executed operations in milliseconds;
counts/totals are cumulative for the controller, latest values are gauges.
They do not estimate separate GPU/worker time or change project codecs/history.
Unobserved aggregate stale publications, GPU driver memory/fences and editing
presentation latency remain null rather than fabricated0.

Executed evidence root `D:/Codex/evidence/p1-p8-m98-20261006`:
- `m98-parser-final-04`: Node exit0,16/16 pass,0fail/skip.
- `m98-structure-prepared-06`: PID15068/exit1,Qt15pass1fail0skip;
  old whole-snapshot equality ignored the new6 actual restore events.
- `m98-structure-prepared-green-09`: PID7504/exit0,Qt16/16;
  existing editing counters/events preserved, restore counts/events explicitly
  increment, fresh controller values remain unobserved before real work.
- `m98-timeline-current-07`: actual PID19660/exit0,Qt108/108,0fail/skip;
  saving, restoring, native-only Web refusal, complete archive and failure
  atomicity executed. The launch wrapper incorrectly declared26 Qt rows and
  exits1; that authored count failure is retained, not a native test failure.
- `m98-metrics-final-mutants-10`: copying omitted adjacent test headers and
  compile exited1; it is not a caught runtime regression. The isolated execution
  helper now copies the actual headers, without modifying production worktrees
  or shared build inputs. Rerun11 receipts follow when complete.

Fresh final-source device runs and review are pending at this authored checkpoint.
Full acceptance remains BLOCKED by empty-v1/full-data fixtures. Existing e01f0e6
performance failures are retained; no passing aggregate is inferred from Qt exit0.

Actual isolated rerun11: all3 production regressions caught by real Qt assertions;
- unobserved-duration-as-zero: baseline PID12252/exit0; negative PID6180/exit1,Qt{'passed': 2, 'failed': 1, 'skipped': 0}
- cumulative-total-as-latest: baseline PID11624/exit0; negative PID11740/exit1,Qt{'passed': 2, 'failed': 1, 'skipped': 0}
- event-drop-unreported: baseline PID13460/exit0; negative PID20424/exit1,Qt{'passed': 2, 'failed': 1, 'skipped': 0}
Shared production source and link inputs remained hash-identical during each run.
