# T2-2 project persistence integration

Execute inline on the existing feat/timeline-app and feat/timeline-web branches.
Do not merge or deploy. Keep T0, T1, T2-1 and T2-2a semantics and checkpoint APIs.

Goal: integrate one canonical timeline value and immutable geometry archive with
real project codecs, atomic restoration, autosave and existing command history.
Architecture: general/regional identities; TimelineRecords owns lifetime, geometry
selection and administrative parent/coverage. Existing stores own metadata and
geometry values. Static views are derived, never a second editable document.

Global constraints: project format 9, required timelineRecords (record schema 1)
and geometries; metadata-only territorial wire objects; exact IDs, dates and
coordinates; no migration, duplicate writer, sovereignty inference or T3/T4.
Nonstatic documents may persist but cannot activate in the pre-T3/T4 editors.
Only unbounded single lifetime/binding/parent records are statically editable.

## Review focus

Whole project failures must preserve selection/history/dirty/save target; archive
versions must not be pruned; delta base must match; no current-shape overwrite of
dated records; actual native production codec and cross-project exchange evidence.

## Task 1: Canonical project ownership

- Test metadata identities and timeline validation against actual document owners.
- Split native shared geometry/validity types to avoid circular includes.
- Replace native Country/Subunit/Region ownership with General/Regional.
- Replace per-entity geometry/validity/parent ownership with TimelineRecords.
- Keep non-temporal metadata and reject unsupported political relationships.
- Connect static commands/factories to immutable versions and canonical records.

## Task 2: Production codecs and exchange

- Write failing full-project storage/restore cases before implementation.
- Switch native/web current formats and factories to 9 together.
- Require timelineRecords/geometries; reuse T2-2a storage validation.
- Reuse losslessjson, projectcodec, webimport::prepare and web serializer.
- Add projectcodec::encodeWeb(ProjectSnapshot); verify actual project round trips.

## Task 3: Atomic restore, autosave and history

- Test failed candidate preservation before changing publication boundaries.
- Prepare and validate complete candidates before any live/reset effects.
- Store complete records/archive in full/delta autosave; validate delta base.
- Carry records and immutable geometry in existing history/worker snapshots.
- Gate unsupported temporal activation before current state changes.

## Task 4: Verification and publication

- Establish fresh focused baselines; record existing failures separately.
- Verify holes/islands/dateline, versions, exact temporal precision, gaps/overlap,
  malformed wire input, recovery, commands/Undo/Redo and cursor noncontent state.
- Verify web -> native import -> native web export -> web project semantics.
- Final user steering: run only necessary checks. Build the app and the ten
  focused persistence targets; run those and actual web/native exchange on
  Windows and Linux. Do not rerun full CTest or full CI. Report
  Windows and available Linux/headless evidence independently, never count unrun.
- Fresh whole-branch review, then scope-only commits and nonforce push to the two
  existing feature refs. Verify remote heads. No main/integration merge or deploy.
