# Pinned cut worker kernel

The immutable baseline dependency closure of `prepareCutInWorker` comes from Pando commit
`53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47`. The original application modules
are byte-identical copies of the committed lifecycle source fixtures. The source
manifest, Git blob IDs, SHA-256 hashes, and every adapted expression appear in
`provenance.json`. These application files are shared between the same owner's
Pando and PandoEditor projects; no separate upstream application license is
asserted or invented.

Production imports `corrected-adapted/`, which applies the explicitly approved
`07d3e2053c71573e11c5cf89151f5f6686038511` dateline correction over the baseline.
The original baseline files and adapters remain retained. The active closure has
16 modules, including the newly imported `map-edit-geometry.js`; approved
originals, active transformed hashes, source Git tree and parent are recorded
separately. The shared approved polygon helper is evaluated unchanged by
`loadApprovedSplitPolygonGeometry`. Baseline and active import/export closures
are both tested, and dependency-pruning checks use the active source map.

Qt 6.8.3 does not parse object spread. `tools/m97/cut-kernel-generate.mjs` uses the
existing pinned Acorn 8.15.0 parser to replace only the object-spread expressions
listed individually in the manifest
with ordered, nested calls to the existing own-data merge helper. All other
source bytes are retained. Adapter tests reverse each insertion and require the
exact original bytes. Unsupported object forms and source drift fail closed.
The cut dependency closure imports only `freezeEditingGeometry` from the UI
packet module. Its adapted module is the byte-identical initial prefix containing
that function and its private WeakSet. The full original is retained; the
manifest records the exact byte range and omitted exports, and the generator
rejects new references to omitted exports or unexpected imports. This avoids
Qt's unsupported async UI function without rewriting its semantics.

No geometry algorithm, coordinate, candidate identity, ordering, error text,
projection calculation, or numeric operation is replaced.

Runtime dependencies are reused, not forked:

- D3 3.5.6: `../river/original/d3.min.js`, its reviewed redundant-var-only
  `../river/adapted/d3.min.js`, `../river/d3-provenance.json`, and BSD notice
  `../river/LICENSE.d3.txt`.
- Polygon clipping 0.15.7: `../polygon-clipping-0.15.7.js`, the existing
  `loadPinnedPolygonClipping` Qt loader, and `../LICENSE.polygon-clipping.txt`.
- Plain-data platform compatibility: `../river/platform.js`.
- Missing `Array.prototype.at`: `platform.js` provides standard relative-index
  array-like lookup only when absent, without overriding any arithmetic.

The native host verifies all original and adapted hashes before importing the
module into a fresh worker-owned QJSEngine. It invokes the exact pinned worker
entry point, checks for input mutation, and returns complete owned JSON including
line, snaps, assessment, errors, and every ordered candidate. Cancellation is
checked around loading, invocation, and decoding. The synchronous JavaScript
call cannot be interrupted in the middle; cancelled outputs are discarded.

The six existing `cut-corpus.json` cases are Node observations and only regression
coverage. They are not a claim of real-browser parity. Browser acceptance must
compare full unrounded values against the pinned actual Chromium runtime.

Verification:

- `node tools/m97/cut-kernel-generate.mjs --check`
- `node --test tools/m97/cut-kernel-adapter.test.mjs`
- `cut_geometry_calculator_tests` (native CMake target)
- `m973_cut_kernel_probe` accepts one JSON payload or a payload array on stdin;
  it returns `{result,inputUnchanged}` or an array of those envelopes on stdout.
  The probe does not load expected fixtures, round numbers, sort candidates, or
  normalize coordinates.
