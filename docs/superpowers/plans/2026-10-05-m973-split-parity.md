# M9.7.3 Split Parity Implementation Plan

**Goal:** Match pinned web complex line partitioning and its actual single-new-sibling selection/commit lifecycle.

**Architecture:** Reuse the exact pinned web cut preparation modules through a bounded, hash-verified Qt adapter. Reuse the existing `TerritorySelection` bounded-creation model for ordered multi-fragment selection; create one sibling from the selected union and retain the whole-source remainder through the existing atomic command/history infrastructure.

**Tech Stack:** C++20, Qt 6.8.3 QJSEngine/QML, pinned JavaScript, actual Chromium/V8 oracle.

**Spec:** M9.7.3 user-approved sequence and `docs/web-parity-contract.md`; stage-2 baseline c8195598815f605390f01a79c752fe4c9b9fa8d3.

## Global Constraints
- App branch: `codex/m97-web-editing-parity`; no main merge, release, installation, or deletion.
- Web behavior: Pando `53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47` plus explicitly approved overlays through `6c3f930b8573fa09991885b661879ea36725472e`.
- Preserve existing stage-1/2 fixtures and provenance. New observations must execute production web entry chains.
- No snap/shared-boundary changes, engine extraction, performance reuse, or v9 bridge expansion in this stage.
- No approximate geometry/ref/history expectations, no numeric adaptation based solely on Node output.
- Native build and tests are serialized by `/tmp/m972-build.lock`.

## Review Focus
- Multiple selected faces still create exactly one sibling, not one object per face.
- Untouched components remain in the source; crossing holes/date-line faces preserve exact geometry.
- Equal-area defaults retain first traversal-order candidate; toggle order must not change union operand order.
- Empty/all-selected cases cannot silently remove the bounded source; canceled/invalid calculations cannot commit.
- Child relationships, retained references, copied/default presentation, and selection after commit match real root and child entry chains.

### Task 1: Establish actual split oracle and RED cases
**Files:** New `tools/m97/web-split*.mjs`, `tests/fixtures/web-m973-split/**`.
**Interfaces:** Existing `runWorkerCalculation`, exported lifecycle runtime, pinned selection module wiring. Produce exact operation and lifecycle JSON plus verified source provenance.
- [ ] Capture production `enterTerritorialUnitSplitMode`, draft preparation, candidate selection/archive, preview/confirm/cancel/Undo/Redo for root and child sources.
- [ ] Extend six existing complex cut cases with boundary/tangent/invalid/no-cut, multiple selected faces, untouched islands, all/empty selection, repeated/canceled work.
- [ ] Assert current native failures before implementation; verify actual Chromium observations, retaining raw outputs.

### Task 2: Add pinned Qt cut calculation adapter
**Files:** New `app/cutgeometrycalculator.{h,cpp}`, `assets/geometry/cut/**`, qrc and `tests/cut_geometry_calculator_tests.cpp`; register in `app/CMakeLists.txt`.
**Interfaces:** `CutGeometryResult prepareCutGeometry(const QJsonObject&, const GeometryCancellation&)`; complete worker JSON and status, no mutation. Payload contains source, coordinates, production view and preview flag.
- [ ] Write/execute failing tests for actual worker result shape and ordered multi-face geometries.
- [ ] Vendor exact source modules with license/hash manifest and only necessary Qt platform/syntax adaptations.
- [ ] Compare full results against actual browser; verify cancellation/nonmutation and focused regressions.

### Task 3: Connect bounded selection and atomic split transaction
**Files:** Existing `app/editorstructure.cpp`, `app/editorterritoryselection.cpp`, `app/editorgeometry.cpp`, `app/editorcontroller.h`, `app/territorialgeometry.*`, `app/geometrycalculator.*`, core territorial intent/planning/apply files, `ui/common/MapView.qml`, affected native tests.
**Interfaces:** Existing `TerritorySelection(TerritorySelectionKind::BoundedCreation)` candidate/part model; final selected union and remainder supply one atomic creation/replacement plan. Exact transaction fields determined by Task 1 observed production results.
- [ ] Add failing native controller/core/QML cases for multi-face selection, root/child history and reference results, cancellation and invalid input.
- [ ] Replace binary cut guards and inversion-based UI with production-ordered variable candidate selection; use actual view payload for endpoint preparation.
- [ ] Preserve exactly one created sibling and source remainder, all unaffected geometry/refs, and observed presentation/selection policy.
- [ ] Run focused native tests and compare lifecycle outputs against the actual web oracle.

### Task 4: Review, aggregate validation, and publication
**Files:** Gate registration, final evidence documentation only after implementation stabilizes.
- [ ] Independent review of source adaptation, lifecycle semantics, tests and provenance; correct findings with RED/GREEN evidence.
- [ ] Run isolated full native suite (baseline 138 tests) and actual Chromium oracle/controller comparisons against final source.
- [ ] Publish only to the authorized dedicated branch with exact source/tree verification and real-parent fast-forward.
- [ ] Verify all required exact-commit CI jobs; report actual counts and unresolved limits, without claiming M9.7 overall complete.
