# M7.6 — large-world edit implementation status

M7.6 uses the M7.4 canonical world document and the M7.5 scene packet cache.
Its canonical EPSG:4326 geometry and command/Undo semantics are unchanged.
This document records implementation, not a completed large-world gate.

## Connected paths

| Area | Implementation | Boundary |
|---|---|---|
| Change impact | Validated command preparation compares object geometry bindings in the before/after indexes. The committed result carries changed object refs, new geometry refs, request presentation targets, retained immutable geometry allocation count and estimated new coordinate bytes. | Only pure `territorial.geometry.replace` is eligible for the incremental scene path. Other commands take the full scene path because structural and presentation dependencies can extend beyond changed geometry refs. |
| Snapshot sharing | `ProjectSnapshot` holds a shared immutable `DocumentState`. `GeometryStore` copies its ref map but retains the same `shared_ptr<const Geometry>` for every unchanged version. | The ref map and document metadata are still copied for a candidate; this is not a copy-on-write document index. |
| Worker preparation | Annex, split, coast and shared-boundary geometry previews use `CommandJobRunner`; union/difference for shared boundaries now runs in that worker. The job carries an immutable snapshot and cancellation token. Completion checks the job ID, draft request sequence, project instance and revision before publishing a preview. | Simple replacement and content preview preparation still run synchronously. Existing structure worker preparation is retained. |
| Scene patch | A pure territorial geometry replacement retains packet buffers for unaffected objects, prepares changed objects, reconstructs world-base commands and recomputes canonical M5 draw ordering and revisions. New interaction highlights, view or quality changes force a full build; clearing the edit target/hover/chooser can use the patch. | Packet vectors and draw refs are copied into a new immutable scene. Large structural commands, content edits and Undo/Redo still prepare a complete typed scene. |
| Spatial candidates | A committed command advances the cached index using changed refs. Old dateline cell membership and legacy flat bounds are removed; replacement bounds are inserted. The update is staged before swapping index maps. Visibility stays outside the index and the existing exact hit test remains authoritative. | Staging copies the grid maps, so peak index memory is not yet proportional only to the affected set. Undo/Redo and project replacement fall back to rebuild on the next pick. |
| Diagnostics | `renderQuality` reports scene patch/full build counts, incremental spatial update count, affected object count, retained immutable geometry count and estimated new geometry bytes alongside existing packet cache bytes/hits. | New geometry bytes count coordinate storage plus the Geometry object; it excludes vector capacity, metadata, allocator overhead and GPU residency. Hardware time and peak worker memory are not claimed. |

The worker does not modify `ProjectDocument`. Its last good render scene stays
published until the owner thread confirms the preview. A stale, cancelled or
failed worker result does not call `CommandProcessor::confirm`. The existing
command confirmation remains one atomic project application and one Undo unit.

## Later verification and remaining work

| Item | Status |
|---|---|
| Full-world transfer/split/shared boundary/coast/annex and M5/M6 cross-domain equivalence | NOT RUN |
| Snapshot pointer identity, changed ref impact and scene packet reuse cases | NOT RUN |
| Cancellation/stale/Undo/Redo, save/reopen and failure injection | NOT RUN |
| Incremental dateline spatial membership and exact picker comparison | NOT RUN |
| Fresh Qt build, full CTest, sanitizers and world edit benchmarks | NOT RUN — deferred to the later Codex verification by request |
| Windows and Android physical device edit responsiveness | NOT RUN |

The QPainter fallback still rebuilds its `MapProjection` on geometry commit.
M7.5's scene graph upload scheduler can expose a partial new scene during
progressive uploads after commit; atomic visual promotion across those frames
is not established here. Command preparation may still copy document indexes,
and incremental spatial publication stages the whole cell map. These remain
explicit M7.6 gate findings; no elapsed-time or memory pass threshold is set.

Ruling: The user's later-Codex verification instruction supersedes the plan's
per-task TDD, Oracle, CTest and platform gates in this session. The code is
recorded as unverified and must be inspected and exercised before M7.6 can be
closed or the M7 branch merged into `integration`.
