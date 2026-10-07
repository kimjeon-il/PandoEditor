M98.5 independent editing diagnostic
==================================

This diagnostic executes production editing mechanisms independently of the full
performance acceptance run. It does not supply an editing budget or approver.
The actual production place manifest is hash-pinned `empty-v1`; full-data
acceptance remains blocked. Synthetic geometry and the miniature hydro dataset
cannot replace Standard, Dense, Editing-heavy, or Large-world acceptance fixtures.

The 14 cases are:

- Mouse 10 px and touch 18 px snap sweeps, each with 500 and 5,000 distant objects
  and 300 changing pointer coordinates. The production `geometrysnap::Provider`
  schedules real `CommandJobRunner` calculations. `resolveSnap` uses the native
  flat projection with viewport 1920 x 929. Every returned candidate and all
  available spatial-index counters are recorded. The first query includes cold
  preparation; subsequent provider cache hits are identified by submitted count.
- Annex provider selection, polygon drawing, candidate calculation, archive,
  preview, Apply, Undo, Redo, Undo, and repeated entry/cancellation.
- Actual miniature hydro provider loading and partitioning, component selection,
  archive, part deletion, repartition, preview, Apply, Undo, and repeated cancellation.
  A run fails if the provider reports zero loaded rivers.
- Original six-crossing, hole, MultiPolygon, and dateline split inputs from the
  immutable M973 capture. Only the original `cases[].input` values are copied into
  this separate diagnostic manifest. No captured expected values are rewritten.
  Original view/coordinates are retained. The pinned capture identifies its own
  historical behavioral source; it is not relabeled as the current fixed Web
  golden. Current preview rejection, if any, is recorded with its actual error
  and fails this diagnostic case; it cannot stand in for a measured preview.
- Shared-boundary 2/4/8/16 owner gestures using explicitly synthetic disjoint
  square sectors sharing one central node. The outer union stays fixed during
  the gesture. Each verifies the actual controller owner count, query, drag,
  automatic preview on release, confirm, Undo/Redo, canceled drag, and repeated
  successful gesture. These cases measure fan-out mechanism growth, not a real
  geographical fixture.

`controllerElapsedMs` spans the public call, queue, worker, and owner-thread
delivery. `productionEvents[].durationMs` is the actual instrumentation event;
its production scope must be read separately. Serialization, observation capture,
and diagnostics reporting occur after the elapsed measurement. Cache snapshots
are actual `renderQuality().resourceCaches` values. Missing fields stay absent or
null. This probe does not initialize a QQuickWindow or observe presentation, GPU
fences, or VRAM. No nearest `frameSwapped` is substituted for editing latency.

QJSEngine initialization count/time, pinned script load, QJS conversion, and
internal cutter time are explicitly null: current public events cover a whole
worker operation. Measuring those separately requires narrow hooks in
`app/cutgeometrycalculator.cpp` around engine construction, pinned script load,
geometry conversion, and actual call; this diagnostic never estimates them by
subtracting other timings.

Root-owned build integration:

```cmake
add_executable(native_editing_performance_probe tests/native_editing_performance_probe.cpp)
target_include_directories(native_editing_performance_probe PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(native_editing_performance_probe PRIVATE pandoeditor_editor)
qt_add_resources(native_editing_performance_probe "sample"
    PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}" FILES assets/sample.pando.json)
```

Build and execution belong to the root's serialized native phase. Use the existing
Release binary and installed Qt; no runtime copying, packaging, or portable output:

```powershell
& $ProbeBinary tools/native-editing-performance-diagnostic.json --validate-only
./tools/run-native-editing-performance.ps1 -ProbeBinary $ProbeBinary -QtBinDirectory $QtBinDirectory -OutputDirectory $EvidenceDirectory
```

Focused negative checks before measurement: use a separate manifest with an
incorrect pin hash (expect native exit 2, `FIXTURE_HASH_MISMATCH`, no case rows), a
missing asset (exit 2, `INPUT_MISSING`), or a wrong fixed source SHA (exit 2,
`SOURCE_PIN_INVALID`). All 14 attempted cases retain their outcome; any case
failure makes the native exit code 1. Each completed controller stage is streamed
immediately as JSONL so later failures cannot erase earlier timing evidence.
The runner refuses timeout, null exit code, nonzero exit, wrong final schema,
missing cases, or a nonzero failure count. Keep both stdout and stderr.

The input builder reads existing fixed fixtures and only writes this new explicit
diagnostic manifest. Running it is input preparation, not native execution proof.
The input manifest itself and the binary are hashed in the runner receipt.
