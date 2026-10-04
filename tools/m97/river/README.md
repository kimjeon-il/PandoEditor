# M9.7.2 pure river kernel checkpoint

This checkpoint has no controller, QML, selection-cache or UI integration. The
application uses a fresh private Qt QJSEngine in its calling worker thread. Node
and Playwright are test tooling only. No Python or Node dependency is added to
non-test application builds.

## Immutable source and bounded compatibility

`assets/geometry/river/original` contains the exact river and planar modules from
`kimjeon-il/Pando` commit `53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47`. Their SHA-256
hashes, all six object-spread replacements, and generated-file hashes are in
`assets/geometry/river/provenance.json`. Original resources are never rewritten.

`generate-adapters.py` verifies those original hashes and exactly one occurrence
of each of six expressions before generating the separate Qt-compatible module.
Planar bytes are unchanged. `--check` verifies checked-in generated files without
writing; `test-adapters.py` proves deterministic bytes and missing/duplicate/hash
rejection. The application also verifies original and adapted hashes at load.
The existing polygon-clipping loader, hash and single Qt comma-return correction
are shared with `calculateGeometry`; its behavior and engine math are unchanged.

The private engine's platform bridge implements own-enumerable CreateDataProperty
copying (including own `__proto__`, symbols and getters read once), ordinary array
flatMap (holes, captured length, order and thisArg), and finite acyclic plain-JSON
cloning. It is not a general structuredClone or Array species/polyfill. Tests
explicitly reject Date, Map, undefined, nonfinite values and cycles. Timing falls
back to Date.now in the original module; only `diagnostics.computeMs` is excluded
from equality.

Qt Math.cos differs from the observed V8 result at 0.7719766168394622. A private
QObject bridge supplies the official Node v24.19.0 / V8 13.6 cosine closure from
`app/river/vendor/ieee754.cc`. Its source and BSD-license hashes are pinned; the Sun
fdlibm permission remains in both original and generated files. The V8 license is
also embedded as an application resource. Function bodies and word macros are
mechanically extracted without algorithm changes; only standard bit_cast/inline
scaffolding replaces V8 headers. A separate object target alone uses C++20 and
`-fno-fast-math -ffp-contract=off`, or MSVC `/fp:strict`. No V8 runtime/library,
process-global math patch, global optimizer change or engine reuse is involved.
Linux x86_64 Qt 6.8.3 is the locally observed scope; other engines, CPUs, compilers
and platforms still need evidence. ECMAScript does not promise identical trig
approximations across engines.

## Worker contract and identity

`app/riverpartitioncalculator.h` exposes owned typed donor/source/component
requests and Completed/Cancelled/Failed results, typed cells, donor statuses,
composition, provenance/segments and all diagnostics. `calculateRiverPartitionsJson`
is a narrow differential seam through the same path. No QJSValue or QObject is
returned or shared between threads.

Explicit donor revisions (including numeric corpus revision 1) and hydroRevision
are passed unchanged. Live donor revisions and sorted edit signatures are computed
with the actual engine's JSON.stringify, including exponent and signed-zero
semantics. Defaults, algorithm revision, config resolution, keying, graph work and
composition all come from the original module. Config overrides are explicit.
Composition preserves original component ordering/sourcePolygonIndex, excludes
all components of an invalid donor and leaves successful untouched originals
without river provenance. Raw geometry remains raw. The actual pinned polygon
normalizer is exercised separately after identity creation in the differential.

Synchronous JS cannot be interrupted mid-call. Cancellation is checked around
resource initialization, loading, invocation and decoding; it wins over thrown
errors and discards all output. The real-source cancellation test sets its token
while Serbia is still executing. Callers must dispatch off the GUI thread and
still guard their eventual result against stale sessions/revisions/tickets.

## Full-source provenance and the numeric decoder correction

The compact `tests/fixtures/web-m972-river/countries.json` is 87,619 bytes, selected
from the original country GeoJSON. The source hash, Git blob, selected IDs and
subset hash are recorded in its manifest. `extract-countries.py ORIGINAL --check`
reproduces it. No repeated decoded rivers, cell geometry or full expected-output
blobs are committed.

`oracle.mjs` executes the byte-identical existing pinned hydro worker and its
existing boundary/Earcut dependencies. It calls real logical discovery, reads all
logical packs, and executes original fragment merging. Every touched manifest,
index, core/detail and shard is size/hash-verified. Full datasets come from the
existing CI checkout at `c0bd31d13dc8495593d78cf51f7cc195de7c9469`:

- v0.13.0 is the untouched original regression corpus with geometryRevision=1 and
  separately computed live revisions. Serbia/Croatia/Moldova yield 17/12/18 cells.
- v0.13.1 is a separate current-native provider differential. It requires the real
  detail load and compares logical IDs, complete geometry and stable IDs. D1
  intentionally does not export the web built-in aggregate source_id field; its
  optional sourceFeatureId is edit provenance, a separate identity domain.
  Current enriched names differ from the original commit; the pin is explicit and
  no old source manifest or full-regression expected name is changed.

The original worker divides integer microdegrees by 1e6. The native logical
conversion previously multiplied by 1e-6, producing one-ULP input differences
(e.g. 15.566666999999999 instead of 15.566667), which changed graph identities.
`core/src/hydroformat.cpp` now performs the same division; exact IEEE-754 tests
cover positive/negative, antimeridian, near-polar, line and polygon coordinates.
This changes numeric interpretation only, not packed bytes, dataset schema,
manifest identity or physical integer cache contents. The provider's existing
per-open generation still identifies captured jobs; there is no persisted
converted-river/result cache in this checkpoint and no format-version bump.

## Durable gates and reproduction

Use the project Qt environment and serialize shared builds/tests:

```
source /tmp/m972-test-env.sh
flock /tmp/m972-build.lock bash -c '
  cmake --build /tmp/m972-build --target river_partition_tests m972_river_probe -j 2 &&
  ctest --test-dir /tmp/m972-build --output-on-failure -R "^(river_partition_tests|m972_river_oracle_contract|m972_river_synthetic_differential|m972_river_full_source_differential)$"
'
```

The full-source gate requires `PANDOEDITOR_HYDRO_FULL_MANIFEST` naming the current
v0.13.1 manifest and its v0.13.0 sibling. Missing data fails; it never skips.
For full native/Node artifacts run `differential.mjs --native PROBE --synthetic
--full --evidence DIRECTORY` (under the same lock locally). All deterministic
fields, coordinate bits, keys and array order compare exactly; only computeMs is
removed. Failures retain complete files when evidence output is requested and
print only bounded leaf differences, avoiding huge assertion-diff formatting.
Per-request native RSS/peak and provider-cache sizes are included as diagnostic
observations, not universal memory limits. The native bridge still uses the same
process/provider across every request in each suite, including six repeated
identical Serbia requests with a stable physical pack cache.

The early offline `tools/m97/*.test.mjs` CI sweep does not require full data,
Playwright or a browser. `.github/workflows/m97-editing-parity.yml` separately runs
an official Chromium gate after data/dependency setup: Node 24.19.0, official
registry integrity-locked @playwright/test 1.62.1, Chromium revision 1234
(151.0.7922.34). It runs exactly 18 synthetic plus 6 original real-source raw/live
cases using the original JS in Chromium. The only module-source adjustment is
redirecting the one relative planar import to its verified sibling Blob URL;
there is no Qt syntax, clipping or cosine adapter in Chromium. The runner verifies
all original SHA-256 values and every complete canonical output hash, records
browser/user-agent/runtime/commit information and uploads raw evidence.

The local browser routes were blocked and must not be retried or replaced with
hosting. Page generation and syntax verification are not browser execution. The
browser gate remains unverified until the coordinator publishes the exact commit
and observes that remote CI job passing. Independent review and the full native
aggregate also remain coordinator-owned before adoption/UI exposure.
