# M9.7.3 actual-browser / native checkpoint

The `split-chromium-oracle` workflow job is independent of the native aggregate.
It runs the pinned official Playwright package with Chromium 151.0.7922.34 and
V8 15.1.206.8, checks CDP runtime versions, and serves every hash-verified module,
vendor, and classic Worker at its original same-origin relative path. There is
no Node-generated expected browser report and no alternate browser executable.

The baseline capture preserves the original source chain, ordered lifecycle
inputs, all lifecycle observations, and 277 raw kernel payloads: all 27 original geographic
inputs plus 250 deterministic flat/globe stress inputs. The stress payload sequence has seed 32498, 200 flat and 50 globe
cases, and JSON SHA-256
`fabbbb99853ac3ceaef31e4099c9192b8d66c7c9a30fc3aa8dc77fbda044fe1a`.
It includes the nine globe cases independently found to differ between Node and
Qt. Neither Node output nor those diagnostic differences is a golden value.

`split-controller-parity` downloads browser evidence and exact-commit native
executables, verifies executable and browser-report SHA-256 values, and invokes
the compiled public `EditorController` and `prepareCutGeometry` entry points.
Every case is retained, including errors; missing or reordered observations
fail closed. Individual raw controller reports, normalized comparison views,
complete native kernel output, and the difference report are persisted before
reporting failure.

## Three independently reported boundaries

The original 27 browser lifecycles remain unchanged. Eleven geographic input
sequences have no exact legacy screen preimage. Their native attempts are
preserved as unrepresentable input evidence, never counted as passing UI cases;
their original geographic doubles still run through both raw kernels.

A separate common controller corpus runs the same explicit fixtures on both
sides. Four invalid-cut fixtures move source and gesture latitude by -5 degrees
to make their view equator-symmetric. Five child fixtures change only the parent
envelope to latitude -30..30, retaining their original child source and gesture.
Two further dateline-dependent fixtures use explicitly declared binary-exact descendant longitudes (179.25 and 179.5 with their negative counterparts), preserving original source and gesture. Actual web outcomes and ancestry were checked before/after; the original inputs are retained. The declarations preserve every before/after input and a reason. Hole, island,
and dateline traits are not removed. Actual raw-kernel and original selected-union
mutation coverage remains separate from this controller-input boundary.

The controller source chain explicitly appends the approved dateline commit
`07d3e2053c71573e11c5cf89151f5f6686038511` to the original `6c3f930` baseline,
verifies every vendored Git blob and SHA-256, and includes ten additional
root/child dateline cases. Separate module URL prefixes and fresh browser
contexts prevent the original and corrected modules from sharing cached imports.

## Comparison boundary

- Candidate order, selected candidate indices, candidate areas, and actual
  owned candidate geometry are compared. Ordered part geometry is observed
  through the existing read-only diagnostic seam.
- Native synchronous finish dispatch acceptance is recorded separately from
  its settled candidate-success outcome; the latter is compared to the
  awaited web finish outcome.
- Web child entrypoints have no user-chosen source-country list. The semantic
  source identity is read from actual `baseSourceFeatures`; native sources are
  read from actual providers. Both raw representations remain in evidence.
- Initial object identities are never renamed. A single actual newly created
  object is mapped by before/after identity difference, and that mapping must
  survive Undo/Redo and the actual committed selection intent. Preview-only
  created IDs have a separate ephemeral role.
- Polygon/MultiPolygon containers and winding may differ. Geometry comparison
  canonicalizes exact boundaries using binary64 dyadic (BigInt) collinearity,
  removing a vertex only when it lies exactly between its neighbors. It preserves
  exterior/hole roles, ignores ring start/winding and polygon order, and never
  rounds coordinates or uses polygon-clipping XOR as equality authority.
  A one-ULP boundary movement fails. Arbitrary alternate polygon decompositions
  fail rather than receiving an approximate equality.
  Kernel observations use complete strict JSON value equality instead.
- Final geometry, names, parent/coverage relations, created/retained/deleted
  identities, reference targets, presentation, and opaque generic ownership
  metadata are observed independently at each checkpoint. No expected-state
  reducer is used by the native probe.
- Review owner identities, geometry and predicted parent relations come from
  actual receipt rows. Parent values are retained at the existing production
  calculation branches, including moved children, clipped descendants and
  carried grandchildren; they do not authorize or drive commit. The same
  actual ephemeral sibling identity map is used for preview owners and parent
  targets. Missing, conflicting, null and unknown parent identities fail closed;
  the empty string remains an explicitly observed root parent. Native flattened
  preview features must exactly preserve their raw receipt rows and order.
- Review completed-preview presence is observed separately: explicit absence
  and an empty completed preview are different observations, and omitted
  presence/status evidence is invalid. Native receipt presence and its closed
  calculation status are explicit independent observations. A failed calculation
  retains its full raw receipt, detail and rows, but has no completed preview;
  a completed blocking-validation result does have a preview. Neither the
  controller's failure ownership nor its UI error/readiness behavior is changed.
  Canonical relations before Apply, Cancel, final relations and references,
  and actual Undo/Redo remain independently observed. Existing archived browser
  evidence may diagnose a new native candidate; it never certifies that newer
  candidate as an exact-commit Chromium gate.
- Cancel, rejected commit, Undo, and Redo also check native canonical document
  byte hashes and public history availability. Exact native stack depth,
  pixel rendering, and private cache fields outside the owned observations are not claimed.

## Geographic input bridge

The public controller currently accepts legacy projected screen coordinates.
The probe first uses the public projection, then searches at most four adjacent
representable screen values in either direction per axis. A chosen value is
allowed only if the public inverse projection returns the intended geographic
double exactly. Naive and chosen values, signed ULP steps, the bound, intended
coordinates, and the actual controller input line are all recorded. If no
exact preimage exists, that case is a failed/unobserved input, never a pass.
This test-input bridge is explicitly temporary pending stage-6 interaction
parity; it does not correct native geometry or change browser expectations.

## Running checks

Prebuild protocol tests are part of the ordinary `tools/m97/*.test.mjs` suite,
excluding the two compiled-probe tests until their executables are built.
After building, run `split-differential-native.test.mjs` with
`PANDO_M973_SPLIT_PROBE` pointing to `m973_split_controller_probe`.

Only the authorized exact-commit GitHub Actions job launches Chromium. The
browser JSON is stringified inside Chromium and transferred in bounded
256-KiB UTF-16 chunks with surrogate-pair boundaries, byte count, and SHA-256
verification before native comparison. Reports are not converted through a
large nested Playwright serialization call.

## Inactive-cache diagnostic

The upstream final-deselection path retains a stale `remainingGeometry` after
selected IDs and combined geometry are empty and preview/archive/advance are
unavailable. Native recomputation instead exposes the full source remainder.
Both exact values stay in the report. This is not a raw parity pass.

A separately named reactivation case reselects the same actual default candidate,
observes fresh selection-worker and preview activity, and compares its rebuilt
geometry/readiness before archive, commit, Undo, and Redo. Only this narrow
inactive-cache difference may be classified for an observable gate; any other
field difference, missing reactivation, wrong source remainder, or usable stale
cache fails. Reports always distinguish `rawParityComplete: false` from the
bounded observable result and retain original UI-input representability gaps.
