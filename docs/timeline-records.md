# Timeline records — T2-1 data contract

Date: 2026-10-04. Normative companion to `docs/timeline-contract.md`.

## Scope and integration boundary

T2-1 implements a pure, validated **record value**, not an application store or a
second editable world. It is preparation for T2-2, which must connect the existing
project serializer, restore, history, autosave, import/export, and immutable
geometry storage together. The live project schema is deliberately unchanged in
T2-1. No current document is interpreted as a timeline and no empty/missing fields
are treated as an old-format migration. This step does not implement a resolver,
UI, or dated editing commands.

The branch inputs are web `c01084dc32f519949bb92a610b064170fdff6a40` and app
`ea18f38ed2bd365c81b8271d7041906897aa2b59`. Web currently uses general/regional
entities; app still has Country/Subunit/Region and a different project codec.
Do not pretend these full project formats are already interchangeable.

## Record shape (schemaVersion 1)

```text
TimelineRecords {
  schemaVersion: 1,
  lifetimes:       [{ id, entityId, validFrom, validTo }],
  geometryBindings:[{ id, entityId, validFrom, validTo, geometryRef: { id, version } }],
  parentRelations:[{ id, entityId, validFrom, validTo, parentId, coverageMode }]
}
```

Every field shown is required. Every endpoint is an explicit null or a valid
nonblank temporal string (year/month/date). Do not round to months, infer a date,
or coerce a numeric value. Normalization preserves precision and the temporal
parser's signed-year spelling, trimming surrounding date whitespace only.
Unknown wire fields and schema versions are errors. `parentId: ""` explicitly
means no administrative parent; it is not a missing value to fill from a base.
`coverageMode` is `partition` or `explicit`.

IDs are nonempty opaque strings, compared exactly and never converted from a
number or an array index. Record IDs are unique across the three collections.
Logical entity IDs, record IDs, source IDs, and geometry references are different
identity domains. A geometry reference is a nonempty ID plus an integer version
in 1..4294967295. Reuse the app's existing GeometryRef type, not a new geometry
class. Bindings refer to immutable canonical geometry; coordinates, simplified
previews, and renderer revisions are not copied into these records.

## One owner; explicit validation dependencies

Web API: `normalizeTimelineRecords(input, { entities, geometryExists })`.
Native API: `normalizeTimelineRecords(const TimelineRecords&, const TimelineValidationContext&)`.

`entities` supplies a read-only catalog of `{id, entityKind: general|regional}`;
this is a validation input, not a new persisted entity collection. The caller
must provide `geometryExists({id,version})` against the authoritative geometry
repository. Missing dependencies fail visibly, and unexpected repository errors
propagate. Geometry existence is checked here; polygon/topology validation remains
with the geometry owner and later edit commands.

Both APIs return normalized copies and never mutate the input, its catalog,
geometry storage, the current project, history, or selection. Web returns deeply
frozen records and references. Native returns a value with no shared mutable
geometry. No singleton, cache, selection owner, or revision is introduced.

## Lifetimes and full coverage

Each catalog entity must have at least one explicit lifetime. Multiple disjoint
lifetimes are allowed (e.g. an entity that ceases to exist and later reappears).
An empty catalog with empty collections is valid.

All intervals have inclusive endpoints. At any valid calendar day:

- A logical entity has zero or one active lifetime, never two.
- While alive, it has exactly one active geometry binding and one parent record.
- While not alive, it has neither active geometry nor active parent records.
- There is no implicit timeless/default record, inheritance from the currently
  displayed world, last-write-wins, or array-order priority.

Thus missing geometry/parent coverage, one-day gaps, and shared inclusive
endpoints between records are errors. Adjacent months and adjacent calendar days
are valid transitions. Precision is retained in storage; comparisons use the T1
temporal parser. BCE/CE adjacency skips year zero; the T1 leap-year convention is
unchanged. Records may cross adjacent lifetime records, but not a gap in existence.

## Administrative relationships

Both ends of a nonempty parent relationship must be general entities and alive
throughout the relationship. Regional entities have no parent and cannot be a
parent. Roots and regional entities must use explicit coverage. Self-parenting
and cycles are errors at **every** date, including a single day not visible in a
monthly UI. Opposite edges in disjoint periods are not automatically a cycle.

These are administrative relationships only. Do not infer sovereignty, political
control, or recognition from parent/root. Those are not fields in this schema.

Validation sweeps actual interval-change boundaries, grouping removals and
additions at the same day before testing the resulting state. It never creates
monthly world snapshots or replays editing commands. Cost depends on records and
entities, not the number of centuries represented. This correctness-first model
validator is not the incremental display resolver planned for T3.

## Error categories

`TIMELINE_SCHEMA`, `TIMELINE_CONTEXT`, `TIMELINE_ID`, `TIMELINE_ENTITY`,
`TIMELINE_KIND`, `TIMELINE_INTERVAL`, `TIMELINE_GEOMETRY`, `TIMELINE_LIFETIME`,
`TIMELINE_OVERLAP`, `TIMELINE_OUTSIDE_LIFETIME`, `TIMELINE_GAP`,
`TIMELINE_PARENT`, `TIMELINE_PARENT_INACTIVE`, `TIMELINE_COVERAGE`,
`TIMELINE_CYCLE`.

Both implementations agree on success/failure and on the category of each
single-fault fixture. No priority between several simultaneous unrelated errors
is part of the contract. Messages may differ by platform. JSON shape errors are
web boundary checks in T2-1; native JSON decoding belongs to the codec integration
in T2-2 and is not claimed complete here.

## T2-2 requirements before activation

1. Connect one canonical timeline value to the existing project/history/autosave
   owners; replace superseded validity/dated-relation ownership rather than keep
   two independently editable versions.
2. Connect persistent immutable geometry references to current geometry owners;
   do not confuse web's WeakMap rendering revisions with historical versions.
3. Update the current schema, default project factories, serializers, restore,
   worker snapshots and exchange fixtures together; no legacy reader/migration.
4. Replace current generic parent/geometry reads with the T3 resolution boundary
   before enabling timeline documents for editing. Until then, reject unsupported
   timeline documents rather than silently dropping records.
5. Verify full project round trips, autosave recovery, Undo/Redo, atomic failure,
   and cross-platform geometry identity. T2-1 value validation is not evidence
   that any of these live application paths work.

## T2-1 verification record

Executed against exact fetched T1 dependencies, not a complete repository checkout:

- JS focused suite: 71/71 pass (68 fixture cases and 3 dependency/immutability tests).
- Native focused suite: all 60 representable semantic fixtures pass, plus dependency
  failure checks. Native tests use explicit failures, not assertions disabled by NDEBUG.
- Actual JS/native validation verdicts: 60/60 match, also with Asia/Seoul and
  America/New_York TZ settings.
- C++17 compilation: -Wall -Wextra -Werror -pedantic passes; separate -DNDEBUG
  -fsanitize=undefined -fno-sanitize-recover=all execution also passes.
- New JS syntax and changed-source whitespace checks pass.
- Web JSON record-value round trips, normalization idempotence, preserved IDs and
  geometry versions, unchanged input on success/failure, and exact-date precision
  are covered. This is NOT a full project save/restore round trip.
- The initial nonvalidating JS API failed 67 tests; the initial native API failed
  41 checks. Four additional boundary/content-preservation fixtures were then added.

Full application builds, full repository suites, Qt UI, live project persistence,
remote CI, and an independent reviewer were not run. CMake registration was added
and inspected; focused native compilation does not prove the entire CMake graph.
T2-1 is implemented; T2-2 remains pending. No merge or deployment is part of this step.
