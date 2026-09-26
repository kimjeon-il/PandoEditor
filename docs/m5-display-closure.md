# M5 display closure audit: labels, flags, distributions and generic objects

Baseline: `world-map` `c0bd31d13dc8495593d78cf51f7cc195de7c9469`.
This ledger separates model/Qt offscreen evidence from an actual browser pixel
comparison. The executable evidence names a regression target, not proof that
every UI path is closed. The recorded fresh run and remaining visual gates are
given below. No device evidence is inferred from offscreen rendering.

## Function and UI regression matrix

| Domain and behavior | Code and executable evidence | Result / open gate |
| --- | --- | --- |
| Label create, edit, delete, typed geometry and Undo/Redo | `app/editorcontent.cpp`; `migration_tests::contentCommandsPreviewUndoCancelAndStale`, `contentPromotionAtomicRoundTrip`, `v6ContentRoundTripAndReferenceValidation` | Existing core regression; no complete 1100/360px create→drag→save→reopen visual journey. |
| Label priority, zoom, collision, selection and pin/manual reset | `core/src/presentation.cpp`, `app/editorpresentation.cpp`; `presentation_editor_tests::labelLayoutAndDistributionMatchWebRules`, `migration_tests::labelAndDistributionSettingsPromoteAndRoundTrip` | Rules tested. Drag gesture and actual glyph overlap at both widths remain visual gates. |
| Label safe area (ordinary versus selected/pinned) | Pinned `label-layout.js` + `map-layout-metrics.js`; `m5_label_safe_area_web`, `presentation_editor_tests::labelLayoutRespectsWebBottomSafeArea`, `ui_tests::labelSafeAreaVisuallyExcludesBottomControls` | Source decisions and Qt offscreen delegate/captures tested; deployed browser is a different build. Native 96px mobile inset does not include Android system safe-area pixels. |
| Label visibility, map pick, hover, search | `MapView.qml`, `app/editorpicking.cpp`, `app/editorselection.cpp`; `presentation_editor_tests::visibleContentDomainsHoverAndHiddenHoverExpires`, `selection_ui_tests::focusAndHoverDoNotEdit` | Map hit and hidden-hover invalidation tested; full browser hover pixel parity open. |
| Flag Default / None / Embedded and missing asset | `app/defaultflagresolver.cpp`; `web_import_tests::flagsAndRootClassification`, `migration_tests::v6ContentRoundTripAndReferenceValidation`, `ui_tests::flagOnlyIsIndependentFromNameChannel` | Import policy and codec covered. Default DEU SVG decodes and appears at both widths; None/Embedded pixels and native invalid-image rejection still need focused review. |
| Independent name and flag switches for country/subunit/region | `core/src/presentation.cpp`, `app/editorpresentation.cpp`, `MapDisplayControls.qml`; `presentation_tests`, `presentation_editor_tests::countryFlagRemainsWhenNameChannelIsHidden`, `ui_tests::flagOnlyIsIndependentFromNameChannel` | Country flag-only and name-only delegates checked at 1100/360px. Subunit/region combinations still lack full offscreen UI captures. |
| Flag/name stacking over selected geometry | `ui/common/MapView.qml`; `ui_tests::mapLabelsAndFlagsRemainAboveSelectionEmphasis`, `flagOnlyIsIndependentFromNameChannel` 1100/360px | Structural Qt layer check and selected DEU flag-only pixels; pinned browser selected state remains open. |
| Distribution layer/entry edit, references, validation, Undo and v6 | `app/editorcontent.cpp`; `migration_tests::contentCommandsPreviewUndoCancelAndStale`, `v6ContentRoundTripAndReferenceValidation`; `property_ui_tests::contentPanelSharedCommands` 1100/360px | Layer form path tested; complete entry-form click and geometry journey remains open. |
| Dominant ties, intensity selected layer, share alpha, boundary and visibility | `core/src/presentation.cpp`, `app/editorcontroller.cpp`; `m5_content_web_parity`, `presentation_editor_tests::distributionModeUsesSelectedVisibleLayer`, `map_render_tests` overlays | Source-driven winner and native display rules; all type/style UI combinations not visually captured. |
| Distribution map hover and pick remap to layer | `app/editorpicking.cpp`, `app/editorselection.cpp`; `presentation_editor_tests::visibleContentDomainsHoverAndHiddenHoverExpires` | Typed hit, hidden group and hover expiry tested; actual hover stroke/pixel parity open. |
| Generic object read/edit, geometry variants and typed v6 refs | `app/editorcontent.cpp`, `core/src/document.cpp`; `migration_tests::v6ContentRoundTripAndReferenceValidation`, `map_render_tests::pointAndOpenLineDoNotBecomePolygonFills` | Existing object edit works. M5 ContentPanel has no generic create action and `beginContentEdit("generic",…,true)` rejects creation; pinned web also sets generic `directCreation:false`. The direct-create journey is outside both UIs. |
| Generic visibility, pick, hover and painter style | `app/editorcontroller.cpp`, `app/editorpicking.cpp`, `renderer/maprenderitem.cpp`; `presentation_editor_tests::visibleContentDomainsHoverAndHiddenHoverExpires`, `map_render_tests` generic/point and viewport capture | Typed map hit, group hide and shared canvas repaint tested; complete edit/opacity/line/point UI journey open. |
| Desktop 1100px and compact 360px display menu | `ui/common/MapDisplayControls.qml`; `selection_ui_tests::presentationMenu`, `property_ui_tests::contentPanelSharedCommands`, `ui_tests` label layout and layering | Menu entry and common command paths tested; each domain's every button is not yet end-to-end asserted. |

## Per-domain hover and render-cache route

The editor does **not** maintain four separate domain image caches. Qt Quick
retains the `QQuickPaintedItem` texture for fills/lines/points; the label/flag
repeater is a separate QML overlay. `MapRenderItem::setPaths` and `setVisuals`
call `update()` on changes. Origin and scale setters emit `viewportChanged`,
which the constructor connects to `update()`. A frame change also calls
`update()`. `MapView.qml` binds geometry, visuals, selection and viewport to
these setters.

| Domain | Hover input / expiry | Repaint dependency / evidence |
| --- | --- | --- |
| Label | Placed typed label only; `pickObject` → `setHoverObject`; group hide expires old hover | Presentation/selection/viewport change recomputes `placedLabels`; 1100/360px delegate safe-area test. |
| Flag | Decorates the parent territorial label and has no separate object hover | `countryVisuals` `flagVisible`/`flagSource` and QML Image binding; flag-only and name/flag stacking tests. |
| Distribution | Visible dominant/intensity entry hit maps to `distributionLayer`; group hide expires hover | Mode/visibility → `visualChanged` → `setVisuals` → shared texture `update`; Qt overlay and map hit tests. |
| Generic | Typed polygon/line/point hit; group hide expires hover | Geometry `paths` and presentation `visuals` update shared texture; point/line and viewport pixel tests. |

`setHoverObject` protects another surface's current hover from an obsolete
`map` clear. A hidden group must also clear a hover already held on its object;
the focused test exercises all three independent content domains.

## Safe-area visual comparison

The pinned web `app-country-labels.js` passes the `map-layout-metrics.js`
safe bounds into `label-layout.js`. Its pinned stylesheet reserves a `2rem`
desktop status bar (32px with a 16px root), and mobile reserves
`96px + env(safe-area-inset-bottom)`; the layout module enforces at least
26px below. An ordinary label whose box crosses those bounds disappears;
selected and pinned labels remain. The native 1100/360px UI captures are
uploaded as CI artifacts, not committed into the repository.

| Capture | Ordinary label center | Selected label center | Visible outcome |
| --- | --- | --- | --- |
| Qt desktop 1100×760 | 50px above map bottom | 10px above map bottom | Ordinary visible; inside the 32px inset it disappears; selected reappears over the bottom edge. |
| Qt compact 360×640 | 115px above map bottom | 10px above map bottom | Ordinary visible; inside the 96px inset it disappears; selected reappears over the bottom edge. |
| Live browser wide 1363×936 | Different world dataset and camera | Selection route raised `PL-RUNTIME-001` | Computed CSS inset `2rem`; label glyphs remain above the bottom strip. |

The 1100/360px Qt offscreen captures show the ordinary label above the bottom
controls and the selected label bypassing the inset. The earlier capture also
showed weak text contrast over a dark fill. The final Qt captures from run
`36172912964` show a readable light outline around the native label at both
widths, following the pinned web rule (`.country-label` uses
`paint-order: stroke` and a 2.3px halo). The same artifact shows decoded
default SVG flags without name glyphs in the 1100/360px country fixture.

On the separately deployed browser page, the wide layout exposed a map
rectangle at `(0,48)` sized `1363×888` and computed bottom safe inset `2rem`.
Visible country labels stayed above that bottom strip. This page is newer
than the pinned commit and its selection route produced `PL-RUNTIME-001`, so
the observation cannot establish a pixel golden for selected labels or a
pinned desktop/mobile image comparison. The live browser screenshot shows dark
labels with a light halo on gray fills; its geometry, viewport and labels are
different from the synthetic Qt fixture, so it cannot serve as a pixel golden.
A native Android system inset and a real Windows Qt frame are likewise
unverified.

## Fresh complete regression

M5 commit `c03c6bd973a76040f306e0e00377e4d4ef0ce55d` passed all **48/48**
registered CTests in [run 36172912964](https://github.com/kimjeon-il/PandoEditor/actions/runs/36172912964),
including the newly registered `m5_content_web_parity` and
`m5_label_safe_area_web`. The six Qt images are in that run's artifact.
M6 carry-forward commit `1c1e1f6c6879485a2891abcecf99e2f36b4285bc`
passed **81/81, 0 skipped** in
[run 36172916801](https://github.com/kimjeon-il/PandoEditor/actions/runs/36172916801);
the M6 audit requires these M5 tests by name. These are Linux offscreen
results, not Windows or physical Android runs.
