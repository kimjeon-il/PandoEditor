# Current application boundaries

This document describes the current implementation, superseding the initial
country-only/Qt Shapes/version-2 sketch. The Web counterpart owns shared behavior
contracts and fixtures; this repository keeps C++ implementation and Qt adapters.
The refactor baseline is App 3934077519bb716cbb45b683bbb63d85dcc8d5ee paired with
Web 008b99b5ca2dd39936e51f7ddd11c0c70fc7bb74.

## Ownership and direction

- Core owns ProjectDocument, immutable DocumentState snapshots, identity/reference
  validation, temporal records/geometry versions, CommandProcessor and ChangeSet.
  Project owns document history, revisions and the saved-state checkpoint.
- MapEngine owns camera/projection/edit calculations, selection calculations,
  boundary sessions, detached geometry draft history, scene packets and resource
  policies. It has no dependency on Qt UI or file dialogs.
- App owns Qt input/value conversion, publication, platform resource lifetimes and
  asynchronous adapters. EditorController owns one Project and one SelectionState.
  selectedId is derived from the primary selection and its project instance, not
  maintained as another mutable selection field.
- QML consumes the same controller commands for desktop/mobile. The renderer and
  MapSceneBridge consume immutable model/scene data and revision-based derived
  caches; they do not own a second editable ProjectDocument.

Direction: QML -> application workflow -> Core/MapEngine. The composition boundary
supplies platform I/O and execution services. Internal filenames need not match
Web filenames; domain semantics and observable operations are the shared contract.

## Commands and detached editing

CommandProcessor retains prepare/confirm/cancel and immutable candidates. Ordinary
and asynchronous entrypoints preserve their existing commit, validation and error
boundaries. CommandJobRunner owns Qt task delivery; its workers receive snapshots
and job tokens, not mutable Project or QObject ownership. Late results retain the
existing instance/revision/cancellation checks.

GeometryDraft in the existing editgeometry engine owns draft geometry, vertex
indices, draft undo/redo and drag history transitions. GeometryEditSession extends
it with the existing tool, preview/job, provider and Qt presentation state. No draft
mutation writes document history; confirmation still uses CommandProcessor.
Boundary-session history and territory-selection history remain with those owners.
Qt notifications, request increments, snap indication and scheduling retain their
existing controller positions. Vertex cancellation and object no-op history keep
their distinct existing behavior.

## Storage, timeline and resources

projectcodec handles current native and Web exchange representation. ProjectStorage
handles bounded reads and atomic platform writes; ProjectAutosave owns delayed
writes. The controller still controls user confirmation, active-edit restrictions,
replacement publication and when a successful save marks the Project saved.
Serialization alone is not activation: temporal activation restrictions remain
explicit. Native storage and Web exchange are separate formats; version numbers
come from the current codec, not from this document.

Timeline records/storage, geometry provenance, source identities, GIS adapters,
historical catalog, place/hydro/terrain providers and renderer handoff mechanisms
remain in their existing canonical modules. This refactor does not revise their
formats, algorithms, retries, resource budgets or unsupported capabilities.

## Validation

Web owns tests/fixtures/portability and tools/check-platform-portability.mjs.
This repository pins its source commit and file hashes in platform-portability-pin.json.
Run tools/verify-platform-portability.mjs with the Web root and built selection_probe
path. The comparison checks complete ordered results; its report includes
expected/processed counts, source pins and native executable hash. It verifies selection only; existing timeline,
geometry, command, persistence and Qt UI tests remain independently required.

Archived Web oracle sources and earlier fixture pins are immutable. New evidence
must name its actual source pair. Existing platform differences are not repaired
as part of this behavior-preserving refactor. No deployment or packaging is implied.
