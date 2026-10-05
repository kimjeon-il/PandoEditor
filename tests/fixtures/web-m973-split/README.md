# M9.7.3 pinned production split observations

`lifecycle-observations.json.gz` stores the immutable first 25-case Node discovery capture at web
`53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47`, with only the approved selection-workflow
changes through `6c3f930b8573fa09991885b661879ea36725472e`. It must remain unchanged
when a later approved dateline correction is added. Record corrected results and
source provenance separately.

The capture executes the real `enterTerritorialUnitSplitMode`, shared entity
selection, actual cut/selection/calculation worker with production client/RPC,
root or child preview, store mutation, strict reference assertion and history.
`lifecycle-observations-v2.json.gz` stores the authoritative 27-case capture at the same
production source pin. Capture v2 awaits the async child draft entrypoint instead
of serializing its Promise as an empty object, includes generic-feature snapshots,
and adds the requested removed-child presentation and legacy metadata cases.
This is a host observation fix, not a web behavior change. The original capture
remains byte-for-byte intact (SHA-256
`b29f65be6069b98925d4c9a32c11794c723517ad52bd3dc842f6dff1fa2b7f5d`).
`presentation-observations.json.gz` isolates those two additional cases.

Platform UI surfaces and selection rendering are headless recording ports. These
are Node discovery observations, not Chromium or rendered pixel evidence.

The original `app-object-picking.js` factory is an additional exact-byte pinned
source, identified by both Git blob and SHA-256 in `source-manifest.json`.
All other originals remain in `web-m97/lifecycle-source`; approved overlays remain
in their existing separate folders. Original source files are not rewritten.

## Reproduction

- `node --test tools/m97/web-split.test.mjs`
- `node tools/m97/web-split.mjs --cases=root-date-line --summary`
- `node tools/m97/web-split.mjs --summary`

Run the file as ESM or with `node --test`. A CommonJS `node -e` host exposes global
`exports`, which diverts the original UMD vendor's export path; the harness
rejects that invalid environment.

## Browser execution

`tools/m97/web-split-browser-bundle.mjs` exports:

- `await splitBrowserSourceBundle({caseIds})`, defaults to all 27 exact cases
- `createSplitBrowserPage(bundle, {assetPrefix:'/split-oracle/'})`

An authorized browser-test HTTP server must serve every
`bundle.sources[path].source` at `/split-oracle/<path>` with JavaScript MIME and
serve the returned page on the same origin. The page verifies every fetched asset
hash, imports exact original relative module URLs, and launches the unchanged
classic production Worker at `/split-oracle/assets/js/workers/map-edit-worker.js`.
No worker source or import URL rewriting is necessary. Inspect `__splitSummary`
for readiness and retrieve `__splitReport` after state `complete`.

Browser launching is intentionally outside these files and belongs to the
approved Chromium CI route. The browser runtime bundle is syntax/dependency
validated by executing an actual lifecycle case under Node, but that check does
not replace browser execution.

## Observed distinctions

- Cutter order is original component/face order. It may yield 3+ pieces and omits
  uncut islands. Strict minimum planar area chooses the default; exact ties keep
  first, and tiny real floating differences are preserved.
- Arbitrary selected pieces are unioned into one created entity. The original
  entity retains whole-source minus that union, including uncut islands.
- Selecting every candidate is allowed when an untouched island remains. Empty
  selection and exhausted single-source entity creation cannot archive.
- Root creation clips/removes existing children; child creation reparents whole
  selected descendants and clips partial descendants. Root dangling distribution
  or territorial label-setting refs reject commit atomically.
- Fresh created identities have factory defaults, not cloned source identity or
  style. Selection-after points to the new entity.
- Original dateline cut succeeds, but root entity-create preview rejects planar
  containment against the original seam-crossing source. No state/history applies.
  This baseline is preserved pending a separately approved web correction.

## Explicit candidate-only testing

`web-split-candidate.mjs` exposes
`loadSplitCandidateModules({candidateRoot,candidateChanges:[{path,sha256,blob?}]})`.
It copies the verified bounded original tree, applies only explicitly named and
hash-checked candidate files, and identifies the result as test-only. Both
controller and actual worker use that copy. It never changes pinned originals or
observations. Pass its loaded result and a case definition to
`runSplitLifecycleCase`, and call `loaded.cleanup()` afterward.

## Presentation is not a document history invariant

The actual root removed-child presentation case commits while keeping stale
`itemVisibility.subunits` and `layerPresentation.objectStyles` entries. Undo
restores the child and its settings. Redo prunes the hidden subunit entry, but the
object style remains. Therefore confirmed and redone presentation are not equal;
compare each captured checkpoint, and do not normalize away this distinction.
The actual legacy generic `ownerId` survives as descriptive source metadata, not
an active territorial relation, after its named territory is deleted.

## Lossless storage

The three observation payloads use deterministic gzip (level 9, no timestamp or
filename). `observation-manifest.json` lists compressed and decompressed byte
lengths, SHA-256 hashes, source provenance, capture versions and readable case
inventories. The original JSON byte streams have not changed. Their compressed
sizes are 155,614 bytes (v1), 183,734 bytes (v2), and 28,561 bytes (presentation).

Use `readSplitObservation(name)` or `readSplitObservationBytes(name)` from
`tools/m97/web-split-fixtures.mjs`; these verify both stored and decompressed
hashes. `node tools/m97/web-split-fixtures.mjs --check` checks exact deterministic
recompression and lossless roundtripping. This is fixture storage only and does
not change any project format or comparison value.

Custom candidate definitions may supply `parentGeometry` and `dependentFeatures`
(full Features or `{id,geometry,parentId}` entries). Browser candidate bundles use
`splitBrowserSourceBundle({candidateRoot,candidateChanges,caseDefinitions})` and
are visibly marked `testOnlyCandidate`; default bundles remain pinned.

## Separate empty-selection reactivation scenario

`splitReactivationCaseDefinition()` returns the custom
`root-empty-selection-reactivated` case without adding it to the immutable
27-case inventory. Its actual workflow stages are `before`, `candidates`,
`emptied`, `reactivated`, `selected`, `archive`, `review`, `cancel`, `confirm`,
`undo`, and `redo`. The emptied checkpoint attempts the public `advance` action
and records false; the reactivated checkpoint records the actual successful
`selectCandidate` toggle of the originally default-selected candidate index.
The confirmation restart repeats those public actions before consumption.

The returned `reactivation.inactiveCache` contains the actual cached remainder,
working source, empty selected IDs, null combined geometry, false preview and
archive readiness, and the false advance outcome. It is deliberately not changed
to the source geometry or marked unavailable. `rebuiltBeforeConsumption` records
the actual recomputed current and remaining geometries and true preview/archive
readiness. Cross-runtime reports must retain both raw inactive-cache values and
may only distinguish that inactive diagnostic from behavioral parity after
verifying the reactivated state matches before archive and commit.

## Deterministic production startup and async settling

Pinned production `app-progressive-startup.js:450` invokes the public
`mapEditClient.rebase()` after initial map setup. The harness reproduces that
startup boundary and waits for the real worker's `stats().ready` before entering
a split workflow. It does not run a synthetic calculation or consume a request
ID to warm the worker.

This matters for child entry: production `enterTerritorialUnitSplitMode` calls
setup once through `enterTerritorialCreateWorkflow`, then clears its source
cache and requests setup again. Both real `territorial-source` requests remain;
latest-wins cancellation and the resulting raw error are not filtered. Without
the startup boundary, independent cold-worker readiness timers could separate
those requests enough for the first to complete, making the incidental error
observation vary with host timing.

Settling also waits for all actual wrapped worker request promises and the active
setup-source cache, including after cancel. Interrupted-session callbacks follow
the unchanged production suppression rules; repeated entry retains its own real
cancellation diagnostics. A regression deliberately defers the second cold
readiness poll until a genuine worker job completes: it reproduced the original
failure before the startup fix and passes afterward without golden changes.
