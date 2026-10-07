# Fixed-source Windows measurement and exchange checkpoint

This is an executed checkpoint, not completed P1–P8/M9.8 acceptance.
Performance assessment is **FAIL**; authentic full-data acceptance is **BLOCKED**.
The subsequent report-only commit does not change the tested implementation.

```
WEB_CANDIDATE_SHA: ebcfae4d27b29cbbea6416a7045a4806930204be
APP_BASE_SHA: 2e65cf9f4a1d29e01d406029de60c5c9ff0b6eb5
APP_CANDIDATE_SHA: e01f0e6f2383022017ad947eb18d1a4269e2eee3
CONTRACT_HASHES:
  App raw CRLF SHA256: 49194028cee9b18bdfe8af331e920ebec9be7df37f2137a998fc0a1791afef29
  App LF / original Web Git blob SHA256: 6faaa45917b6322d6cbb94bfa85c6eb10518a17e2400b62adea42b49f91e66cc
FIXTURE_MANIFEST:
  Web10 original nine-file exchange SHA256: 852af3bf7f59cab73a44862c91eb1d60e9db45f8e0ee561f9a265a702b93721c
  Web10 original 30-source manifest SHA256: d6a128f9f9a9ce6b81590d0c768a5f3d2911906eec537d7e1abf7b1db69aebdd
  Native diagnostic fixture SHA256: 9af0e521085c093596c4b1ab051c90fe1ceb2bc82d73ed4bdaedcafcbfb129e4
```

## Actual native executions

Windows 11 build 26200, Qt 6.8.3/MinGW 13.1, Direct3D11,
NVIDIA GTX 1650 driver 32.0.15.9186, Ryzen 5 3500, six logical processors.
Ordinary separate Release build: `D:/build/Pandoeditor-m98-measurement/ui_tests.exe`.
Installed Qt is used directly, with the existing verified WebP plugin; no runtime/package copy.
Binary SHA256: `46b039251888abdf69d226fd02c7c0d4916cb6989b33fd7c8d30d70e605f4343`.
Build log: `measurement-logger-build-65.log`. Build source was `c3ecae6994263485196204aa8feb2e6dd8a2b91d`;
the diff through the measured App SHA contains runner/doc changes only, with no
`app/core/engine/renderer/ui/tests/assets/CMakeLists.txt` changes.
Source, binary and all fixture hashes stayed fixed across the three serialized runs.
Actual map viewport was 1920×929; each used 60000-ms warmup and >=720000-ms repeat.
The Qt function watchdog and native process deadline were finite at 2400000 ms.

| Run | Native PID / exit | Qt pass/fail/skip | Scenarios | Primary inputs / all inputs | Cycles settled / attempted | Repeat span ms | Idle CPU % | Heartbeat >=500ms / max ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 5772 / 0 | 3/0/0 | 15/15 | 1268 / 6500 | 12/12 | 720726 | 0.159599 | 30 / 6383 |
| 2 | 21320 / 0 | 3/0/0 | 15/15 | 1268 / 6500 | 12/12 | 720644 | 0.134333 | 30 / 6202 |
| 3 | 3460 / 0 | 3/0/0 | 15/15 | 1268 / 6500 | 12/12 | 720641 | 0.285027 | 30 / 6266 |

Runner exit 0 means the processes completed. The production assessment exit was
**1**, status **FAIL**. Across runs it reports 318 INPUT_STATE, 3 FRAMES,
36 LONG_RUN and 3 STALL faults. No failure is removed from the aggregate.
All 36 cycles report pendingJobs=0 and settled=true; aggregate stalePublications
is **null**, not a measured zero. CPU is process time divided by all six logical
processors; driver/GPU timestamp/VRAM and independent edit-overlay presentation
are not established by these Qt synchronization/window-swap observations.

## Mismatch and minimum reproduction

The diagnostic input is the unchanged current native10 five-country application
sample, assigned to four reduced fixture roles. Its project SHA256 is
`b3323896b2e51061349cec6e66c0c587e17b1ff9f1d8f215e14b7b8d8dd7f6e1`;
input contract SHA256 is
`bba1b55f7bb882865e7ad87bc1b28a88bbf326b02ed7e7a951bfd898922bef68`.
No authentic World Standard/Dense/Editing Heavy/Large Project acceptance is claimed.

| Failure | First observation / minimum input | Contract expectation | Responsibility |
| --- | --- | --- | --- |
| INPUT_STATE | 106 unchanged inputs per run: first move of five pan scenarios, 17 clamped DEM zoom-out events, 84 repeat events. Input 1 has matched/delivered=true but stateChanged=false; zoom input 992 stays at scale 2914.7354993926874 | Required changed-state navigation inputs must actually change state and retain exact frame ownership; gesture initialization and clamped no-ops do not satisfy that requirement | App measurement workload; not a Web codec mismatch. Current analyzer correctly rejects it |
| FRAMES | Warmup frames are tagged idle before the actual idle scenario interval: 67/38/65 out-of-interval idle frame rows | Phase records must describe their actual measured interval; do not prune raw rows to manufacture a pass | App measurement phase tagging; not measured navigation p95 failure |
| STALL | 30 actual heartbeat >=500ms events in each run, all phase -1 around fixture transitions; maxima 6383/6202/6266ms. First run event 157492ms has 5936ms gap | Existing 500ms heartbeat limit requires zero events; setup transitions after warmup are currently visible in the raw contract | Observed App stall; underlying production cause not isolated, no backend rewrite or threshold relaxation |
| LONG_RUN | stalePublications=null in all 12 cycles per run | Stale publication absence needs actual instrumentation; unobserved is not zero | App aggregate instrumentation remains incomplete; domain stale completions are separate |

All 19500 inputs have delivered/matched render-state receipts, but 318 are
unchanged-state events and are retained as failures. This is not a claim of
19500 valid changed-state navigation measurements. The recorded 1268 primary
inputs per run match the original scenario event count; mandatory semantics
still fail. Detailed raw states and `mismatch-diagnosis.json` retain the distinction.

## Recorded distributions and resource limits

These are diagnostic raw distributions with the above eligibility faults.
Idle frame intervals include the phase-tagging defect and are not a navigation
latency verdict. Other per-scenario navigation p95 values are below 33.3ms;
input p95/max values are below 100/500ms in this reduced workload. Those numbers
do not override the INPUT_STATE/STALL/LONG_RUN failures.

| Scenario | Input median / p95 / p99 / max ms | Frame median / p95 / p99 / max ms |
| --- | --- | --- |
| idle | null / null / null / null | 16.6764 / 683.1786 / 8241.2851 / 8281.6340 |
| flat-pan | 15.7989 / 16.1638 / 32.0505 / 32.3677 | 16.6688 / 16.9028 / 17.3858 / 33.0935 |
| flat-zoom | 15.8621 / 16.1189 / 16.2686 / 16.3743 | 16.6639 / 16.8541 / 156.3860 / 179.2000 |
| globe-rotation | 15.8571 / 16.2253 / 25.8558 / 26.8540 | 16.6580 / 16.8837 / 16.9789 / 26.3127 |
| globe-zoom | 15.8955 / 16.2001 / 25.9587 / 26.3131 | 16.6617 / 16.8690 / 166.5875 / 167.9705 |
| projection-switch | 18.7637 / 29.0976 / 31.1914 / 31.1914 | 16.6922 / 22.6179 / 26.3703 / 31.0372 |
| hover | 15.9304 / 16.7648 / 32.5372 / 32.6540 | 16.6093 / 17.1489 / 17.3598 / 17.5282 |
| selection | 32.5563 / 32.8123 / 34.1539 / 34.1539 | 16.1843 / 18.6304 / 21.8524 / 32.5855 |
| dense-labels | 15.8634 / 16.2337 / 24.0695 / 32.0434 | 16.6668 / 16.9408 / 20.9440 / 166.8696 |
| hydro-dense | 15.8739 / 16.1779 / 16.2786 / 28.5547 | 16.6626 / 16.8771 / 16.9996 / 20.9128 |
| dem-zoom-in | 15.8203 / 16.1559 / 16.2777 / 16.5400 | 16.6535 / 16.8865 / 174.2970 / 176.9137 |
| dem-zoom-out | 15.9173 / 16.2443 / 16.4662 / 19.3506 | 16.6610 / 16.8922 / 186.3800 / 187.1083 |
| combined-pan | 15.8039 / 16.2865 / 26.3598 / 32.3674 | 16.6632 / 16.9854 / 24.2044 / 33.2781 |
| menu-panel | 15.9923 / 16.5105 / 20.2286 / 20.2286 | 16.6615 / 19.9701 / 21.1948 / 21.1948 |
| large-navigation | 15.8401 / 16.2223 / 27.9634 / 32.4813 | 16.6546 / 16.9526 / 27.8858 / 33.2688 |

All three runs have the same settled cache values across all 12 cycles:
geometry 161524 bytes (budget 201326592), labels 2552 bytes (compatibility
working-set budget 2552), place 0 (budget 25165824), hydro 0 (budget 100663296),
terrain 0 (no prepared provider in this reduced sample). This is a measured
plateau for the reduced sample, not a populated terrain/place/hydro plateau.
The label budget is explicitly a compatibility working set, not newly approved.
CPU/QSG ledgers do not certify driver VRAM. `acceptedBy` remains null.
Separate prepared DEM evidence and the 1282-file inventory remain in
`terrain-integration-validation.md`; this diagnostic does not replace them.

## Functional / structural / device gate separation

| Gate | Actual evidence | Current judgment |
| --- | --- | --- |
| A: production exchange | `web10-current-e01f0e6-08`, Node exit 0; 16/16 cases, mismatch/fail/skip 0; web-app-web 6/6, app-web-app 6/6, expected rejection 4/4; 24/24 traces and 28/28 actual native processes | PASS for the fixed pair and original corpus; intermediate raw/reader/prepared outputs compared |
| A: M32 | `m32-current-e01f0e6-07`, actual PID 21944/exit 0; expected/generated/processed 2525/2525/2525; mismatch 0; root executable exists and ran | PASS for the separate historical property pin; not current-Web exchange evidence |
| A: atomicity/native-only fields | `timeline-storage-current-6afe93d-03` PID 22396/exit 0/74 checks; `timeline-focused-current-6afe93d-04` PID 13776/exit 0/26 Qt rows | Prior unchanged-production-source checkpoint: native defaults retained, unsupported Web export refused, full archive/history/scene state atomic; common fixtures lack populated inline flag/content rows |
| A: actual pointer | `native-geometry-registered-counted-07` actual PID 21960/exit 0, CTest exit 0, 3 Qt pass, expected/processed 1/1 | PASS: real D3D11 selection, changed draft, preview/cancel, document/dirty/Undo/Redo preservation; registration-only logging change |
| B: terrain lifecycle/device regressions | `retained-terrain-owner-prefetch-full-05` PID 5848/exit 0, 8 Qt pass/0 fail/skip; new internal checks 8/8; `retained-terrain-device-mutations-03` compiled actual device regressions caught 2/2 | PASS scoped synthetic pressure/source/mask/window mechanisms; earlier surviving mutant retained as FAIL |
| B: structure/parser | Current checkpoint 15 native structure Qt rows and 16 Node parser cases; five actual compiled metrics mutants caught by named native failures | Selected execution evidence; original raw checkpoints retained, no broad CTest pass inferred |
| Discovery | `final-discovery-checkpoint-02.json`, CTest exit 0, 206 registered, required terrain/place/catalog/structure/timeline/native pointer gates present | Discovery only. It is not 206 executed tests; whole CTest is NOT RUN |
| C: Windows | Three completed ordinary Release diagnostic executions, 45/45 scenarios, 36 cycles, native exits 0; assessment exit 1 | FAIL diagnostic eligibility/performance; full-data acceptance BLOCKED |
| C: Android | No physical Android run; earlier device inventory had no connected physical device | NOT RUN; not certified by desktop/mobile QML layout tests |
| Independent final review | Earlier agents' reviews precede later root changes | NOT RUN for subsequent changes; no new agents spawned |

## Reproduction and provenance

Evidence root: `D:/Codex/evidence/p1-p8-m98-20261006/m98-device-sample-e01f0e6-09`.
Raw per-run `measurement.json`, `measurement.json.jsonl`, `qt-test.txt`,
`process.json`, stdout/stderr, environment, run-results, assessment, receipt
summary and mismatch diagnosis are retained. Original failed `...c3ecae6-08`
watchdog run and interrupted run 2 are retained. No expected/fixture content is regenerated.

```powershell
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/run-native-performance.ps1 `
  -ProbeExecutable D:/build/Pandoeditor-m98-measurement/ui_tests.exe `
  -QtBin C:/Users/taeeu/Qt/6.8.3/mingw_64/bin `
  -AdditionalQtPluginDirectory D:/Codex/deps/qt-imageformats-6.8.3/6.8.3/mingw_64/plugins `
  -OutputDirectory <new-evidence-directory> `
  -FixtureManifest D:/Codex/evidence/p1-p8-m98-20261006/native-performance-fixture-sample-native10-pointer-04/diagnostic-fixtures.json `
  -Mode diagnostic -DiagnosticReason 'Reduced five-country native10 sample; empty-v1 places; full acceptance blocked' `
  -Runs 3 -WarmupMs 60000 -RepeatMs 720000 -TimeoutSeconds 2400
```

Raw report SHA256, run 1/2/3:
- Run 1: `0803060dfd583aa5b026e4506c99fa3156e688778022bfa273501ed8a19318b9`
- Run 2: `a5d21725198ba4a5fe790360d56755e2657ff31c21e9421a31eb903618d75d7e`
- Run 3: `ab1847bdf7aa4aad90e67f1f76614892616d153a789c4bd2dacc80a547958ce2`

The only changes in this final continuation are native Qt result logging and
bounded long-run watchdog setup plus executed reporting. Schemas/contracts,
Web code, original source blobs, original fixtures/expected and historical
M9.7 oracle pins are unchanged. Regional edit pixel/camera corrections,
retained owner device regressions and prior implementation checkpoints are
recorded separately in the follow-up history.

Remaining: authentic populated places/four full fixtures; workload no-op and
idle phase tagging corrections; fixture-transition stalls; aggregate stale
publication observation; cap/join/AA visual closure and final independent review.
No main merge, deployment, tag, release, installer or portable artifact.
