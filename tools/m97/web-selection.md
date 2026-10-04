# M9.7.2 actual web selection and root-annex observations

Behavioral pin: `kimjeon-il/Pando@53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47`.
No web production file or application repository file was changed to create these observations.

## Deliverables

- `web-selection.mjs`: portable production session harness. Seven cases exercise polygon clipping and archive/review/cancel; actual two-crossing cut candidates with ties and toggles; components union/toggle/deselect; component snapshot archives/removal/Undo; method switch cancel/confirm; automatic component archive and mixed-method accumulated parts; actual river partitions, preserved provenance and sliver request; exhausted full-donor candidate regression.
- `session-observations.json`: deterministic serialized session observations, actual calculation/preparation requests and preview outputs.
- `web-selection-lifecycle.mjs`: actual root annex lifecycle linked to existing M9.7.1 lifecycle harness. Nine cases exercise partial/full roots, partial/full roots with children and an independent region, dangling donor/child distribution refs, dangling label/label-setting refs, and selected untouched remote donor.
- `root-lifecycle-observations.json` plus `root-lifecycle-observations/*.json`: a small index and nine named case files containing full store identities, geometry version refs, timeline records, features, distribution refs, labels, presentation, pending preview, session state and history at before/preview/cancel/review/confirm/Undo/Redo.
- `web-selection.test.mjs`: verifies source blob pin, exact session replay and exact root lifecycle replay; independently checks preview/cancel nonmutation, document Undo/Redo, rollback, tie/order behavior and selected-but-untouched donor.
- `selection-manifest.json`: minimum static import closure, 38 unmodified sources. All bytes verified against the Git object at the exact behavioral pin.
- `lifecycle-manifest-additions.json`: seven sources absent from the existing lifecycle-source manifest. Existing closure already contains pinned d3/clipper/cut worker modules.
- `observe-full-selection.mjs`: observational full-donor repro usable against either the original or an explicitly approved fixed source root. Records actual workflow source Git blob and never substitutes expected values. Keeps original pin fixture untouched.

## Integration

Copy the three `web-selection*.mjs` scripts into `tools/m97/`. Export existing harness helpers from `tools/m97/web-lifecycle.mjs` by appending:

`export { productionModules, createRuntime, observe, workerFactory, referenceEffects };`

Copy the observation JSON fixtures, the `root-lifecycle-observations/` case directory and `selection-manifest.json` into `tests/fixtures/web-m97/`. Add the seven exact source files in `lifecycle-manifest-additions.json` to existing `tests/fixtures/web-m97/lifecycle-source/` and its manifest. Keep the existing M9.7.1 sibling corpus untouched.

Default source root after integration is the existing verified `lifecycle-source`. The replay test independently verifies every selection manifest source blob before executing. The source loader deliberately accepts an explicit root so the separate full-selection observer can run an approved new pin; do not present unverified source roots as the original pin.

Temporary `lifecycle-support.mjs` is only a development test adapter: a copy of the existing lifecycle harness with helper exports and absolute paths to its existing contract/worker-host/fixtures. Do not add that copy to the repository; integrate the one-line export instead.

Local verification command:

`WEB_SELECTION_ROOT=/workspace/shared/m971-web-oracle WEB_SELECTION_FIXTURES=/tmp/m972-web-session WEB_LIFECYCLE_MODULE=file:///tmp/m972-web-session/lifecycle-support.mjs node --test /tmp/m972-web-session/web-selection.test.mjs`

The actual worker is the existing production worker through the existing real worker client and host. Selection-only `territory-components`, `territory-selection` and `territory-slivers` transport directly dispatches to the real `createTerritoryComponentPlan` without duplicating algorithms. Root preview/commit is not replaced: `createGeometryPreview`, country commits, `applyWorkerCountryPatches`, `transferLandDependents`, entity store transaction, reference assertion, project snapshots and history all run.

In the read-only session corpus only, preview transport runs the real country calculator and `calculateCountryPreview`, storing its output without mutating documents. That corpus makes no lifecycle application claim. The separate root lifecycle corpus applies via the real production route.

## Observed behavior

- Equal-area line candidates select the first production candidate. Workflow candidate IDs replace cut IDs; both seams are captured.
- Selection IDs preserve click insertion order. Union and component part archival follow source/index order.
- Component selection keeps remaining source equal to working source until archive; drawn/candidate selection subtracts live geometry.
- The original full-donor polygon candidate cannot be archived or advanced. The approved fix is separate and must not rewrite this original-pin fixture.
- Root full absorption deletes the donor and its dependent nested general objects. Independent regional geometry survives. Partial annex clips dependent geometry without reparenting.
- Dangling distribution, label-country and territorial label-setting refs cause actual apply rollback; no history is committed. These are not the sibling-annex cleanup semantics.
- A selected remote donor remains in session/preview donor ordering but is absent from affected IDs and unchanged canonically.

Limitations: no browser DOM, pixels, pointer gesture acquisition, persisted autosave I/O, network hydro ingestion, browser worker transport, or broad race/failure stress claim. The headless draft coordinates, deterministic UID/time-independent fixtures, rendering no-ops and injected hydro source are explicit test inputs/platform boundaries.

## Approved corrected full-donor path

The original pin and blocked fixture remain unchanged. `selection-correction-manifest.json` identifies the published correction `12cd8c8ec47c83cfb8c650e8f44c81cdfac10043`, tree `2ec7e0badab8804de7a5fc457a4e5e6f8ba3732d`, and the only changed production blob: `app-territory-selection-workflow.js` becomes `8f04dc31c77de0e7168074d92195040fee3d0f7a`.

`web-selection-correction.mjs` validates the original 91-source manifest, checks this exact one-file delta, then creates a temporary exact-byte overlay. No production imports or algorithms are rewritten. `selection-correction-observations.json` separately captures full polygon and full line-multi-selection archive/review and both actual root preview/cancel/confirm/Undo/Redo lifecycles. The approved change allows annex candidates to archive when the source is exhausted; original geometry, source provenance and component/new-entity contracts retain their original bytes.

Each root lifecycle case now includes explicit executed input metadata: original production features, target ID, selected source order, method, draft coordinates and actual selected component keys. This is intended for the native differential driver; snapshots remain the authority for output and references.

## Transport-safe fixture storage

The original root corpus is stored as one index plus nine compact, named per-case JSON files. `readRootSelectionLifecycleObservations()` strictly assembles the same in-memory corpus and rejects missing, duplicate, extra, reordered or mismatched case identities, unexpected files and noncanonical paths. `writeRootSelectionLifecycleObservations()` preserves this layout during regeneration and asserts exact assembled equality. Session and correction fixtures are compact JSON; no parsed observation value was changed. The largest root case is 65,322 bytes, session corpus 169,721 bytes and correction corpus 94,841 bytes, each below the 190 KB per-publication-batch ceiling when sent individually.
