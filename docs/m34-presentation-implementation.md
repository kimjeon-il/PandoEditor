# M3.4 presentation implementation work log

Base: M3.3 `aefa3e0`; presentation Oracle source: `58e4087`.
The current local web `main` is `17c3dbe`.  Its pinned presentation modules
(`layer-presentation`, `layer-list-model`, and `territorial-fill-style`) are
unchanged from `58e4087`; later changes are selection-rendering work outside
M3.4's presentation contract.

This document records implementation evidence, not a release declaration.
M3.4 remains incomplete until all V01–V40 contracts and rendered output have
been verified. No release, push or portable distribution update is part of this work.

## Contracts implemented in this working tree

- `WebPresentation` stores group/symbol visibility, sparse hidden IDs, optional
  styles, and subunit/region render order independently from Qt user layers.
- `PresentationCommandProcessor` has atomic candidate replacement and separate
  revision. Scoped visibility lifts an off master on both show and hide;
  batch visibility uses item state and never lifts the master.
- Content confirmation and history restoration preserve later presentation
  changes. Object lifetime changes are rebased separately. Ordinary history
  restoration retains the existing allocation-free fast path.
- Native v5 stores `presentation.webPresentation`; v1–v4 are still readable.
  Missing object opacity remains different from explicit opacity 1.
- New web imports have no fabricated native layer membership. Legacy adapter
  removal requires matching web provenance, group IDs, names, visibility,
  opacity and memberships. Modified native layers are retained.
- Supported retained presentation leaves are promoted; remaining extension
  payloads use the lossless JSON writer. Migration archives are read-only.
- Controller display commands preserve property drafts and selection. A common
  QML display popup exposes the same commands at desktop/mobile widths.
- Debounced recovery snapshots use a separate private recovery file; they do
  not mark the document saved or overwrite the original project.

## Evidence currently available

- `presentation_tests`: scoped show/hide, batch/master separation, neutral
  opacity inheritance, subunit propagation versus region overrides, content
  Redo retention, prepared command rebasing, saved-content dirty tracking.
- `presentation_parity_probe` and `m34-presentation-oracle.mjs`: 216 actual
  pinned-web/native scoped visibility and fill inheritance comparisons.
- `MigrationTests::v5PresentationPresenceRoundTrip`: v5 version and optional
  opacity round trip.
- `MigrationTests::v5ForeignObjectOrderIsRetainedOutsideCanonicalOrder`:
  non-territorial ordering data remains an extension rather than being dropped.
- `PresentationEditorTests`: selection/draft/Redo retention, recovery round trip
  without draft commit, rejection of a pending write after project replacement.

Test registration or source presence alone is not execution evidence. Final
execution results must be appended after the build completes.

## Focused execution evidence

- `presentation_tests`: passed; includes scoped visibility, history separation,
  style inheritance and structure delete/Undo presentation rebasing.
- `command_allocation_tests`: passed, including presentation candidate allocation
  failure preservation.
- `m34-presentation-oracle.mjs`: passed 216 pinned-web/native comparisons.
- `migration_tests`: passed 16 cases, including v1–v5 migration and lossless
  unknown presentation/order preservation.
- `presentation_editor_tests`: passed 5 cases, including project-specific
  recovery and replacement safety.
- `selection_ui_tests presentationMenu`: passed at desktop 1100px and mobile
  360px offscreen. The Qt bundled-font warning is environmental only.

## Outstanding acceptance work

- Expand the Oracle to the full map-settings/object-command dependency fixtures,
  normalization/order/symbol groups and all V01–V40 mappings.
- Verify renderer multiply compositing and distinct outer/internal boundary
  widths/dashes using pixels, including native-layer composition.
- Complete desktop submenu/mobile accordion behavior, object action labels,
  rendered symbols and 1100px/360px UI acceptance.
- Verify modified/ambiguous legacy migrations, unknown-field preservation,
  native layer semantics, structure deletion/conversion restoration and recovery
  failure paths with dedicated fixtures.
- Run the complete fresh build, CTest, prior Oracles and actual Windows checks.
  Android input and sanitizers remain unverified until actually executed.
