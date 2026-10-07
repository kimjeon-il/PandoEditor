# M9.8 actual projection route preparation

Fixed Web: `ebcfae4d27b29cbbea6416a7045a4806930204be`.
Evidence: `D:/Codex/evidence/p1-p8-m98-20261006/`.

`m98-device-cf-v3-91` run1 really launched native PID20576 and exited1,
Qt2pass/1fail/0skip at451891ms. All15 primary scenarios completed, but a
repeat-cycle projection click failed: `projectionGlobeButton` was hidden.
The journal retains the actual pointer and step failures. No completed
measurement.json exists. Run2/PID16848 was explicitly interrupted to diagnose
the failed route; run3 NOT RUN. `interruption.json` preserves that distinction.

The old measurement reused a general navigation helper that checked visible
and used fixed50/100ms waits; it did not prove the popup was opened or that
the preparation click's center was inside actual ancestor/window clipping.
QML resets section onOpened and animates height170ms. Independent review
confirmed the readiness gap, but the Basic-style failure's exact cause is
not established: Basic has no default enter animation. No production popup,
projection setter, timing threshold, input count or expected data changed.

The measurement now uses a separate actual-pointer route helper. One5-second
deadline covers the whole preparation, including actual opened, root/target
section, enabled/visible and clipped-center conditions. Each preparation
click occurs once; no retry/direct setter/section mutation hides failure.
Deadline expiry rejects even a late successful condition. The existing
general helper remains unchanged. The final projection click stays inside
the original measured input/frame association; its failure propagates.

Actual Windows11/Qt6.8.3/D3D11 GTX1650 evidence:
- `popup-route-red-93`: PID20872/exit0,Qt3/3,40 ordinary routes. It did **not**
  reproduce the original451-second Basic failure; do not label it RED proof.
- `popup-route-controlled-red-94`: PID19156/exit1,Qt2pass/1fail/0skip.
  A test-only real QML400ms enter transition reproduces opened=false before
  submenu selection. This is a controlled mechanism, not production styling.
- `popup-route-controlled-green-95`: PID21936/exit0,Qt3/3,40/40/0mismatch/skip.
- `popup-route-deadline-green-96`: PID21944/exit0,Qt3/3,40/40/0mismatch/skip;
  includes the independent review's strict deadline correction.

These were dirty-source checkpoints; receipts preserve binary/source/dirty
identity. They do not certify three final-source15-scenario/12-minute runs.
Those must be executed again after freezing the new source. Original
incomplete/failing reports remain unchanged. Full acceptance remains BLOCKED
by `empty-v1` and missing authentic four full-data fixtures; unobserved stale
aggregate/GPU/driver/edit presentation metrics remain null.
