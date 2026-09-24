# M5 implementation contract and progress

Baseline: Pandoeditor `00ac06a`; web `bc46720`. This document describes work in
progress, not an M5 completion report.

## Canonical data

`ProjectDocument` owns labels, editable hydro, distribution layers and entries,
generic features, country details, territorial symbols and physical dataset
identity. Geometry is referenced through the existing immutable GeometryStore.
`CountryDetails` is the capital data type; `CountryProperties` remains the legacy
country command DTO to avoid changing that API's meaning.

The index distinguishes identical IDs in different ObjectRef domains. Content
validation covers geometry kinds, dangling references, distribution parent type
and cycles, independent shares in 0–100, and validity intervals. Territorial
relations explicitly reject non-territorial refs now that the object index has
multiple domains.

`content.edit` uses the existing CommandProcessor candidate/preview/confirm and
document Undo/Redo. ContentEdit contains its target, a typed replacement or
deletion, and an optional new geometry version. Generic creation stays disabled.
Deleting a distribution layer removes its entries and detaches child layers in
the same candidate. Locks and unknown extension dependencies are conservative.

## Connected in the remaining-work pass

- Shared candidate-only `promoteContent()` is called from Qt decode and web
  import. Labels, hydro, generic objects and a distribution dependency bundle
  promote transactionally. Failed fragments remain retained, canonical IDs win,
  and migration archives are not promoted again. Capital/flag leaf fragments
  are supported; embedded flags are decoded before promotion/upload.
- `ContentEditSession` now provides property drafts, preview, confirm/cancel and
  the common geometry session for Point/MultiPoint/Line/MultiLine/Polygon data.
  Open paths do not receive polygon closure/minimum-three rules. Cancelling a
  geometry preview does not leak that candidate into the property draft.
- Domain-aware rows, search, focus, chooser identity and picking are connected.
  Content point/line/area primitives render through MapRenderItem; content
  selection no longer indexes the territorial vector with a foreign index.
- The shared content panel is reachable through the desktop/mobile workspace,
  with create/edit/delete previews, distribution relation selectors and flag
  upload. Existing map gestures are reused rather than duplicated.
- Content dependency checks inspect changed fields. Territorial removal remaps
  or clears typed label/distribution references atomically. Ambiguous capital
  and symbol conflicts are rejected instead of discarded.
- Loaded builtin hydro records use copy-on-edit and source hiding in one
  command. This does not bundle or download an external hydro dataset.
- Content visibility is preserved during normalization, reopen and deletion
  Undo. Its group controls use the existing presentation processor.

## Label, distribution and default-data pass

- `LabelSettings` now stores pinned state, manual geographic position, optional
  priority and zoom limits, and collision group. The shared layout orders
  selected, pinned, priority and stable key, uses 3 px desktop / 5 px compact
  padding, and shares its accepted rows with label picking. Pinned and selected
  labels remain visible through collisions.
- The map display controller exposes pin, manual-position and automatic-position
  reset actions. Dragging a placed label converts the screen position back to
  geographic coordinates; these presentation actions do not enter content Undo.
- Distribution presentation stores dominant/intensity mode, optional selected
  layer and boundary visibility. Dominant winners are selected per distribution
  type with input-order tie handling. Intensity requires a visible selected
  layer. Fill alpha follows `(0.12 + share / 100 * 0.58) * opacity`; rendering
  and picking consume the same visible entry list.
- v6 serializes these settings as optional presentation fields. Retained web
  label/distribution settings promote only for known objects and keep unknown
  rows and fields in their lossless source fragment. Existing canonical data
  wins over a retained or migration-archive copy.
- The fixed web flag assets and ID resolver are bundled with their notices.
  `Default`, `None` and validated `Embedded` remain distinct, legacy and
  political aliases follow the pinned web mapping, and missing defaults report
  `unavailable` instead of inventing an image.
- A local hydro provider validates an explicitly selected `0.13.1`
  `pandolab-water-shards-v5` manifest and index. Configuration is an undoable
  content command. It does not download, substitute or yet decode the packed
  worldwide feature data; existing loaded builtin records retain copy-on-edit.

## Required remaining implementation

- Decode the selected packed hydro index/shards into read-only map features;
  only provider identity and availability are connected in this pass.
- Complete whole-object translation and exact web content primitive ordering,
  plus broader all-domain hover/render-cache evidence.
- Add the remaining allocation-failure and typed-reference regression cases,
  then run the final M5-wide CTest/Oracle integration pass.
- Perform visible Windows interaction and physical Android touch/SAF/IME checks.
  Offscreen QML coverage must not be reported as either of those checks.

M5 remains open until those items and the previously scoped domain flows are
verified end to end.

Windows frame, deployment assets and the web working tree are outside these edits.
No commit, push or deployment is part of this work.
