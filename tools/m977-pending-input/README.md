# M9.7.7 pending-input observations

This is an additional diagnostic, not a replacement parity gate or a product fix.
It runs seven bounded scenarios at two configured profiles (1024×768 mouse and
360×800 touch capability): 14 rows and 86 requested stages. Browser calls use the
production `handleMapClick`, editing domain, tool configuration and Worker. Native
calls use the public `EditorController` API. Neither route dispatches DOM/QML
pointer events or establishes touch, pixel or GPU parity.

The approved Pando closure remains rooted at
`53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47`, with the approved correction chain
ending at `ad78780f79f4f38fcd7c2a3c2fb1fe0ba8e37c32`. Existing immutable manifests
are verified unchanged. The sole additional production module is tool-controller
Git blob `e841223c2c6ba6232476726039eed02f955a5f1c`. Neither historical `b2c7bb9`
nor current upstream main is used.

## Local diagnostic

Build the production-linked probe with Qt 6.8.3, then run:

```sh
cmake --build build --target m977_pending_input_probe
PANDO_M977_NATIVE_PROBE="$PWD/build/app/m977_pending_input_probe" \
  node --test tools/m977-pending-input/*.test.mjs
node tools/m977-pending-input/node-runner.mjs /tmp/pending-node.json
node tools/m977-pending-input/compare.mjs --diagnostic \
  /tmp/pending-node.json /tmp/pending-native.json /tmp/pending-comparison.json \
  --native-binary "$PWD/build/app/m977_pending_input_probe"
```

Use fresh native/comparison output paths. The comparator runs the actual native
binary itself, checks its SHA-256 before and after, and binds the raw report bytes.
A supplied pre-existing native report cannot be relabeled as a fresh execution.
The native-dependent tests explicitly skip without `PANDO_M977_NATIVE_PROBE`;
the registered `m977_pending_input_contract` CTest always supplies it.

`--diagnostic` is never Chromium evidence. A zero exit means that observations
satisfy their integrity contracts. The comparison still reports every exact
field difference, unavailable stage, `rawParity:false` and `gateAcceptance:false`.
It has no known-difference waiver and does not round coordinate differences away.

## Actual Chromium collection

Only authorized exact-commit CI may launch the browser. The separate
`.github/workflows/m977-pending-input.yml`:

1. Builds the public native probe from the exact commit and checks native tests.
2. Transfers its compiled test artifact with commit, run, Qt and binary hashes.
3. Uses the existing pinned Playwright 1.62.1 / Chromium 151.0.7922.34 revision
   1234 / V8 15.1.206.8 runtime.
4. Creates a fresh Chromium context per corpus row and checks actual viewport,
   pointer capability, document/Worker availability and absence of Node globals.
5. Reads and hashes every served production source before evaluation; uses
   bounded byte-exact exports; verifies complete Worker delivery transcripts.
6. Captures native observations freshly and emits the exact comparison.

The collector refuses non-CI launches before output creation or Playwright import.
CI sources, runtime functions, source closure, corpus, harness files, native probe
source and CMake file are bound in the suite identity. Raw suite/report bytes are
bound by `capture-verification.json`. These are test artifacts, not a portable
application distribution.

```sh
node tools/m977-pending-input/browser-runner.mjs evidence/chromium
node tools/m977-pending-input/compare.mjs --capture \
  evidence/chromium/suite.json.gz \
  evidence/chromium/browser-report.json \
  evidence/chromium/capture-verification.json \
  evidence/native.json evidence/comparison.json \
  --native-binary "$PWD/m977-runtime/m977_pending_input_probe" \
  --native-commit "$GITHUB_SHA"
```

See [the observation report](../../docs/m977-pending-input-observations.md) for
current results, causal differences and explicit coverage limits.
