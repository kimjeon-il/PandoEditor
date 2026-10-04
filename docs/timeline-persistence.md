# T2-2: production project persistence

The native feature branch continues from `98801f39e13e01ae74564efdc166f7a69a6047a8`.
The companion web implementation is `788f43fd27062851bdadf7fa6964819d43534685`.
Both current project formats are version 9; TimelineRecords remains schema 1.
Old development formats are rejected. There is no migration, alias reader, or
parallel source of territorial geometry or administrative relationships.

## Ownership and file paths

The existing ProjectDocument owns General/Regional identity and metadata,
TimelineRecords owns lifetimes/geometry bindings/parent relations, and the
existing GeometryStore owns immutable, versioned shapes. Shared GeometryRef,
Geometry, Validity and store declarations live in geometry-types.h to avoid a
document/records include cycle. No substitute engine or store was introduced.

Native QFile and QSaveFile persistence use projectcodec::decode/encode through
EditorController. Native GeoPackage uses the same v9 JSON codec as its
authoritative project state. Complex temporal archives need no derived spatial
tables; static spatial columns, IDs, shapes, properties and row sets (including
labels/generic/distribution content) and optional image assets must agree with
the JSON. Unknown columns or project attribute tables are rejected.
webimport::prepare reads current web full/project saves through
projectcodec::decodeWeb. projectcodec::encodeWeb(ProjectSnapshot) emits the
current shared web exchange format. Unsupported fields are errors rather than
successful imports/exports with dropped values. Native numeric metadata stays
lossless; web export rejects numbers that cannot survive JavaScript's finite
Number representation, before publishing any file.

Native/web territorial identities contain no inline shape or single lifetime/
parent/coverage value. Web Feature.geometry is null. Required timelineRecords
and geometries contain every saved version, including shared and unreferenced
historical shapes. Source provenance, names, capital, embedded flags and
arbitrary metadata survive supported exchanges. Historical library catalog
classifications remain source metadata; instantiated project kinds are canonical.
Administrative roots/parents do not imply sovereignty. Unsupported political
relationships fail explicitly.

## Publication, recovery and history

Decoding builds detached identity and geometry stores. Reference, temporal,
geometry and full project validation finish before Project publishes a candidate.
EditorController also requires static activation before changing selection,
history, dirty state, save target, renderer or import session state.

Until T3/T4, each editable territorial identity must have one unbounded lifetime,
one unbounded binding and one unbounded parent record. Complex projects can be
saved/exchanged by the production codecs and owner, but Qt UI opening and
existing rendering/editing paths reject them before publication.

Existing native full autosave uses the same production codec and frozen
ProjectSnapshot. The web's existing full/delta autosave includes complete
records and geometry archives. Delta recovery uses an exactly matching
fingerprinted baseline; absent/different baselines are rejected. A raw web
delta supplied without its baseline to the native import API reports
BASE_DATA_REQUIRED; a recovered full candidate is the exchange boundary.

Static geometry edits append immutable versions and update canonical bindings;
administrative moves update canonical parent records without replacing identity.
Command/transaction snapshots and Undo/Redo restore records together with shapes.
Workers receive immutable snapshots. Timeline cursor movement is session state,
with no content dirty flag, history entry or content revision.

## Verification

Evidence and platform limitations are recorded in [the validation report](timeline-persistence-validation.md).
The production QFile suite covers 31 cases, including month/day/year precision,
BCE/CE, disjoint lifetimes, parent changes, holes, islands, dateline coordinates,
shared/unreferenced versions, duplicates, gaps/overlaps and atomic rejection.
Nine additional numeric boundary cases cover rejected precision loss and exact
representable values. The actual Qt owner suite covers file save/open, native/web GeoPackage,
autosave, immutable Worker input, history and activation rejection.
timeline-exchange-oracle.mjs invokes the actual web serializer/project owner
and native QFile codec in both directions, including actual Worker content and
full/delta recovery. Existing regression targets remain registered; none are
deleted or skipped to reach a passing count. The user's final verification
scope is the ten persistence targets and actual exchange checks; full regression
validation is not claimed.

The removed webmigration.cpp and presentationmigration.h had no surviving
callers after the current-only v9 codec replaced old development readers.
Existing projection/backends, source kernels and renderer caches remain in place.
No T3 resolver, T4 dated editing, new timeline UI, main/integration merge or
deployment is included.
