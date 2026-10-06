# M9.7.7 pending-input lifecycle tail

This separate 2-case / 58-stage gate continues matched desktop mouse (1024×768)
and 360px touch-capability (360×800) public inputs through real drawing Finish,
preview/review, Cancel, retry, Apply, project Undo and Redo. It does not replace
or amend the historical 14-case pending-input observation corpus.

## Executed production path

- Exact current-model `m975-annex-partial-refs.json`, including child hierarchy,
  retained labels, hydro, generic content, distribution references and presentation.
- Existing approved source-history and model-exchange closures, plus the already
  pinned `tool-controller.js` supplement. No production source or pin changes.
- Web `handleMapClick` → actual `createEditingDomain`/`toolDraftDefinition` → actual
  territory workflow, real Worker, preview/commit and project history services.
- Existing production `createSpatialIndex` wiring supplies actual revision and
  Worker history synchronization rather than the older fixture wrapper's no-op.
- Native actual web import, public geometry controller operations, real dispatched
  selection worker and canonical native codec/web export.

Corpus v2 sets the explicit test view scale to 572.9577951308232 (10 pixels per
degree). Actual pinned D3 and native public inputs must inverse exactly to the
shared geographic coordinates; observed screen points must remain in viewport.
This is a test setup, not a production projection change. The earlier scale-1000
corpus and its raw diagnostic reports remain under their original identity.

Each attempt sends pending points `[2,2]`, `[4,2]`, with the actual completed Worker
result withheld from owner delivery. Ready must be empty, including draft history.
It then sends `[-1,0]`, `[4,0]`, `[4,10]`, `[-1,10]`, finishes and archives the real
candidate, observes ready preview/review, and cancels or applies. No click retry,
private model patch, queued-point replay or fabricated worker result is used.

## Exactness and scope

Cancel and every pre-Apply stage preserve the complete raw save within each
runtime. Native Undo restores exact full native bytes; Redo restores exact applied
bytes. Native web exports follow the corresponding native snapshot exactly.

Web Undo/Redo use the established model-exchange `documentKeys` history contract:
territorialEntities, sourceInfo, labels, genericFeatures, hydroEdits, timelineRecords,
geometries, distributionLayers and distributionEntries. Values, reference identities,
array order and numbers are exact. Full raw web Undo/Redo equality is **false**:
production snapshot restoration reorders one generic source object's keys and adds
six empty itemVisibility groups. Every raw save and all seven raw deltas are retained;
any additional presentation/value/order change fails the contract. The persisted
fixture installer does not represent full application startup; no baseline
normalization is inserted to make this result disappear.

The paired gate compares every shared interchange field, ordered geometry
ID/version and reference graph at every stage, plus candidate/part boundaries and
project/draft history, pending, ready and preview outcomes. Geometry uses the
unchanged `exactGeometryBoundary` from the existing split comparator: polygon
wrapper, cyclic ring start/winding and exactly proved collinear representation are
handled without clipping, rounding or epsilon. One-ULP boundary drift fails.
Full raw geometry/tree differences stay in the report. Only the six specifically
enumerated empty web visibility groups differ outside geometry after history.

Web preview observations are linked to original request/result/delivery bytes,
Finish candidates, archived parts and applied geometry. Native observations are
linked to ordered public state-change signals and public candidate/part receipts.
Missing/failed tail stages and missing native execution fail rather than skip.

This is public handler/controller evidence, not DOM/QML hit testing or physical
mouse/touch dispatch. The held barrier is worker-complete, owner-delivery-pending;
simultaneous Worker CPU cancellation and preparation failure are unclaimed.
Full startup, raw cross-engine parity, GPU and pixel acceptance are unclaimed.
The existing corrected empty-ready QML lifecycle in `tests/ui_tests.cpp` is reused
as independent UI coverage; this slice does not modify it.

## Local diagnostic

Build target `m977_pending_lifecycle_probe`, then:

```sh
export M977_PENDING_LIFECYCLE_PROBE=/absolute/build/app/m977_pending_lifecycle_probe
node --test tools/m977-pending-lifecycle/*.test.mjs tests/m977-pending-lifecycle-integration.test.mjs
node tools/m977-pending-lifecycle/node-runner.mjs /absolute/evidence/node-report.json
ctest --test-dir /absolute/build -R '^m977_pending_lifecycle_contract$' --output-on-failure
```

Node is diagnostic only. Plain `compareLifecycle` always returns
`actualBrowser:false`, even if callers attach a Chromium-looking label.

## Actual Chromium gate

The standalone `.github/workflows/m977-pending-lifecycle.yml` builds native and
runs the collector on the same exact authorized application commit. Publication
requires separate permission. Do not run the collector locally or set fake CI
variables. The collector fails before browser launch outside exact-commit CI.

Runtime is imported unchanged from `tools/m97/river/suite.mjs`: Playwright 1.62.1,
Chromium 151.0.7922.34, revision 1234, V8 15.1.206.8, Qt 6.8.3. The browser dependency
manifest and lockfile and the transitive executable harness graph are hash-bound. The gate
checks exact committed harness bytes, full decompressed capture bytes, browser
context/runtime, CI run/commit, freshly executed native binary SHA-256 and raw
native stdout. It preserves native warnings in a separate log and all cross-engine
raw differences; passing functional contracts is never promoted to raw parity.
