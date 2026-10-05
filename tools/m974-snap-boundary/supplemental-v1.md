# Separate M9.7.4 threshold diagnostics, version 1

The original 35 snap and 33 boundary inputs remain immutable. These 12 additional
synthetic cases have their own input manifest, source identity, report schema,
artifact and exact-commit browser capture. There are no expected numeric golden
values derived from Node.

- Four diagonal virtual-node cases use the immediate floating-point predecessor,
  supplied value and successor of 7.071067811865474e-8, plus its negative value.
  The opposing shared edge is (-2,-2) to (2,2), with the node at (a,-a).
- Six signed half-grid cases use ±5e-8 and each immediate representable neighbor.
  Production quantized node grouping, actual refs, virtual refs and ordering are
  retained exactly.
- Two shared-node cases move to the coordinate already held by one owner, using
  offsets 4e-10 and 4e-8. The direct production worker returns all affected owner
  patches even if one owner's coordinates are unchanged.

Each case records two distinct paths:

1. The original actual application boundary workflow, including its no-op gesture
   guard, preview/confirm/cancel and history when reached.
2. A separately labeled actual worker boundary-prepare and boundary-move request,
   with complete request/result/error envelopes, alongside the actual production
   topology calculation.

The 4e-10 movement is smaller than the application's 1e-9 gesture no-op tolerance.
The direct worker result must not be presented as a successful UI gesture. Invalid
preparation, fixed handles and rejected movement remain explicit observations.
Pixel projection, hit testing, GPU rendering and native parity remain unproven.

Capture in the authorized exact-commit CI environment:

    node tools/m974-snap-boundary/supplemental-browser-runner.mjs evidence/supplemental-chromium

Artifacts use the separate m974-supplemental-chromium-<commit> name. Generation of
new inputs is deliberate and is not run by the capture workflow:

    node tools/m974-snap-boundary/supplemental.mjs --write-supplemental-inputs
