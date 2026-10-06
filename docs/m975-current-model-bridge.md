# M9.7.5 current model bridge

This document specifies the native-v10 ownership implementation and its acceptance contract. Exact-HEAD regression and actual Chromium artifacts determine acceptance; it does not declare final M9.7.7 parity.

## Existing bridge reused

This work extends the existing current web schema 9 / territorial model 5 and immutable timeline archive bridge. It does not recreate a schema 7 adapter, add old schema 3–6 readers, or change the pinned web editing algorithms. The approved web source chain remains rooted at `53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47` and ends at `ad78780f79f4f38fcd7c2a3c2fb1fe0ba8e37c32` for this gate. Supplemental source closure uses that same commit.

## Native v10, web v9

New native saves use version 10. Existing native version 9 files remain readable; their archive rows are all conservatively retained as semantic data. No ownership is guessed from ID spelling, geometry equality or unused status. Saving an opened v9 document produces v10; merely opening it does not rewrite its source file. Older v9 applications reject new v10 saves. Web files and web exchange remain schema 9, with no native marker added to their format.

The native-only `geometryProvenance` object distinguishes original archive rows from geometry inserted solely to adapt inline web labels, hydro, generic features and geometric distribution entries. Native files retain all geometry versions. Web export omits only individually proven adapter-only allocations, restoring the original web archive rather than adding native implementation rows.

An ordinary fixture therefore has five original web archive rows, eight internal native rows and exactly five exported web rows. Existing polluted native v9 archives cannot be automatically repaired: their original ownership is unknown and all rows remain preserved.

## Ownership through edits

- Known typed adoption by another owner or a territorial timeline binding promotes an allocation to semantic data. Re-adoption by a previously detached/deleted creator also promotes it. Later deletion does not undo that promotion.
- Undo/Redo restore the complete ownership state with their immutable document snapshots. Preview, cancellation, validation refusal and failed publication do not mutate the active project.
- Unused derived allocations have distinct owner-deleted and creator-detached/rebound accounting. They remain in native storage. No original or promoted geometry is removed merely because it is unused.
- Opaque metadata changes are not proof of adoption. They retain native data and cause explicit web-export refusal while unresolved adapter-only rows would otherwise be omitted. A private immutable validation record prevents stale raw document copies from granting clean ownership evidence.
- Equal coordinates do not merge identities. Actual web reimport must reconstruct every live native GeometryRef exactly; unrepresentable identities are refused, not silently rebound.

Native QFile/controller saves, autosave payloads and native GeoPackage files carry v10 provenance. Native GeoPackage source markers must agree with the embedded native root version; current v9 packages remain readable. The autosave envelope itself is unchanged.

## Validation boundaries

The new corpus contains ten valid current-model fixtures. Its declared inventory is 100 bidirectional exchanges, plus explicit raw-delta import refusal and rich-timeline activation refusal. Each engine's real saved bytes pass through the other production reader/serializer. Native public edit checkpoints cover preview, cancellation, confirmation, Undo/Redo, retained-reference rewrites and object creation/deletion. Individual archive/ownership accounting accompanies every permitted native representation difference.

The gate uses pinned actual Chromium and Qt runtimes on the exact application commit. Node execution is diagnostic only. Existing fixtures, archived preimage failures and numeric equality contracts are not relaxed. Rich timeline storage/exchange remains distinct from the editor's static-only activation limit; Undo stacks are observed during actions, not claimed to be serialized into project files.

This gate is not final cross-engine edit-calculation parity, full DOM/file-picker interaction, pixel projection or GPU performance acceptance. The existing M9.7.6 engine extraction and M9.7.7 final differential gaps remain separate work.

## Open M9.7.6/7 handoff: input during selection preparation

During the M9.7.5 full-regression investigation, both desktop and 360px mobile pointer traces demonstrated a pre-existing input-continuity gap. Reproduction: begin annex selection, activate polygon mode, then send two map taps immediately while selection preparation is pending. The method button click is delivered and activeMethod is polygon, but selectionPending is still true. The first tap leaves zero draft vertices; a second tap after pending clears creates only one. If both taps arrive during pending, a later method-switch confirmation can be absent.

The direct cause is the explicit selectionPending rejection in `EditorController::geometryAddPoint` (`app/editorgeometry.cpp`). This investigation did not change that production behavior. The UI regression now needs a genuine ready-state precondition and an exact two-vertex assertion; waiting for readiness must never become automatic click retries or a claim that pending input loss was fixed.

M9.7.6/7 must compare the actual web event policy using the same pending-phase pointer stimuli, then resolve point retention, cancellation and preview/confirmation/history behavior before final editing parity acceptance. This gap remains alongside the earlier asynchronous, projection, cache-staleness and stimulus-coverage limits.
