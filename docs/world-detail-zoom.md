# World detail zoom policy

This change uses one preview mesh and one canonical mesh. It is independent of
the paused, uncommitted M9.2–M9.4 work. No M9.5 cache work is included.

| Projection | Preview threshold | Canonical threshold |
| --- | --- | --- |
| Globe | zoom <= 1.8 | zoom >= 2.2 |
| Flat | zoom <= 2.2 | zoom >= 2.8 |

The band between thresholds retains the last requested detail, including a
projection switch. Initial detail is preview. The active projection's camera
determines zoom. Selection focus and an open editor force canonical; edit forcing
lasts until the actual editor/session closes. Releasing forcing immediately
evaluates the current zoom, retaining the last requested detail inside the band.

Camera focus is currently synchronous. Its canonical request survives resize
and view publication and ends on the next valid explicit zoom, pan start, fit,
or projection selection, including an otherwise unchanged/clamped command.
Invalid input does not release it. Project replacement clears focus/detail.

Unavailable requested meshes retain the current visible mesh. Completion uses
the latest camera/editor policy; generation and document snapshot guards reject
stale loads. Opening a saved built-in world attaches meshes only after validating
canonical coordinates. An edit during this attachment discards the result and
retains precise document packet rendering; no retry is added.

Render precision and canonical document readiness are separate. A ready preview
respects document styling, visibility, draw order, interaction outlines and edited
geometry overrides. Startup placeholders retain their existing rendering path.
Picking, editing, save and autosave use canonical document geometry. Mesh changes
do not replace or serialize preview geometry into the document.

Verification covers exact thresholds, both directions, repeated band movement,
mode switches, focus/session lifetime, asynchronous latest-view selection,
missing canonical mesh, reopen, recovery and project replacement. Bounded CPU
pixel and QSG structure tests cover ready-preview ordering and outlines. These
checks do not establish a native GPU performance or visual quality guarantee.

Release validation and exact source/EXE hashes are recorded in the portable
package's BUILD-INFO.json and validation directory. Existing cross-platform
release gates remain open; this is a Windows preview. Long GPU stress and native
RHI performance tests are excluded because the earlier PC shutdown is unresolved.
