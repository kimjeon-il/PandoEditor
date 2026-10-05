# M9.7.5 valid-v9 model exchange inputs

These are new saved-file fixtures. They are not edited copies of M9.7.2–4 observations or regenerated browser goldens. Every file is a complete web v9 / territorial-model-5 save with valid UUID content IDs, supported presentation fields, and exact bytes/SHA-256 in `manifest.json`. Both production readers receive those same input bytes before editing. No ID repair or content insertion occurs after an operation.

## Explicit input revision 2

The complete revision-1 input bytes and manifest remain in `v1-inputs.json.gz`, authenticated by the revision-2 manifest. `v1-native-preimage-failures.json` preserves four genuine native diagnostics where the unchanged requested geographic coordinates had no exact public screen preimage within four ULPs for the original asymmetric inline-content scene bounds.

Only four revision-2 fixtures change initial, unrelated inline content coordinates: annex partial (M=10), child split (M=30), and both boundary cases (M=2). Label becomes [40,0], generic point [41,0], and hydro line [[45,-M],[46,0]]. Their territorial geometry, edit coordinates, IDs, metadata, source information, and action definitions remain exact. The other six input files are byte-identical. The new symmetric scene envelope enables the bounded exact input setup; it does not fix the retained asymmetric-projection limitation or introduce geographic rounding/tolerance. Tests enforce this complete change scope.

## Fixed inventory

- Annex partial refs: donor remains; target and donor shape versions change; dependent refs survive.
- Annex full recursive: donor and both children are removed; survivor references remain.
- Annex rejected deleted reference: the same full annex with a donor distribution reference rejects atomically in the production workflow.
- Split multiple island: three selected pieces create one sibling, source and untouched island remain.
- Split child descendants: a sibling under the fixed parent is created; original identities and immediate parent relations are preserved or changed by the production edit.
- Boundary root descendants: three owners change, whole descendants reparent, cut descendants clip, removed descendants and presentation are cleaned.
- Boundary child fixed parent: the same descendant effects with fixed parent P unchanged.
- Generic delete: requested UUID fallback point removed; Undo restores and Redo removes it.
- Split dangling reference: an actual production commit rejects rather than publishing a dangling descendant reference.
- Rich timeline: full storage exchange plus explicit activation refusal.

Six territorial positive cases have before, preview, cancel, confirm, undo, redo. Generic deletion has before, confirm, undo, redo. Each rejection has before, preview, cancel, rejected. Rich storage has storage. The full-annex case also saves a real full autosave and nonempty changed+removed delta, then uses the matching baseline to recover it. Missing/wrong fingerprints reject. Raw delta and raw recovered storage remain separate artifacts.

## Source identity

The original approved 106-file M9.7.4 source-history closure is reused unchanged at `ad78780f79f4f38fcd7c2a3c2fb1fe0ba8e37c32`. The M9.7.5 supplemental manifest adds exactly two same-commit executable files (`app-color-picker.js`, `custom-color-control.js`) to complete the actual hydro history normalization ports. Complete `app-environment.js` is hashed evidence only; a unique byte-bounded literal extracts its real HYDRO_TOOL_CONFIG. No behavior repin or copied normalization algorithm is used.

The fixture authors deliberately do not add synthetic archive records for ordinary inline label, generic, or hydro geometries. Any native archive growth/loss is therefore a strict exchange mismatch, not an allowed normalization.

## Observations and limits

The runtime uses the actual production serializer (`buildProject`, `buildAutosave`), storage/activation readers, store, editing workflows, geometry preview, Worker/RPC and history. Existing harnesses supply only UI/fixture ports. Boundary geographic callbacks come from the exact pinned production source. There is no file-picker, pixel-projection, full-DOM or GPU claim. Browser and native generated IDs/geometry are not claimed equal across edit engines: each engine's own complete checkpoint must survive exchange.

All raw files, UTF-8 lengths and hashes remain authoritative. Web whole-file comparison has no dropped fields, ID-array sorting, coordinate rounding, or tolerance. Native return import may recreate only `/documentId` (the production decoder derives it from incoming web bytes). Every other native field, geometry version and GeometryRef is exact.

Undo is checked against document/history fields. Presentation remains checkpoint-specific: for example, removed territorial label settings need not reappear on Undo; empty visibility groups can appear, and deletion Redo can prune a stale generic visibility key. These actual production observations are not normalized away.

A raw delta omits physicalSourceInfo by production design. Its recovered storage is checked against the full project except that one explicitly absent header. A subsequent actual `buildProject` receives the recorded existing terrain/hydro environment source context and emits the standalone save exchanged with native. This is not a claim that raw delta alone preserves that header. Raw delta native import must refuse with BASE_DATA_REQUIRED before any project publication is attempted, with the source QFile reread unchanged and current document/history preserved. This import-only check makes no unexecuted destination-sentinel claim; active-editor save and native export refusals retain their separate actual sentinel checks.
