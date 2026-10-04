# T2-2a: geometry-bearing timeline storage checkpoints

Date: 2026-10-04. Companion to `timeline-contract.md` and `timeline-records.md`.
Bases: web `ccb180ac978a42d26dbcfd2c19c7a6a611764bfe`; app
`a5346062328bcfe8e9d3314363b7fdd79c972bea`.

## Status and ownership

This is the first storage part of T2-2, NOT completion of T2-2. T2-1 validated
record references against an injected predicate; this change preserves actual
geometry allocations with those records and validates them together.

Web gains an append-only `createGeometryVersionStore(entries)` because the
existing `geometry-versions.js` WeakMap is a renderer/history-copy cache, not a
persistent ID/version registry. That cache is unchanged. Native reuses the
existing `GeometryStore` from `document.h`/`document.cpp`; no second native
geometry implementation or validator was introduced.

The new functions construct storage/checkpoint values, not a live project store.
They are not yet called by the application serializer, autosave or history.
No existing geometry/parent/lifetime field is duplicated into live editable state.
The current project schemas, files, editing behavior and renderer are unchanged.

## Interfaces and snapshot value

Web:
- `createGeometryVersionStore(entries = [])`: `insert(ref, geojson)`, `get(ref)`, `snapshot()`.
- `snapshotTimelineStorage(records, geometries, entities)`.
- `restoreTimelineStorage(snapshot, entities)` returns `{ records, geometries }`.

Native:
- `snapshotTimelineStorage(const TimelineRecords&, const GeometryStore&, const vector<TimelineEntityIdentity>&)`.
- `restoreTimelineStorage(const TimelineStorageSnapshot&, const vector<TimelineEntityIdentity>&)`.
- `TimelineStorage` is a candidate value containing records and the existing GeometryStore.
- Snapshot geometry entries share `shared_ptr<const Geometry>` from that store;
  restoring copies/validates their coordinates into an isolated new store.

The JS storage value is:

```text
{
  schemaVersion: 1,
  records: TimelineRecords,
  geometries: [{ id, version, geojson: { type, coordinates } }]
}
```

The native DTO represents the same values with GeometryRef and immutable Geometry
allocations. Native production JSON decoding/encoding is NOT added here. The JSON
writer in the native test is test-only output for cross-platform comparisons.
Neither this DTO nor its tests prove that existing project files are interchangeable.

The entity catalog is a required validation input from the future project owner,
not another persisted copy of entity identity. Source/library IDs and renderer
revisions are not converted into geometry IDs or version numbers.

## Preservation and failure rules

Geometry IDs are nonempty strings and versions are integers in 1..4294967295.
Inserting an existing ID/version is rejected, even for identical coordinates.
No automatic version increment, overwrite, pruning, rounding, simplification or
winding correction occurs. Snapshots retain ALL versions, including old versions
and geometries used by other document domains. Multiple entities may reference
the same immutable version. Snapshot order uses UTF-8 ID order, then numeric
version, matching the native map for valid Unicode IDs.

Canonical Point/MultiPoint, LineString/MultiLineString and Polygon/MultiPolygon
values are retained. Structural coordinate/ring checks match the native geometry
owner: finite 2D geographic coordinates, required cardinality, closed polygon
rings and nondegenerate ring area. Holes, islands and dateline coordinates are
not rewritten. These are not polygon intersection or partition-topology tests.
Unknown JS archive/geometry fields are rejected rather than silently dropped.

Timeline geometry bindings must resolve to an actual Polygon/MultiPolygon version;
a point or line is not accepted merely because its reference exists. The T2-1
lifetime, overlap, gap and parent-graph checks run before a candidate is returned.
Malformed storage versions use `TIMELINE_STORAGE_SCHEMA`; existing geometry and
record error categories remain unchanged. Multiple unrelated faults have no
cross-platform first-error priority contract.

Snapshots cannot change when a later version is inserted. Restore does not mutate
the input or the previously loaded state. A caller must replace its current state
only after the returned candidate passes all later project invariants. There is
no mutation/rollback of current state inside these helper functions.

## Verification performed

- Web: 62/62 focused tests (27 geometry-store checks, 35 storage checks).
- Native: all 25 typed shared fixtures pass, plus checkpoint isolation, missing
  version, null allocation and failed-candidate preservation checks.
- Actual web/native comparison: 25/25 verdicts and 11/11 COMPLETE successful
  snapshots match, including geometry IDs/versions/types/coordinates and records.
- Native tests compile with C++17, `-Wall -Wextra -Werror -pedantic` for new code.
- Separate `-DNDEBUG -fsanitize=undefined -fno-sanitize-recover=all` execution
  instruments the selected real core sources and passes.
- Parity repeats under Asia/Seoul and America/New_York with the same result.
- JS snapshot -> JSON stringify/parse -> restore is tested; native tests exercise
  the typed snapshot path, not production Qt JSON or project file IO.
- Initial stub implementations failed 27 geometry tests, 35 storage tests and
  26 native checks before implementation; the committed code is not those stubs.

The local workspace contains exact pinned source blobs, verified by Git blob SHA,
not complete checkouts. Native compilation links the actual document.cpp geometry
owner, temporal.cpp, timeline-records.cpp and the new storage code; unused project
functions are removed with section GC, without test replacements for dependencies.
CMake source/test registration is updated and inspected. Full CMake/application
builds, browser/Qt UI, existing live serializer/autosave/history flows, remote CI
and independent review were not executed. No merge or deployment was performed.

## Remaining T2-2 integration

1. Finish the native production codec and strict wire validation using the current
   codec boundary; do not introduce a second JSON parser or old-schema reader.
2. Adopt a single canonical timeline/geometry owner in ProjectDocument and web
   project state, replacing superseded date/geometry/parent ownership together.
3. Wire full project serialization, restore, autosave and history to that owner;
   update current factories, schemas, exchange fixtures and worker snapshots.
4. Connect T3 resolution before enabling timeline documents for current editing.
   Existing single-state consumers must not silently overwrite historical data.
5. Verify real project file round trips, automatic recovery and command Undo/Redo.
   Manual checkpoint tests above are not completion evidence for these workflows.
