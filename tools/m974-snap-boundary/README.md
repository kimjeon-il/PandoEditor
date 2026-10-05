# M9.7.4 actual web workflow oracle

This corpus contains synthetic inputs and exact production source bytes. It does
not contain Node-produced browser golden values. Actual observations are created
only by the dedicated exact-commit GitHub Actions workflow using Playwright
1.62.1, Chromium 151.0.7922.34 revision 1234, and V8 15.1.206.8.

## Provenance

The 104-file transitive production closure is pinned to original web commit
53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47 and the four explicitly approved corrections
12cd8c8 → 6c3f930 → 07d3e20 → ad78780. The source manifest records every original
Git blob, final Git blob, final SHA-256, byte count, and all five commit/tree IDs.
It includes corrected current territorial models, preview/history dependencies,
worker code and imported vendors. It is not assembled from mixed-era model APIs.

production-sources.json.gz uses deterministic gzip. The source manifest pins the
hash and length of the decompressed JSON. Every browser-served production file is
hashed before import. Existing vendor license notices are retained in the source
bytes; license texts also remain in tests/fixtures/web-m97/licenses/.

The three boundary gesture callbacks are extracted without modification from
app-domain-assembly.js. The complete module hash, UTF-8 offsets, slice hash and
callback names are captured. Suite verification independently re-extracts that
span. Canonical production source owns the geometry, previews, transaction commit,
reference effects, and history; the driver supplies headless presentation ports.

## Coverage and limits

- 35 snap cases: actual pointer cache → dedicated worker → indexed candidates →
  production editing-domain resolution/draft/indicator/undo/redo; all candidate
  classes, distance and tie order, source insertion history, raw dateline
  endpoints, generic/locked targets, root/child working split sources,
  source revisions, actual cancellation, stale/rebase/replacement and error retry.
  Hidden-target behavior is source-audited only; fixture hiddenIds are not applied
  to a live visibility state and do not establish observed visibility parity.
- 33 boundary cases: actual multi-selection/auto-child entry, preparation,
  actual/virtual refs and fixed owners, exact gesture callbacks, preview/cancel,
  explicitly pending impact modal and modeled cancel/confirm decisions, atomic
  confirm/Undo/Redo, 3+ owners, recursive child transfer/clipping/removal,
  holes/MultiPolygon, disjoint pairs, locks, invalid/no-op and delayed real replies.
- 101 actual browser Math.hypot/geometry-snap diagnostic pairs, including the
  reported libc/V8 rounding pair. These are numerical observations, not a web fix.

The boundary driver invokes geographic gesture callbacks. It does not observe
pixel hit testing, boundary pointer projection, GPU rendering, or a separate
multi-owner draft undo/redo API. Those stages are explicitly unobserved. Native
helper comparisons use actual browser-observed candidate projections, so they
must not certify native projection or controller parity. rawParity remains false.

## Commands

Protocol and real-worker Node discovery tests (not browser golden generation):

    node --test tools/m974-snap-boundary/*.test.mjs tools/m97/river-report-transfer.test.mjs

Actual browser capture, authorized exact-commit CI only:

    node --max-old-space-size=512 tools/m974-snap-boundary/browser-runner.mjs evidence/snap-boundary-chromium

Native helper comparison, after the compiled executable's commit and SHA256SUMS
have been independently verified by the native CI job:

    node tools/m974-snap-boundary/compare-browser-native.mjs browser-directory native-executable output-directory

The native driver strips expected candidates/result/indicator before invoking the
compiled probe. Browser-observed projection points and actual source insertion
order are retained. Omitted observations, duplicate rows, missing required fields,
changed source/runtime/input pins, and altered extraction provenance fail closed.

Captured artifacts include suite.json.gz, suite-pin.json, CDP runtime identity,
browser-report.json and deterministic gzip, bounded-transfer byte count and SHA,
snap-probe-input.jsonl, boundary-observations.json, math-diagnostics.json and a
capture-verification summary. A Node structural protocol test uses an in-memory
claimed-runtime envelope only to test validation; it is never persisted as a
browser observation.

Changes to inputs or source pins require deliberate regeneration and review.
Neither generation command is used by the capture job:

    node tools/m974-snap-boundary/sources.mjs --generate EXACT_APPROVED_WEB_CHECKOUT
    node tools/m974-snap-boundary/protocol.mjs --write-inputs
