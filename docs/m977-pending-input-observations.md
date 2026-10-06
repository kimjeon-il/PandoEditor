# M9.7.7 pending-input observation slice

## Status and evidence scope

Prepared against native application commit
`218b1a56cf6c1fad75779e19ef8107b9f61cfccf` on
`codex/m97-web-editing-parity`. Local actual-production-domain Node and compiled
Qt 6.8.3 observations are collected. Actual Chromium collection remains pending
separately authorized exact-commit CI. This document does not claim Chromium or
web/native parity completion.

The corpus contains seven scenarios, each at 1024×768 mouse and 360×800 touch
capability: 14 rows, 86 web stages and 80 observed native stages. Six native
cancel/re-request/confirm stages are unavailable because the preceding empty
method switch did not open a confirmation. They remain explicit coverage gaps.

## Findings from the local diagnostic

- Both implementations discard preparation-phase taps. Their ready draft is
  empty. The distinct ready taps use corpus points `[4,2]`, then `[4,4]`, proving
  that earlier attempted `[2,2]`/`[4,2]` inputs were not replayed from a queue.
- The production web editing domain considers an empty configured drawing tool
  active. Switching that ready polygon tool to line requests confirmation. Cancel
  keeps polygon active; confirming switches to line and clears draft history.
- Native switches the empty ready polygon tool directly to line. No confirmation
  appears, so its dependent Cancel and Confirm operations are not synthesized.
- With a nonempty draft, both preserve points through a canceled method change,
  clear the old draft after confirmation, and preserve the points on a same-method
  request. Draft Undo removes the last ready point; Redo restores it. Native
  exposes history availability, not numeric private history depth.
- Clear and Back prevent a completed-but-withheld result from restoring old draft
  work. Held-result supersession leaves the replacement line method active.
- Immediate pending supersession is a different boundary: it rejects the original
  activation before its old Worker request/native timer dispatch. It is not
  evidence of an old Worker-result cancellation. The held-result scenarios supply
  that separate evidence.
- Every observed stage preserves exact document bytes within its own runtime,
  project history availability and revision. Cross-format canonical equivalence
  is not claimed.

The raw selected-field comparison reports **70 field differences**, retaining:

- Web `preparing` versus native `drawing` plus `selectionPending` during setup
  work, and the raw post-Back phase (`preparing` versus `sources`)
- Empty-ready method/confirmation/pending differences
- Native immediate-return pending state versus web awaited-promise completion at
  confirmed method change; these are different scheduling observation boundaries
- Exact coordinate differences up to `1.3322676295501878e-15`, including native
  presentation projection/inverse round trips. No epsilon is used to turn those
  differences into parity

After Back, a historical web phase value of `preparing` does not itself mean an
outstanding preparation. The report separately retains `preparationActive`, raw
Worker request counts, stage and phase. Its `pending` flag requires current
preparation or a preparing *selection* stage.

## Why the previous stub missed the empty-ready behavior

The approved production `editing-domain.js` defines `draftInputActive()` from
`toolConfig()`, independent of point count. Actual `toolDraftDefinition()` permits
a drawing configuration only in selection/drawing. Production
`app-territory-selection-workflow.js::activeCurrentWork` uses that predicate when
choosing whether a method switch needs confirmation.

The older `tools/m97/web-selection.mjs` adapter instead uses `draft.length > 0`.
That adapter therefore cannot establish empty-ready confirmation behavior. This
slice replaces its editing-domain port with the actual domain and actual tool
configuration without modifying the original source closure or old gates.

Native `app/editorterritoryselection.cpp::geometrySelectTerritoryMethod` supplies
`!lineDraft.empty() || !draft.polygons.empty()` to `requestMethod`. This explains
the observed empty-ready difference. No product-policy fix is included here.

## Evidence integrity and reviewed corrections

- Exact original source closure and supplementary Git blob, byte lengths, SHA-256
  and corpus pin are verified before evaluation.
- The recovered native harness initially used ready tap indexes 0/1 while the web
  used declared indexes 1/2. A failing actual-native test reproduced the mismatch;
  the probe now reads every tap index from the immutable corpus. Earlier native
  reports are superseded for coordinate comparison.
- The actual Worker transcript is correlated by worker/request identity through
  rebase, readiness, request, original response, held/released delivery and
  termination. Dropped responses, changed payloads/identities, false preparation
  labels and fabricated decision history are rejected by mutation tests.
- Native canonical bytes/hash, public raw state, exposed paths and ordered public
  state-change events are checked; unsupported private observations stay explicit.
  Observed public affine parameters must equal the controller's public projection.
  Exact affine consistency binds intended/map/inverse coordinates and accepted
  draft vertices. This checks observation consistency, not independent projection
  correctness or private raw polygon storage. Browser input consistency uses the
  actual verified D3 bytes, with no handwritten projection substitute.
- Actual Chromium identity requires a verified capture path. Diagnostic Node rows
  cannot be passed directly to the comparator as Chromium evidence.
- Capture hashes bind exact decompressed raw bytes. Native collection is performed
  by the comparator against a stable binary, rather than attaching a binary label
  to an arbitrary existing JSON file.

## Remaining coverage limits

This is public-handler/controller evidence. Viewport and pointer capability
configuration do not establish DOM/QML hit testing or real touch dispatch. Native
inputs use map coordinates; browser inputs use D3-projected screen coordinates.
The held-result boundary means the worker has completed and owner delivery is
withheld. It does not prove cancellation during simultaneous Worker CPU work.

The slice does not Finish or Apply a drawing and does not replay project Undo/Redo.
It does not cover preparation failure, actual UI gesture release or repeated drag
sequences, snap candidate parity, raw private polygon draft storage, full web
project serialization, reference-image lifecycle, GPU or pixels. Those need
separate bounded gates. Native numeric draft history depth and private generation
identities are unavailable and are not inferred.

## Verification on 2026-10-06

- Clean isolated pending-only probe build from the unchanged `218b1a56` native
  source plus this diagnostic slice: passed (Qt 6.8.3, Node 24.19.0).
- All new contracts against that freshly built probe: **66/66 passed**, zero skips.
- Registered `m977_pending_input_contract`: **1/1 passed**. The inventory retains
  all 172 prior registrations and adds only this aggregate test (173 total).
- Broader offline JavaScript regression: **522/522 passed**, zero failures or
  skips. An initial run had seven missing-Acorn dependency failures; restoring the
  exact lockfile-pinned Acorn 8.15.0 dependency resolved them before this full rerun.
- Independent code review and exact 21-file candidate-manifest verification:
  passed. Candidate CMake changes contain only the added diagnostic target/test;
  concurrent reference-image and boundary-adoption changes are excluded.
- Fresh isolated Node/native comparison: **14 cases, 86 requested stages, 80
  paired stages, 70 exact field differences, six unavailable decision stages**.
  Raw files are `isolated-node.json`, `isolated-native.json` and
  `isolated-comparison.json` in the task's evidence directory.

The complete 173-test native suite has **not** been rerun for this candidate; its
combined regression is held for the separate concurrent product slices. The prior
172-test result belongs to the earlier baseline. Actual Chromium collection is
also still pending. Neither is relabeled as passed by the focused checks above.

## Reproduction and next step

See [the runner instructions](../tools/m977-pending-input/README.md). Local evidence
is stored in the task's `m977-work` directory; `isolated-comparison.json` records its
raw input hashes and binary hash. Review logs include actual-native stimulus
red/green, runtime mutation red/green, native duplicate-evidence red/green, and
focused test output.

Publish only the reviewed pending-input diagnostic files after explicit scoped
approval, then run the exact-commit CI collection. Review actual Chromium/native
observations before deciding how to address the empty-ready native behavior.
Passing diagnostic integrity is not permission to merge or a parity pass.
