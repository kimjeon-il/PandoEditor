# T2-2a timeline geometry storage implementation plan

> Execute inline on the existing timeline feature branches; do not merge/deploy.

**Goal:** Preserve real geometry versions together with the T2-1 records in validated checkpoints.
**Architecture:** Web append-only geometry registry; native existing GeometryStore;
read-only snapshots and isolated restore candidates. No live schema activation.
**Spec:** `docs/timeline-storage.md`.
**Global constraints:** Same IDs, uint32 versions, exact coordinate values and
source date precision; no pruning, implicit fallback, migration or renderer mutation.

## Review focus

Past versions omitted by a current-only save; a point accepted as territorial
geometry; mutable snapshot aliasing; failed restoration replacing current state;
claiming actual autosave/Undo integration from helper-only tests.

## Completed implementation steps

- [x] Inspect pinned record, geometry and project serialization owners on both branches.
- [x] Write geometry and storage tests; observe failing stubs before implementation.
- [x] Implement web `geometry-version-store.js` and `timeline-storage.js`.
- [x] Implement native `timeline-storage.h/.cpp`, reusing GeometryStore/TimelineRecords.
- [x] Generate native fixtures from the shared `timeline-storage.json` corpus.
- [x] Compare actual complete snapshots, not only success/failure or fixture names.
- [x] Run focused suites, strict new-source C++ compilation and UBSan.
- [x] Add source/test registration to core/CMakeLists.txt; inspect its focused diff.

Publication uses only existing `feat/timeline-web` and `feat/timeline-app` refs,
with pinned parents and non-force updates. Verify remote heads/blob hashes afterward.

## Ruling / handoff

This completes only the geometry-bearing checkpoint part of T2-2. Full live
persistence cannot safely be activated independently of its resolver consumers.
The remaining native codec, canonical project ownership and real
serializer/autosave/history/worker integration are listed in the spec and remain
unimplemented. Do not relabel this checkpoint as complete T2-2 or a complete timeline.
