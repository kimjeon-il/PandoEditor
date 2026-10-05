# M9.7.5 actual-browser model exchange gate

Local diagnostics only:

    node --test tools/m975-model-exchange/*.test.mjs

Authorized exact-commit GitHub Actions only:

    node tools/m975-model-exchange/browser-runner.mjs <native-runtime-dir> <evidence-dir>

The runtime artifact must provide `m975_model_exchange_probe`, `COMMIT`, `SHA256SUMS`, and `QT_VERSION`. The runner checks exact application HEAD/GITHUB_SHA/run identity, native binary SHA and actual process compiled/loaded Qt 6.8.3. Browser launch requires Playwright 1.62.1, Chromium 151.0.7922.34 (revision 1234), and actual CDP V8 15.1.206.8. No local launch or fabricated CI path is available.

The suite binds original/supplemental source hashes, fixture byte hashes, serialized harness/runtime source and every native/browser identity. It explicitly imports the archived production project serializer. Reports fail on missing, duplicate, extra or reordered cases/stages, incorrect observed flags, source/runtime/file identities, action outcomes, native QFile/canonical mismatch, or semantic mutations. Public Worker requests are captured at actual transport boundaries. Native input-observation count/order/coordinates are derived from each public action; intended and inverse geographic doubles must be exact, the maximum screen search remains four ULPs, and every chosen screen double is checked against its recorded signed IEEE754 ULP steps. Initial requested view, actual stage activity/readiness, current action-only error slices, persistence unchanged proofs, and a final source QFile reread are mandatory.

The combined runner preserves browser saves, sends them through the native QFile/controller probe, then actually reopens/resaves with browser production readers/serializer. It independently runs native public edit actions from the same input bytes, reopens/resaves each native web export in Chromium, and reimports into native. Complete web semantics and complete native return documents (except regenerated documentId) are compared. Every operation has explicit target/geometry/identity/reference effect contracts; an unrelated metadata change cannot fake an edit. Native active-edit application-save refusals are distinct from successful QFile codec snapshots.

Expected main exchange inventory: 10 fixture cases, 49 ordinary browser checkpoints, 2 full/delta-recovered checkpoints, 49 native checkpoints = 100 bidirectional exchanges, plus 1 raw-delta native import-only refusal and the rich native activation refusal. Success writes `capture-verification.json` with `state: complete`, `passed: true`, exact identities, counts and limits. Any failure writes `failure.txt`; no terminal pass is published early. The GitHub workflow belongs to the parent task.

The native CLI protocol is `probe <input.json> <request.json> <output-dir>`; JSON result is on stdout and every input remains an exact QFile byte payload. `nativeRequest()` contains the explicit public controller action DSL, including named stages and expected refusal outcomes.

See the fixture README for operation coverage, valid input/source provenance, delta environment context, and presentation-history limits. Node execution is diagnostic, never authoritative browser evidence. The compiled native public-action corpus is separately observed; authoritative Chromium and complete bidirectional file exchange remain pending; current inline archive synthesis is intentionally exposed by strict comparison rather than normalized.
