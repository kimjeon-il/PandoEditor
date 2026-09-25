# M5 Display Closure Audit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Record a complete M5 display function/UI matrix and executable evidence for domain hover, repaint invalidation, and label safe-area placement.

**Architecture:** Preserve the existing presentation and picking model. Test controller-visible label bounds and domain hover, then fix only demonstrated gaps. Compare the fixed web label layout and safe insets with Qt decisions; distinguish source parity, Qt offscreen pixels, and live browser observations.

**Tech Stack:** C++20, Qt 6.8 QML/QtTest, Node.js Oracle, GitHub Actions.

**Spec:** `docs/m5-closure-audit.md`, `docs/m5-web-oracle.md`, and the user's display closure request.

## Global Constraints

- Web baseline: `kimjeon-il/world-map` `c0bd31d13dc8495593d78cf51f7cc195de7c9469`.
- Preserve M5 content semantics; no device-validation claim from Linux offscreen tests.
- Do not commit generated screenshots.

## Review Focus

- Ordinary labels at the bottom edge must respect desktop and mobile web safe insets; selected and pinned labels bypass that filter.
- Changes to viewport origin/scale must repaint every domain in the shared map renderer.
- Hidden, stale, or foreign-source hover must not survive invalidation or incorrectly clear another surface's hover.
- Distribution intensity needs a selected visible layer; generic geometry and label refs retain their typed identity.
- Visual comparison of the live deployed web page must be distinguished from the pinned web source and Qt offscreen evidence.

---

### Task 1: Label safe-area parity

**Files:** `tests/presentation_editor_tests.cpp`, `app/editorpresentation.cpp`, `tests/fixtures/web-hydro/source/{label-layout.js,map-layout-metrics.js}`, `tools/m5-label-safe-area-oracle.mjs`, `docs/m5-display-closure.md`.

**Interfaces:** `EditorController::labelLayout(scale,originX,originY,zoom,width,height)` returns placed refs and coordinates. Fixed web `layoutLabels` receives bounds derived from `createMapLayoutMetricsSnapshot`.

- [ ] Add controller test with one country label at viewport bottom: desktop 26 px and mobile 96 px exclusion, plus selected/pinned bypass.
- [ ] Run against old controller; observe ordinary mobile/desktop unsafe labels included.
- [ ] Apply the web safe inset to controller bounds, retaining selected/pinned behavior.
- [ ] Run Qt test and pinned JS comparison for desktop/mobile bounds and overlap.
- [ ] Commit the independently verified change.

### Task 2: Domain hover and render invalidation

**Files:** `tests/presentation_editor_tests.cpp`, `tests/map_render_tests.cpp`, `renderer/maprenderitem.cpp`, `docs/m5-display-closure.md`.

**Interfaces:** `EditorController::pickObject` and `setHoverObject` accept typed refs; `MapRenderItem::setOriginX/Y/MapScale` updates the texture on viewport movement.

- [ ] Add tests that exercise visible label/distribution/generic hover and hidden/stale-source cases, plus a Qt scene capture around a viewport setter.
- [ ] Run failing tests and identify whether the missing behavior is stale renderer content or hover state.
- [ ] Fix only the observed difference and run the focused tests.
- [ ] Run the full Linux Qt suite and commit.

### Task 3: Closure evidence and visual cross-check

**Files:** `docs/m5-display-closure.md`, `docs/m5-closure-audit.md`, `docs/m5-web-oracle.md`, PR descriptions.

**Interfaces:** Table rows name code, test, source pin, CI run, and remaining visual/platform gate.

- [ ] Complete all four domain function/UI rows and hover/cache state changes with evidence level.
- [ ] Compare browser label positions and safe bounds at desktop/compact widths where the deployed page allows it, and mark pinned/Qt visual limits accurately.
- [ ] Confirm final M5/M6 CI and synchronize stacked M6 branch, then update draft PRs.
