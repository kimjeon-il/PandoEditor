# Qt v6 migration and validation status

Status: M5 connected-path WIP. No final M5 integration or real Windows UI validation
has been claimed.

The codec reads versions 1–6 and writes v6. Opening does not overwrite a source
file. The existing document fields remain; the new required `content` object has
labels, hydro, distributionLayers, distributionEntries, genericFeatures,
countryDetails, symbols and physicalData. Existing v1–v5 inputs acquire empty
content collections. Shared promotion then converts validated retained content;
failed fragments remain retained without partial candidate changes.

Source `details` and distribution metadata are lossless JSON objects. Numeric
tokens and array order are preserved; object key order is canonicalized by the
existing lossless parser. Unknown content fields are retained as extensions and
are not overlaid onto current canonical values during saving.

Focused cases added to migration_tests:

- `v6ContentRoundTripAndReferenceValidation`: cross-domain IDs, shares 60+70,
  geometry kinds, unknown numeric tokens, content survival across territorial
  Undo/Redo, dangling refs, cycles and invalid domain geometry.
- `contentCommandsPreviewUndoCancelAndStale`: candidate-only create, single
  Undo/Redo, cancel, stale rejection, delete restoration and v6 reopening.

Validation executed on Windows with Qt 6.8.3 / MinGW 13.1, Debug, using the ASCII
source junction and `m5-build` under the local Pandoeditor app-data directory:

- Focused build: migration_tests, model_tests, command_tests and content_probe
  succeeded.
- CTest: command_tests, model_tests and migration_tests passed, 3/3, 1.69 seconds.
- Distribution Oracle: 3/3 cases passed against the actual pinned web export.
- Source fixture Git blob hashes match the two recorded original blobs.

Tests additionally cover duplicate creation rejection, locked-object unlock
without bundled property changes, and layer deletion/child detachment Undo.
The v5 presentation regression explicitly decodes a v5 input without content;
v6 reopens and unknown content field preservation are tested separately.

No full CTest, visible Windows UI flow, Android input or sanitizer run was made.
Remaining implementation is listed in m5-implementation.md. These focused passes
do not establish M5 completion.

## Remaining-work validation cases

- `contentPromotionAtomicRoundTrip`: successful/failed label fragment promotion,
  canonical precedence, unknown numeric spelling and array preservation.
- `contentMetadataDependencyAndPresentationUndo`: geometry-only dependency does
  not block renaming; hidden state survives v6 reopen and deletion Undo.
- `contentPointLineSession` (desktop/mobile Controller): point creation, vertex
  movement/cancel, line insertion/deletion, preview non-mutation, single Undo and
  save/reopen. This is not a physical Android touch test.
- `contentPanelSharedCommands` (1100/360 offscreen): common panel entry,
  distribution-layer preview and confirmation via QML buttons.
- `pointAndOpenLineDoNotBecomePolygonFills`: actual point/line pixels and absence
  of an accidental triangular fill from an open line.

The point/line test exposed a dangling temporary bounds geometry in projection
rebuild. The bounds geometry now outlives the read-only CountryView DTOs.
Final outcomes of this focused pass:

- Shared-panel offscreen tests: desktop 1100 and mobile 360 passed (2 cases).
- MapRenderItem pixel tests: point/open-line and existing multiply/selection
  boundary cases passed (2 cases).
- The full territorial-geometry suite's existing 14 cases passed. The two new
  content cases initially reached save but used a path where the API requires
  `QUrl::fromLocalFile`; that fixture was corrected for the focused rerun.
- The metadata test confirms that an opaque geometry dependency permits rename,
  but deletion must still reject a dangling extension reference. Deletion Undo
  is tested on an independent document without that dependency.
- Final focused reruns passed: `contentPointLineSession` desktop/mobile (2 cases,
  153 ms) and `contentMetadataDependencyAndPresentationUndo` (1 case, 35 ms).
  Together with the initial suite passes, migration's 18 substantive cases and
  territorial-geometry's 16 substantive cases now have passing results. This is
  a full focused suite plus affected-case reruns, not a fresh full CTest run.
- Final build succeeded for pandoeditor, migration_tests,
  territorial_geometry_tests, property_ui_tests and map_render_tests.
- `git diff --check` passed. Deployment folders were not refreshed.
- Offscreen Qt reports a missing bundled font directory; this is not evidence
  of Korean font appearance on the actual desktop. The pinned clipping bundle
  also emits its existing used-before-declared warnings.

No commits, remote writes, deployment updates, full CTest, new Oracle completion
claim, physical Windows input checks or Android device checks were performed.

## Label/distribution/default-data focused validation

The v6 codec now round-trips optional label settings and distribution display
settings. A missing field in an older v6 document still produces the documented
defaults. Promotion tests cover known-row removal, unknown-row retention,
canonical precedence, and preservation of unknown numeric spelling and arrays.

Focused Windows Debug checks after the change:

- `migration_tests`: passed.
- `presentation_editor_tests`: passed, including automatic label policy,
  selected/pinned collision behavior, dominant/intensity selection, alpha, and
  local hydro manifest configuration plus Undo.
- `ui_tests` focused cases `attributesAndLayers`, `compositing`, and
  `webFieldsAndAsyncApplyCancelAcrossPcAnd360px`: passed. The suite timeout was
  raised from 60 to 120 seconds because the existing combined offscreen cases
  exceed 60 seconds; the focused cases did not hang.
- The main `pandoeditor` target built with 235 fixed-version default SVG flag
  assets. With the Qt runtime on PATH, an offscreen software-rendered startup
  stayed alive for the eight-second smoke window.

The startup check is not a visible Windows interaction test. Packed hydro
feature decoding, full CTest, physical Android input and sanitizer checks were
not run and are not counted as passes.
