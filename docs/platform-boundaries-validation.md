# Platform boundary refactor validation

Date: 2026-10-07. Branch in both repositories: `codex/platform-boundaries`.
Web baseline: `008b99b5ca2dd39936e51f7ddd11c0c70fc7bb74`.
App baseline: `3934077519bb716cbb45b683bbb63d85dcc8d5ee`.
Web implementation/contract: `ea3f690c2aebaafa2ee15128f149f016eec4bcd1`.
The final execution report records the App HEAD separately, avoiding a self-referential
commit field. `platform-portability-pin.json` pins committed and working Web bytes.
Implementation stays in the two original checkouts. A detached baseline-only
checkout at `D:/build/Pandoeditor-portability-baseline-source` was additionally
created to classify the mobile chooser failure; it is not an implementation branch.

## Changes and preserved boundaries

GeometryDraft moves detached geometry/history/index transitions into the existing
map engine. GeometryEditSession retains Qt state, tool settings, requests and jobs.
All commit/cancel callers still perform their existing notification and scheduling
sequence. selectedId derives from SelectionState and the current project instance.
Project, ProjectDocument, CommandProcessor, ChangeSet, JobRunner, codecs, autosave,
provider caches and renderer ownership are unchanged. No schema/geometry algorithm
or unsupported-feature behavior is changed. See the Web responsibility map at
`docs/architecture/platform-boundaries.md` at the pinned commit.

Independent review found a remaining Web reference-image import after the storage
adapter extraction. It was corrected and all six reference-image browser scenarios
and four storage unit cases passed. No review findings remain unaddressed.

## Evidence

- Before changes: five native CTest executables passed: command, edit geometry,
  command editor, selection editor, timeline editor persistence.
- After changes: eight focused CTest executables passed (zero skipped): those five
  plus selection state, timeline records and timeline storage. GeometryDraft history
  additionally covers exact undo/redo, vertex redo invalidation, cancellation and
  object drag/no-op boundaries.
- Supplemental job, territorial geometry and screen-edit controller executables
  passed. The original aggregate selection UI (90 seconds) and screen-edit UI
  (180 seconds) invocations timed out; they are not counted as passes.
- Windows Qt selection input exposed missing default-flag resources in the test
  executable. Current resources are now linked into both relevant UI test targets,
  as in the existing other UI tests. No warning assertions were removed or relaxed.
- Windows Qt editing input: 12/12 scenario rows passed (14/14 including init/cleanup,
  zero skipped, exit 0). Vertex drag, object drag, no-motion and active-tool replacement
  cover desktop/mobile mouse/touch. Selection search/modifiers, keyboard/hidden-search
  and draft-preserving navigation passed all six desktop/mobile rows; desktop chooser
  cancellation/stale-result checks also passed. Mobile chooser activation failed at
  the first visible assertion. The untouched `3934077` baseline was separately
  built and reproduced the identical Windows input failure at selection_ui_tests.cpp
  line 318 (exit 1; init/cleanup pass, one scenario fails, zero skipped). Current
  offscreen Qt also reproduces it. This is a recorded pre-existing failure, not a
  repaired feature or a passing scenario; production UI behavior stays unchanged.
- Shared selection: 14 expected/14 Web/14 native; zero mismatches. Input SHA-256:
  `22b960c42f494449953853cd1e0d06083905660794f683b4648240074d16a02d`.
  Frozen Web oracle SHA-256:
  `61e5b9b1fc6592cdaae2306853bc60ec93c6668a5449f6d2e39ba3b7811ac453`.
  Native probe SHA-256:
  `bd2368cc50f30e7e119184cdee96e27757e41004ba1bd1597bbdbeae7efe5888`.
- Provenance rejection: a separate local clone with an altered working manifest
  was rejected before probe execution (expected verifier exit 1). Original fixture
  bytes were not edited for this negative check.
- Timeline: 60/60 record verdicts; 25/25 storage verdicts; 11/11 complete storage
  snapshots match. Prior timeline fixtures/source pins remain untouched.
- Web: 147 focused unit cases, four reference-image storage cases, 13 Python
  boundary checks, 12 focused browser cases and one Worker coastline handoff pass.
  Final follow-up of the changed factories/import and shared corpus: 38/38 passes
  (overlaps the preceding unit counts). ESLint and runtime boundary checks pass.
- Broader browser handoff invocation: 21 scheduled, one completed/pass, 20 not
  completed after narrowing the run. The 12 focused browser cases above are a
  separate invocation set. No failed or unexecuted row is counted as a pass.

Raw local logs and final source/binary reports are preserved in
`D:/Codex/evidence/platform-boundaries-20261007`. Counts above do not include
aborted runs as successes. Packaging, deployment, full application parity, all
geometry/network/provider inputs, and hardware GPU parity were not tested here.
Qt software-rendered mouse/touch/keyboard inputs verify the selected production
QML flows, not every existing screen or Android hardware. Existing platform gaps
are left unchanged. Reference-image storage/placement remains platform-specific;
native JSON storage and Web schema-9 exchange are not interchangeable containers.
Timeline serialization does not enable unsupported timeline activation or editing.

## Reproduction

Build targets with CMake in a Qt 6.8.3/MinGW 13.1 environment:

```powershell
cmake --build BUILD --target command_tests edit_geometry_tests command_editor_tests selection_editor_tests timeline_editor_persistence_tests selection_state_tests timeline_records_tests timeline_storage_tests selection_probe job_tests territorial_geometry_tests edit_screen_controller_tests selection_ui_tests edit_screen_ui_tests -j 2
ctest --test-dir BUILD -R '^(command_tests|edit_geometry_tests|command_editor_tests|selection_editor_tests|timeline_editor_persistence_tests|selection_state_tests|timeline_records_tests|timeline_storage_tests|job_tests|territorial_geometry_tests|edit_screen_controller_tests)$' --output-on-failure
$env:QT_QPA_PLATFORM='windows'
$env:QT_QUICK_BACKEND='software'
& BUILD/selection_ui_tests.exe searchAndModifiers keyboardSelectionAndHiddenSearch draftsSurviveRealNavigation chooserCancelAndStaleResults -o 'selection-input.txt,txt'
& BUILD/edit_screen_ui_tests.exe vertexDragUsesEventTimeCamera objectDragUsesCurrentScaleAndOriginDraft noMotionAndActiveToolReplacement -o 'edit-input.txt,txt'
node tools/verify-platform-portability.mjs WEB_ROOT BUILD/selection_probe.exe
node tools/timeline-records-parity.mjs BUILD/core/timeline_records_tests.exe WEB_ROOT/assets/js/modules/timeline-records.js
node tools/timeline-storage-parity.mjs BUILD/core/timeline_storage_tests.exe WEB_ROOT/assets/js/modules/timeline-storage.js
```

Replace BUILD/WEB_ROOT with absolute paths. Put Qt/MinGW DLL directories on PATH.
This run used `D:/build/Pandoeditor-portability` and the two `D:/dev` repositories.
Record any nonzero exit or skipped scenario separately; never change expected
fixtures or production behavior merely to obtain a matching report.
