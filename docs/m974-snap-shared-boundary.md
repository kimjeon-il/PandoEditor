# M9.7.4: indexed snap and shared-boundary workflows

Scope: an implementation checkpoint, not universal editing/source-history
parity. Acceptance requires final exact-commit CI receipts. The remaining
lifecycle integration work is explicitly listed below.

## Oracle and scope

The editing oracle is web commit `53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47`
with explicitly approved corrections ending at
`ad78780f79f4f38fcd7c2a3c2fb1fe0ba8e37c32`. The checked-in source manifest
and extraction guards identify the actual production dependency closure. The
original 35 snap and 33 boundary inputs are immutable. Twelve separate
supplemental inputs cover diagonal epsilon, signed half-grid quantization, and
an affected owner already at the destination.

Authoritative numbers come from Chromium 151.0.7922.34 / V8 15.1.206.8. Node
runs protocol tests, but does not substitute for the numeric browser oracle.
No new editing engine is introduced here; extraction remains M9.7.6.

## Snap

`geometrysnap::Index` retains source insertion order, uses the existing
GeoSpatialIndex plus conservative segment envelopes, and queries nearby
segments rather than scanning every territorial unit for each pointer event.
Raw dateline endpoints, shifted query windows, globally deduplicated vertices,
intersection ownership, and stable candidate order follow the pinned source.
All candidate classes compete by screen distance; mouse and touch radii are
10 and 18 pixels respectively. Finding a vertex does not stop edge search.

`GeometrySnapProvider` owns readiness and invalidation through the existing
CommandJobRunner. A cold query returns no candidates. Its real worker result
is available to a later event, never retroactively applied to the original
pointer event. Snapshot, source identity/revision, tool, ordered owners, query
cell and projection margin participate in the cache contract. Stale callbacks
cannot restore cancelled or replaced state. Immutable source insertion ranks
are synchronized at root-general geometry/membership worker-notification
events, project installation, actual candidate queries, and boundary preparation. Root deletion and
Undo are observed even without an intervening query; generic-only and
child-only transient mutations remain unobserved until a worker-equivalent
query. Requests capture the rank snapshot; delayed
workers cannot roll it back. Unchanged pointer/view publications neither scan
source membership nor copy that map. Hover on an empty draft is ignored,
so moving the pointer before the first click does not pre-warm that click.

The controller projects snap candidates into the same visible flat editing
overlay coordinates as its pointer and indicator, using the existing
MapProjection and camera display transform. High-latitude mouse/touch and
marker-center tests guard against mixing this space with the geographic
renderer projection. This is native display-adapter coverage, not a claim of
new browser-pixel or globe-editing parity. Touch-owned markers survive Qt's
stationary mouse hover-exit event; subsequent real mouse movement takes over
normally, and session changes reset this ownership.

The structural diagnostic checks that warm queries prepare no geometry and
retain a bounded nearby candidate set as distant geometry grows. Debug CPU
measurements are diagnostics, not a device/GPU performance acceptance gate.

## Shared boundaries

The session preserves actual and virtual references, ordered node owners,
external/parent-fixed nodes and incident segments. Multiple selected owners
are supported. Disconnected adjacent pairs are permitted where the web permits
them; an isolated selected owner is rejected. Automatic child entry uses the
seed-connected eligible sibling set.

Dragging changes session visuals. Release constructs the owner patch and
prepares the canonical preview. Preparation geometry remains the original
source after preview cancellation, impact decline, or failure, so repeating a
gesture uses the same original node key. Descendant transfer/clip/removal
approval is a separate apply-time decision. Changes commit atomically.

Whole-tool root cancellation follows the pinned web's actual fallback: original
selected items are restored, but the last item becomes primary. The web snapshot
omits per-item keys and its restore helper cannot recover the saved primary key.
This observed quirk is preserved rather than silently repairing the oracle.
Child cancellation and preview discard retain their distinct source policies.

The calculator validates full geometry/area and hierarchy effects. Its
boundary receipt privately owns immutable canonical plan/patch values and
cannot be forged by changing public diagnostic fields. Commit checks the live
project identity, revision and re-derived references. The generic core patch
API is an admission layer, not a complete polygon-intersection proof. Its
independent guards must not reject geometry which the actual calculator allows
under the pinned area tolerance; ambiguous area belongs to the calculator.

For boundary commands specifically, territorial label settings remain live
presentation state during Undo/Redo, matching the web snapshot contract.
Deleted territorial settings are not resurrected by Undo. Custom-label history
and other command policies are not broadened by this boundary alignment.

## Evidence boundaries

All differential receipts keep `rawParity: false`. Exact geometry comparison
uses dyadic predicates, permitting only representation-equivalent ring starts,
winding, component order and exactly collinear segmentation. Raw helper
candidate order, references, owner order and numeric coordinates are not
rounded, sorted away or accepted with epsilon.

- Original controller inputs without an exact native public projection
  preimage are explicitly unobserved; geographic helper evidence does not
  replace those pointer observations.
- Browser pixel hit testing, GPU rendering, native raw preview packets, parent
  preview presentation, and private numeric history depth remain outside the
  paired controller observations.
- Native LabelSettings has no web `visible` field. Reference presence and all
  supported payload fields are compared; that missing field is not fabricated.
- Two provider input stimuli are not equivalent: malformed web source geometry
  rejected by the native document model, and reallocation of equal geometry
  changing only JavaScript identity. Separate native diagnostics do not count
  as equivalent-input passes.
- Four asynchronous boundary cancellation probes wait for the actual native
  worker pool to finish without processing owner events, then invoke the real
  cancellation/selection action before queued delivery. This differs from the
  browser harness holding an RPC result. Native boundary node fan-out is
  synchronous and has no delayed boundary-move worker result to intercept.

Additional narrowly recorded observation limits:

- The original boundary corpus has three exact public-projection input
  omissions. The separate threshold corpus has six more; these are distinct
  corpus counts, and helper results do not stand in for those controller stages.
- Two malformed selection fixtures reference absent objects. Native selection
  sanitizes those refs while web selection retains them. Array parity is not
  claimed for those inputs; actual entry rejection, canonical state, history,
  supported reference fields and each side's normalization are still checked.
  A valid existing generic object cannot use this exception.
- Three stale-response diagnostics use different invalidation stimuli. The web
  harness changes generation/stateRevision; the native public API changes
  selection, because project replacement is prohibited during active editing.
  Each side's actual settled selection and unchanged canonical/history state
  are enforced, but those settled selections are not an equivalent-input
  selection-parity pass.
- The separate source-order-v2 browser diagnostic exercises actual client and
  Worker source removal/restoration. Its native counterpart uses real controller
  deletion and Undo. Shared source geometry and first snapped vertex owner are
  compared; this is not claimed as a complete web application Undo workflow.

Source-history timing is not universally equivalent yet. The verified hooks
cover root-general notifications, installation, snap queries, and READY boundary
preparation. Remaining audited paths include pending Worker stop/deferred rebase,
stage 2/3 selection/cut/preview operations, render-driven boundary/highlight
queries, and render/rollback rebases. Web stop can retain a READY candidate cache;
a blanket reset on native cancellation would not be an equivalent fix. These
paths require a separately scoped lifecycle integration and differential corpus.

The existing M9.7.3 input, preview and presentation limitations remain in force.
