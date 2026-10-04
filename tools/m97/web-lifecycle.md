# Current-web lifecycle observations

This is an independent, web-only observation corpus. It is not evidence that the native editor matches the web, and it does not declare every stage observed.

## Pin and provenance

- Behavioral source: `kimjeon-il/Pando@53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47`.
- `tests/fixtures/web-m97/lifecycle-source/`: 84 original production files, copied byte-for-byte from that pinned checkout, preserving original relative paths.
- `lifecycle-manifest.json`: the original Git blob SHA-1 for every production file. Source bytes were checked against `git rev-parse <commit>:<path>` when copied.
- The only non-production file inside that source tree is `package.json`, containing only `{"type":"module"}` to permit Node ESM loading. The loader checks this separately.
- `verifyLifecycleSources()` validates every declared source hash and rejects unlisted JavaScript files before importing production modules. The worker host repeats validation before launching the unchanged worker program.
- No old `b2` behavior supplies observations or lifecycle expectations.

## Run

From the repository root, on Node 22+ (verified on Node 24.19.0):

```sh
node --test tools/m97/web-lifecycle.test.mjs
node tools/m97/web-lifecycle.mjs > /tmp/pando-web-lifecycle.json
cmp /tmp/pando-web-lifecycle.json tests/fixtures/web-m97/lifecycle-observations.json
```

To intentionally regenerate observations after reviewing an authorized source-pin or fixture change:

```sh
node tools/m97/web-lifecycle.mjs > tests/fixtures/web-m97/lifecycle-observations.json
node --test tools/m97/web-lifecycle.test.mjs
```

The JSON is observed output from execution, never manually authored expected geometry/state. Tests also execute the corpus twice and require identical observations and equality with the checked-in artifact. The input is deterministic synthetic geometry and IDs; synthetic input is not a substitute implementation of the production behavior.

## Executed owners

- Parent detach: actual `app-object-metadata.commitTerritorialRelation`, `territorial-service.changeAdministrativeParent`, kernel validation, `project-command-pipeline`, and `document-mutation-runner`.
- Sibling merge, full annex, and create: actual `app-territorial-drafts.previewTerritorialEdit`, `map-edit-worker-client`, source tracker, job scheduler, worker RPC, and the unmodified `map-edit-worker.js`, including geometry calculation and validation receipts.
- Preview, discard, and confirmation: actual `app-geometry-preview` and `geometry-preview` sessions. The harness invokes the real impact modal's supplied `onCancel` / `onConfirm` callbacks, rather than simulating the resulting mutation.
- Canonical state: actual `territorial-entity-store` / repository, timeline records, geometry-version store, and production normalizers.
- Undo/Redo: actual `app-project-snapshots` capture/restore and `history-service`. No features-only surrogate snapshots are used.

`web-lifecycle-worker-host.mjs` implements only browser-worker platform facilities through Node worker threads: `self`, `location`, `postMessage`, `importScripts`, and event transport. It copies no application algorithm, receipt logic, source tracking, or queue policy.

## Observed cases

1. `parent-detach`: synchronous child parent removal preserves all geometry values. Undo returns all document fields, identities, timeline records, geometry versions and distribution references; Redo restores detached state. Preview and cancel explicitly remain unobserved because that production command has no pending preview.
2. `sibling-merge`: preview does not mutate document/history; real preview discard is a no-op for canonical state; rejecting the impact confirmation also cancels without mutation; confirm deletes the donor, reparents its grandchild, and rewrites donor distribution references to the retained sibling. Undo/Redo restore document states.
3. `sibling-annex-full`: the donor is removed and its grandchild reparented. Unlike merge, the donor distribution entry is deleted, not rewritten. Both direct preview discard and impact-modal rejection are observed.
4. `sibling-create`: the worker creates a new child from part of the donor. Cancel leaves canonical state/history untouched; confirm creates the object and changes donor geometry; Undo removes the new object and restores the donor; Redo re-creates the observed document state.

Each observed stage carries document state, presentation-reference state, history depths, current preview, and outcome. Created/deleted/retained IDs, distribution reference rewrites, and reparented IDs are derived from the captured before/confirm documents, not from a hand-maintained expected map. `additionalStages.impactCancel` separately records the real modal-cancel branch where it exists.

### Important current-web observation

Current production history snapshots include document fields, not general presentation fields. For sibling merge and full annex, deleting the donor clears its label settings, item visibility, and object style. Undo restores donor identity, geometry and document references but these deleted presentation entries remain absent. The corpus records that actual distinction; it does not fill in an idealized full restore.

## Bounds

These are headless production lifecycle observations, not browser UI tests. Platform/UI ports stub rendering, DOM text/classes, feedback, autosave persistence, and selection presentation. Selection, tool UI effects, pixels, keyboard/pointer gestures, and actual browser worker transport are unobserved. The fixture has no hydro/generic/standalone labels and no built-in country-label references; unrelated UI dependencies are not exercised. Unexpected application errors fail the runner rather than being converted into successful output.

Country merge/annex command workflows, invalid geometry, lock policy, source races, failure rollback, concurrency, and all other operations are explicitly unobserved. Do not treat sibling editing as country editing or direct calculation as cancellation coverage. Source pin changes require a new explicit source review and newly executed observations.

The native adapter must explicitly select comparable fields and distinguish supported stages from unobserved ones. This corpus does not silently normalize away IDs, geometry-version changes, presentation loss, or history effects.

## Calculation-only worker hook

`runWorkerCalculation(operation, payload, { entities = [], root, manifest } = {})` is exported from `web-lifecycle.mjs`. Omit `root` and `manifest` for the checked-in verified source pin. It uses the actual production worker client and worker, returns only `response.result`, and stops the client in `finally`. It does not create lifecycle observations.

For a cut, use operation `territorial-cut` and payload `{ source, sourceKey, coords, buildPreview: true, view }`. Here `source` is a Polygon/MultiPolygon **geometry**, not a Feature. `view` includes `kind`, `scale`, `translate`, `rotate`, `center`, `size`, `coarsePointer`, and `snapDistance: { mouse, touch }`. The actual cut preparation conditionally loads original vendored D3 3.5.6. A focused test verifies a simple two-crossing cut returns two real candidates; complex-cut fixtures are maintained separately from this lifecycle corpus.

Vendored third-party code in this lifecycle source tree: `assets/js/vendor/polygon-clipping.min.js` (bundles polygon-clipping, splaytree, robust-predicates) and `assets/js/vendor/d3.min.js` (D3 3.5.6). Applicable notices are maintained in the sibling fixture license directory.
