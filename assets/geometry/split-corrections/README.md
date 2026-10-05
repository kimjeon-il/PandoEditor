# Approved split geometry correction

The full `original/polygon-geometry.js` is byte-identical to Pando commit
`07d3e2053c71573e11c5cf89151f5f6686038511`, Git blob
`15f0ae1ee1873e42452778ca8b5e0f604ef5c925`. Its SHA-256 is
`a645827c46f7c62dbf929f850c67f350516669dfeafe5c637aec6c67efcfebf4`.
The immutable ten-file approval manifest is copied as `approved-manifest.json`;
`provenance.json` records the parent commit, tree, resource, generator, and license
provenance. No original river or cut vendor file is replaced.

These application sources are shared between the same owner's Pando and
PandoEditor projects. No separate upstream application license is asserted or
invented. Polygon clipping 0.15.7 is reused under the existing MIT notice at
`../LICENSE.polygon-clipping.txt`. Its existing `loadPinnedPolygonClipping`
loader keeps the same original SHA-256 and single reviewed Qt comma-return
correction. No geometry math or tolerance is supplied by this adapter.

Qt 6.8.3 executes the complete approved helper unchanged: no transformations,
missing exports, dependency pruning, or platform polyfills are required.
`loadApprovedSplitPolygonGeometry` exposes that shared verified loader for the
cut worker. Geometry calls each create a fresh worker-owned QJSEngine, run the
exact `wrapPolygonGeometry` or `normalizeClippedPolygonGeometry` function,
check input nonmutation, and decode exact ordered doubles and geometry type.

`wrapSplitGeometry` is for read-only geographic source/selected operands before
planar clipping. It unwraps shortest edges, subtracts overlapping periodic hole
copies, clips longitude strips, and retains untouched components. Do not wrap
the raw cut-worker source before candidate construction; its coordinate and
candidate ordering is part of the contract.

`normalizeSplitClippedGeometry` consumes a planar Boolean result. The approved
helper adds collinear vertices to edges wider than 180 degrees so a later wrap
cannot reinterpret the planar path. This is the upstream algorithm, not a native
repair or numerical approximation.

Empty output is a successful `GeometryOperationStatus::Empty`, never a valid
canonical replacement on its own. Failures retain the helper exception message
and stack; the native shape/finite-value guards fail explicitly. Cancellation
wins at boundaries and discards output, including an error from a running call.
The synchronous JavaScript itself cannot be interrupted mid-call. Callers retain
their cancellation and stale-session checks before accepting a result.

## Regression evidence

`normalizer-cases.json` contains 63 deterministic Node observations. The tracked
case definitions in `tools/m97/split-normalizer-cases.mjs` cover root and child
plain dateline sources, holes, untouched islands, multiple crossings, actual
approved cut candidates, and their Boolean remainders. Separate cases cover a
full-world periodic hole, tiny untouched island, a long strip edge, wide planar
edges/holes, noncrossing identity, and empty geometry. Expected coordinates,
ring starts, ordering, and geometry types are compared without normalization,
rounding, sorting, or tolerance. Input nonmutation and all cancellation boundaries
are also checked in native tests.

These Node observations establish regression coverage only. The required actual
Chromium 151 differential gate is independent; these tests do not claim browser,
UI, lifecycle, or full-application parity.

Verification:

- `node tools/m97/split-normalizer-generate.mjs --check`
- `node tools/m97/split-normalizer-cases.mjs --check`
- `node --test tools/m97/split-normalizer-source.test.mjs`
- `split_geometry_normalizer_tests` (native QtTest target)

The checked-in source and manifest hashes are enforced both in the generator
and at runtime. Regeneration refuses source or manifest drift.
