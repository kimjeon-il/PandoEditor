# P7 fixed-source place mechanism exchange

This probe compares production native PlaceRuntimeStore, PlaceRuntimeProvider,
MapLabelEngine and core copy/Undo behavior with immutable Web commit
`ebcfae4d27b29cbbea6416a7045a4806930204be`. It does not modify production,
source fixtures, source pins, or existing expected values. Native execution and
build registration belong to the root agent.

## Evidence and operands

`tools/place-runtime-contract/native-parity.mjs` verifies every one of the 24
source/artifact hashes in the original place source manifest before executing
the copied original Web PLAC decoder, worker store, runtime scheduler, D3 camera,
projection-frame helpers and label layout. It additionally reads two exact Git
blobs from the same commit without modifying Web: `app-generic-commands.js` and
`app-territorial-labels.js`. Their hashes are emitted as `sourceOwnerHashes`.
The actual nested copy function and actual copied-ID suppression expression
are evaluated from those blobs with dependency adapters; neither expression is
rewritten. The copy adapter records the function's real history/autosave calls.
Restoring its recorded before/after snapshots checks suppression derivation;
it is not a claim that the complete Web project-history application was run.
The native counterpart executes actual core command confirm, Undo and Redo.

The synthetic `basic`, `dense`, and `overscan` PLAC bytes remain unchanged. They
prove bounded mechanisms only. Production `assets/data/places/manifest.json`
is the fixed `empty-v1` manifest with no source tiles. Real city-data acceptance,
including the requested real-source review corpus, remains **BLOCKED**.

There are exactly 39 normalized cases:

- One complete basic PLAC decode, preserving IDs, source/sourceId, names, kind,
  geographic coordinates, country/feature codes, population, priority, minZoom
  and notes.
- Thirteen viewport queries: flat, zoom threshold, translated native pan,
  dateline, front/back globe, DPR 1/3, mobile/desktop safe inset, complete safe
  culling, dense 1500 cap/order and overscan filtering. Tile counts and every
  ordered candidate record are compared exactly.
- Six search inputs: empty, one character, Korean with spaces, exact city name,
  BOM/NBSP normalization, and an absent prefix. Normalized text, ordered records
  and truncation are compared exactly.
- Empty production source and immediate store cancellation, including no tile
  cache admission after cancellation.
- Eight runtime transitions: initial publication, movement/deferred demand,
  settle, viewport cancellation, latest request, shared search selection,
  project/source replacement and close. Scheduling-dependent numeric metrics
  are not substituted for publication, snapshot and source-selection facts.
- Eight label cases: base collision, selected builtin, simultaneous territorial
  country group, ordinary place collision, copy suppression, Undo restoration,
  Redo suppression and hidden labels. Builtin replacement must preserve the
  document source count/revision.
- Transient/project separation plus copied provenance, pinned placement,
  single native Undo, Redo suppression and unchanged source values.

The native flat camera represents pan through translation. The flat-pan operand
sets native geographic center 150 degrees and adds its projection displacement
to translation, yielding the same geographic viewport midpoint zero as the Web
`flatCenter` operand. This is a representation adapter, not a shifted expectation.
Globe camera rotation uses the native sign versus D3's opposite sign. Source
record coordinates and record ordering receive no tolerance or normalization.
DPR is a camera operand; place labels use CSS dimensions. Layout operands use
the established mobile 96 / desktop 32 CSS bottom inset. Actual Qt window-width
classification and attached-screen DPR are exercised by separate controller/UI
tests, not claimed from these direct mechanism probes.

## Label geometry disposition

The eight engine exchange cases supply the fixed Web Unicode-count boxes to the
native production engine, isolating priority, selection, visibility, collision
groups and suppression. They do not claim the native controller's real font
metrics match those boxes. The native probe separately emits the actual Qt
application font name, width and height for each basic record alongside fixed
Web dimensions. The comparison report lists each exact difference under
`fontGeometryDifferences`, with disposition **REPORT**. It does not accept a
tolerance, rewrite source dimensions, or claim pixel/layout-geometry parity.

## Execution

Native target requirements: `tests/place_parity_probe.cpp`, production store
and provider, PandoMapEngine/Core, Qt Core/Gui/Concurrent and AUTOMOC for the
provider. No QML, codec, device or GPU execution is required by this probe.

```powershell
$env:QT_QPA_PLATFORM='offscreen'
& '<root-build>/place_parity_probe.exe' `
  'D:/dev/worktrees/p1-p8-m98/Pandoeditor(App)/tests/fixtures/web-place-runtime-source' `
  'D:/Codex/evidence/p1-p8-m98-20261006/p7-place-native-01.json'
node tools/place-runtime-contract/native-parity.mjs `
  --native 'D:/Codex/evidence/p1-p8-m98-20261006/p7-place-native-01.json' `
  --web-repo 'D:/dev/Pandoeditor(Web)' `
  --report 'D:/Codex/evidence/p1-p8-m98-20261006/p7-place-exchange-01.json'
```

Any mechanism mismatch makes the Node comparison exit nonzero and records the
actual native/Web row. No expected fixture is regenerated. Missing native rows
or additional native rows are mismatches. The initial Web-only run exited zero:
24 hashes verified, 39 mechanism cases, zero native comparisons. Its output is
`D:/Codex/evidence/p1-p8-m98-20261006/p7-place-web-only-01.json`; this is not native
GREEN or actual cross-system parity evidence.
