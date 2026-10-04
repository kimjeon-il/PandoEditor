# M9.7.2: selection value and controller integration

This is an incremental substep, **not completed M9.7.2 editing parity**.

## Behavioral sources

- Original observed web: `kimjeon-il/Pando@53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47`.
- User-approved narrow web correction: `12cd8c8ec47c83cfb8c650e8f44c81cdfac10043` on `codex/m972-full-annex-selection`.
- The correction allows an annex candidate to be archived after validated preview when it consumes the entire source. It preserves the candidate → archive → revalidated preview → review sequence and leaves bounded creation guards unchanged.
- Original fixtures remain immutable. A separate one-file delta manifest and executable correction corpus identify the changed workflow blob. The web branch has no automatic CI trigger; local focused tests and independent review are recorded separately from native CI.

## Implemented production changes

1. Root annex uses one `TerritorySelection` inside the existing `GeometryEditSession`. Worker-side polygon preprocessing performs `(drawing ∩ selected donor union) − target`; the sibling entry retains `prepareDrawnTerritoryAnnex` and its existing strict raw annex command. Selected remote donors remain in the editor's source list but only intersected donors enter the mutation patch.
2. The pure value `TerritorySelection` reuses the existing geometry calculator. It models ordered candidates, multiple selected fragments, archived parts, original component indices, supplied river-cell provenance/snapshots, method-change requests, and derived source/remainder geometry. It owns no canonical document, command history, worker lifecycle, or UI stage.
3. Root-country annex clips/removes donor children, matching the actual root web workflow. Sibling territorial annex retains its separately observed reparenting behavior.
4. Root annex rejects deletion that would leave distribution, label or territorial label-setting references dangling. It no longer silently applies sibling reference cleanup to those root operations.

## Executable evidence and limits

- The original selection corpus executes the actual web workflow and real geometry preparation, including candidate toggles, components, accumulated parts, method switches, river provenance, and the original full-donor drawing dead-end.
- The root corpus executes actual web country commit, worker/RPC, entity-store transaction, dependent transfer, reference validation and history. It records partial/full donor, children, selected remote donor, distribution, label and label-setting cases. Failed application must restore canonical document/history.
- The correction corpus executes the exact approved workflow delta, including full polygon and full-line selection and real full-polygon lifecycle.
- Native controller regressions cover oversized one/multiple/remote-donor drawings, pending and ready-preview cancellation, unchanged document before confirmation, and Undo/Redo.
- Native command regressions cover root child removal and dangling-reference rejection. Existing sibling regressions remain unchanged.
- Native helper tests replay actual web candidate and river-cell observations using the production native clipping kernel. Supplied river cells do not mean that native river partition generation is implemented.
- The calculation probe preserves distinct drawn and raw annex operations and now observes multiple donors. The first measured comparison is 9 calculation matches, 1 mismatch (disconnected merge), and 8 unobserved cases. This is not a full lifecycle parity pass.

## Controller integration (A–C)

- The active controller session owns the only selection value, explicit setup/selection/review stage, generation and computation/preview epochs. Source, candidate and component changes preserve input order immediately; zero-delay coalescing snapshots the newest state and runs derived clipping off-thread. Preview is debounced 300 ms. Cancel, Back, replacement and stale results cannot install older state.
- `CommandJobRunner::submitGeometry` uses the same scheduler, physical worker slot and owner-thread delivery as commands. Its tagged values are actual selection, draft and annex calculations, not fake successful command previews.
- `AnnexGeometryPreviewResult` contains real transfer geometry, ordered affected-owner before/after rows, geometric validation and a plan/patch receipt. Dangling references may have ready geometry preview; strict `CommandProcessor` preparation at Apply rejects them without canonical/history/autosave writes. The web applies then rolls back; native rejects before installation. Internal rollback events are not fabricated.
- Root source picking excludes nested general units and highlights selected donors. Sibling selection/reparenting contracts remain separate.
- Line (existing simple kernel), polygon and original components have real map and list selection, accumulation, removal, source/method confirmations, review, Back, Apply and Undo/Redo. Desktop and 360 px tests drive actual QML controls/pointers and check canonical bytes before every Apply.
- Candidate order follows the web graph's first source-boundary sub-edge, independent of gesture direction. This is an ordering adapter for the existing two-crossing kernel, not the deferred complex-cut engine. Hole-crossing, self-crossing and wrapped strokes are rejected explicitly instead of emitting invalid nominal pieces.
- Automatic component-to-drawing changes wait for a matching valid preview before archival. Failed automatic changes do not survive into later repaired selection; explicit discard confirmations survive background preview failure. Empty polygon transfer keeps the editable draft and reports `NO_TRANSFERABLE_SELECTION`.
- Native differential observations use real controller methods, event loop, worker jobs, production SVG overlays (inverse projection) and canonical serialized documents. Unexposed internal caches stay unobserved; original web empty-cache inconsistencies are documented separately. The original and approved corrected source hashes remain immutable.

Verification evidence for this A–C delivery is recorded in `m972-controller-validation.md`. Real GPU performance and all M9.7 editing parity are not established by these functional tests.

## Explicit remaining work

- Connect actual river partition generation/coordinator and its cancellation/source-identity contract, not only supplied-cell value handling.
- Root/sibling dependent behavior outside the observed corpus, original-polygon preservation and river sliver admission still need differential coverage.
- The original web workflow sometimes retains stale remainder/component caches after final deselection or deleting the last archived part with no live selection. Native value state recomputes the correct remainder. This is an explicit known mismatch, not an approved additional web fix and not a parity pass.
- Complex cut geometry remains M9.7.3, snap/shared boundaries M9.7.4, current-model exchange M9.7.5 and engine extraction M9.7.6.

No main merge, release or installation is part of this substep.
