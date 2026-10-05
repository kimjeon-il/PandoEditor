# M9.7.3 split parity

## Behavioral sources

The original web oracle remains Pando `53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47`, with previously approved full-annex and removal overlays through `6c3f930b8573fa09991885b661879ea36725472e`.

Two explicitly approved corrections are recorded separately:

- `07d3e2053c71573e11c5cf89151f5f6686038511`: geographic seam-aware split selection, preview and commit; periodic holes and wide planar clipping output are preserved.
- `ad78780f79f4f38fcd7c2a3c2fb1fe0ba8e37c32`: remove deleted-child visibility/style settings and restore them only when identity membership changes during Undo/Redo. Later preferences on surviving objects remain current.

Exact source trees, Git blobs and SHA-256 values are under `tests/fixtures/web-m973-split/corrections/`. Original observations remain losslessly compressed with their decoded hashes; corrected observations never replace the original bug evidence.

## Production implementation

- The complete pinned cut worker dependency closure runs in a fresh, private Qt engine. The limited platform/syntax adapter preserves every arithmetic expression. Original and approved corrected source closures remain separately verifiable.
- Multiple crossings, rings, components and wrapped longitude partitions produce ordered candidate lists. Equal-area defaults retain the first minimum; selection combines candidates in source order.
- Split uses the existing bounded territory selection session. Selected pieces form one new sibling; all unselected and untouched source components remain with the original object.
- The internal binary `retainedPart` intent and two-crossing calculator were removed. Split commits consume an explicit selected union.
- Root creation clips/removes dependents. Child creation can reparent contained children; unchanged/carried descendants preserve their geometry and immediate relations. Locked unchanged grandchildren do not invalidate a valid ancestor transfer.
- Creation uses fresh metadata/style defaults. Source references are not rewritten to the sibling. Root dangling references still reject commit when the actual web transaction rejects them.
- Root exhausted selection may have an inert preview, but bounded selection cannot archive or confirm it. Child exhaustion rejects preview.
- Child preview/apply validity retains the selected identity captured when its request begins. Root source targeting remains stable across ordinary selection changes.
- Geometry predicates understand geographic seams through read-only views. No stored geometry is rewritten merely to satisfy validation, and no validation bypass or JavaScript engine was added to the core.

## Verification boundaries

The final acceptance requires the native aggregate and actual pinned Chromium gate for the same published application commit. Focused tests and Node discovery are not substitutes for that gate.

The differential evidence distinguishes:

1. Original geographic inputs and the full cut worker output, compared directly without screen-coordinate conversion.
2. Actual controller input through its current public projected-point API. A bounded adjacent-double preimage search is allowed only when the observed geographic coordinates equal the intended doubles exactly. Unrepresentable original inputs remain recorded as such. Separately named common fixtures are supplied identically to both implementations, preserving topology and operation outcomes.
3. Production mutation intent and final geometry, properties, object references, confirmation/cancellation and native Undo/Redo/reopen.

No coordinate epsilon or zero-area XOR result can turn a coordinate mismatch into a pass. Any allowed ring representation handling must remain explicit and preserve numeric coordinates exactly.

Camera-dependent cut preparation also requires identical input views. The existing M9.7.2 controller corpus now explicitly supplies its original web flat projection, scale, center, rotation, viewport and pointer policy through public native camera APIs. Every observation checks the actual native view against the web runtime and captured cut payloads. A default native globe view is not interchangeable with the web's flat fixture, even when both receive the same geographic line.

Two inactive-cache observations retain different raw `remainingGeometry` values after empty selection. Both raw values and disabled-action state are preserved, and actual reactivation must recompute before archive/commit. These are diagnostics, not a raw-parity pass. Missing observations are not converted to null or empty identifiers.

Predicted parent relations are not stored as a separate native preview receipt value: the core applies them transactionally. Final parent/reference results are compared exactly; the preview-parent observation gap remains explicitly assigned to the aggregate M9.7.7 gate rather than filled with a second predictor.

The screen-input adapter is temporary evidence infrastructure. Projection/edit API unification remains M9.7.6. Performance tuning, engine reuse, native GPU/device acceptance and M9.8 remain separate work.
