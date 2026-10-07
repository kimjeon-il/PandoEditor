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
