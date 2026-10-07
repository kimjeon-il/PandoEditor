# Reference image web parity implementation ledger

App base: 3934077519bb716cbb45b683bbb63d85dcc8d5ee
Web pin: ebcfae4d27b29cbbea6416a7045a4806930204be

Approved sequence: 1 calibration UI; 2 corner pin/anchor; 3 live wire; 4 line refinement; 5 placement controls; 6 vertex keyboard movement; 7 Space pan.

Ruling: Vendor exact pure Web modules with provenance and execute with Qt QJSEngine (already supplied by Qt Quick), rather than maintain a divergent second geographic solver. Native storage/UI remain canonical. The existing pixel-only warp supports existing placement records.
Preflight: tasks 1/2 share normalized geographic mapping; tasks 3/4 consume that mapping and one revision-guarded draft interface; tasks 6/7 use existing map projection/input ownership.
Tests remain focused as explicitly requested; no full CTest or M9.8 run.
Task 1: in progress. Geographic calibration RED test added; execution pending.

Task 1 evidence so far: reference_image_tests baseline/new RED: 8 passed, 1 failed (missing calibration API), then session RED: 8 passed, 2 failed. After JSON-to-JS array conversion and session implementation: 10 passed, 0 failed, 0 skipped; D:/build/reference-parity-session-green.txt. Actual window verification pending.
Ruling: Qt 6.8 QJSEngine cannot parse object spread. Original Web module remains byte-identical; esbuild 0.25.10 produces a checked-in ES2016 runtime bundle. Qt and Node compare identical fixed-source cases. No browser APIs or network are exposed to the runtime.
Ruling: Existing pixel calibration records remain untouched in controlPoints; new normalized geographic records use geographicPoints. This preserves existing local image data without changing project/GIS schemas.

Task 1 expanded RED: 11 passed, 1 failed; failedUndoKeepsTheHistoryEntry reproduced history consumption on failed save. Native fixed-Web corpus (7 cases) passed. First actual-window run failed (missing test flag resources and popup item lookup); flags added without suppressing warnings; rerun pending.

Task 1: complete. Native Qt rows12 passed/0 failed/0 skipped (10 functional slots); fixed Web corpus7 processed/0 mismatch. Final Windows D3D11 geographicCalibrationActualWindowFlow PASS (actual image/map clicks, Undo/Redo and calibrated red framebuffer pixel), menu regression desktop-1100 PASS. Initial menu assertion8 was updated to9 for the new explicit calibration action; prior action indexes and safety checks retained. Logs: D:/build/reference-parity-stage1-native.txt, reference-parity-stage1-window.txt (calibration PASS/menu expected-count FAIL), reference-parity-stage1-menu.txt (corrected menu PASS). Production changes were identical between final calibration and corrected-menu runs; only test expectation changed.
Task 2: not started; tasks3–7: not started.

Task 2 RED: missing setCornerQuad interface. Initial implementation13 Qt rows passed; stronger Web mode assertion then reproduced empty native anchor object treated as a real anchor (TPS instead of projective). D:/build/reference-stage2-mode-red.txt. Absent native anchor is now null. The actual corner/anchor window test is added. Project replacement cancellation observes instance value changes rather than every generic stateChanged notification.

Task 2: complete. Native14 Qt rows passed/0 failed/0 skipped. Normal anchor translates the placement quad without enabling corner pin; corner-pin plus points/anchor uses the pinned TPS hard constraints. Degenerate/crossed quads reject without publication; gesture cancellation and reopen preserve quad/anchor. Windows D3D11 cornerPinAndAnchorActualWindowFlow:3 Qt rows passed/0 failed/0 skipped, actual corner drag→Undo/Redo→anchor click→globe frame. Logs D:/build/reference-stage2-normal-native.txt and reference-stage2-window-final.txt. First window run failed because helper functions were inside the background item rather than MapView; helpers moved to the correct owner and the same route reran. Source manifest2 Git blobs verified.
Task 3: next; tasks4–7 not started.

Task 3 RED: missing beginTrace production session,2 passed/1 failed. D:/build/reference-stage3-red.txt. Implementation uses original pinned Live Wire code in worker-owned QJSEngine and session/request guards; fixed-source expectation3 cases recorded before native execution. Build and focused tests pending.

Task 3 first native run14 passed/2 failed, invalid-gradient-field. Cause: QList append overload flattened each UV pair into scalar elements at the native/JS boundary. Explicit QVariant pair ownership added; fixed-Web expected remains unchanged.

Task 3 complete: reference-stage3-final-native.txt16 Qt rows passed/0 failed/0 skipped;7 calibration plus3 Live Wire fixed-Web cases processed/0 mismatch. Windows D3D11 reference-stage3-final-window.txt3 Qt rows passed/0 failed/0 skipped: actual map anchors, last anchor Undo, finish, apply, draft Undo/Redo, stale request rejection, cancel during worker, existing preview→confirm→project Undo. Earlier window run failed on an undefined calibration binding; guarded absent session state (no warning suppression). Pinned source mapping is reused, tracing requires ready calibration or corner pin exactly as Web. Four original Git blobs verified.

Task 4 RED:2 Qt rows passed/1 failed, missing beginRefine session, D:/build/reference-stage4-red.txt. The original Web gradient/refiner functions and fixed expected3 cases are connected to the same revision guarded worker. Current draft is inverse mapped through the displayed mesh before calculation; apply alone mutates the draft.

Task 4 complete: D:/build/reference-stage4-final-native.txt3 Qt rows passed/0 failed/0 skipped (3 fixed-Web refiner cases,0 mismatch, plus damaged image rejection). Windows D3D11 reference-stage4-final-window.txt3 Qt rows passed/0 failed/0 skipped: current draft→image pixels→refiner→geographic preview; cancel leaves draft untouched; apply has one draft Undo; project replacement cancels stale worker. Initial UI test tried to replace a project while content editing remained active, correctly rejected by existing open guard; revised test verifies that refusal is atomic, then follows explicit content cancellation before replacing. Five original Git blobs verified.
