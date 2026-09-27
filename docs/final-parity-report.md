# M7.7 migration release gate — open

Observed on 2026-09-26: `git ls-remote` reported
`world-map/main@c0bd31d13dc8495593d78cf51f7cc195de7c9469`.
This equals the M7.1–M7.5 pinned web commit. The M3/M4 baselines predate it;
`docs/parity-manifest.json` records every changed path and original/head blob
for the selected contract files. PandoEditor feature HEAD at the freeze was
`7841cffc53c2044387075300df567327cecac752`; its private `main` identity
could not be observed from this worktree.

| Area | Web baseline | Drift status | Qt/Oracle | Windows | Android |
|---|---|---|---|---|---|
| M3 selection, properties, structure, presentation, interaction | 58e4087f | REVIEW; changed source/UI paths | NOT RUN; M3.5 standalone web/native Oracle missing | NOT RUN | NOT RUN |
| M4 geometry | 58e4087f | REVIEW; country geometry and reference image paths changed | NOT RUN | NOT RUN | NOT RUN |
| M5 content, hydro, display | c0bd31d1 | No newer web commit | NOT RUN | NOT RUN | NOT RUN |
| M6 history and GIS | c0bd31d1 | No newer web commit | NOT RUN | NOT RUN | NOT RUN |
| M7 projection, renderer, full world, quality, edit | c0bd31d1 | No newer web commit | NOT RUN; M7.3–M7.6 gate evidence missing | NOT RUN | NOT RUN |
| Storage and cross-cutting UI/runtime | 58e4087f | REVIEW | NOT RUN | NOT RUN | NOT RUN |

`REVIEW` means changed upstream files await behavior assessment. It is not a
declaration that Qt is wrong or synchronized. A previous Oracle against its
older pinned source cannot close the current-web drift by itself. The source
classifier deliberately puts mixed application/bootstrap paths in the
`crossCutting` bucket for explicit review. Any behavior change found there
must be ported or listed as a known, approved difference before release.

## Evidence tooling

- `tools/check-upstream-drift.mjs` freezes the remote HEAD and classifies
  changes from each domain baseline. It requires a local Git checkout of
  world-map for preparation; committed `docs/parity-manifest.json` is
  self-contained.
- `tools/run-final-oracles.mjs` reports each registered CTest differential
  suite with source SHA. Missing suites are `skipped`, not passed.
- `tools/verify-release-assets.mjs` checks committed world data, corpus,
  shaders, flags and licenses in the source tree. Windows deployed folders
  and Android APK/AAB resources require separate physical package inspection.
- `tools/collect-render-metrics.mjs` reduces actual device frame samples
  without an invented frame or memory threshold.
- `tools/evaluate-release-gate.mjs` reports all blockers in JSON and cannot
  return PASS while drift review, fresh CTest, Release smoke, ASan/UBSan,
  skipped Oracles, physical device evidence, deployed package checks or
  accepted performance budgets are missing.

## Known implementation boundaries

- M7.3 GPU/CPU pixel, scene graph lifetime and physical backend evidence
  remains open. Built-in hydro can still select the CPU fallback.
- M7.4 terrain/hydro physical packages are optional and must report
  unavailable explicitly; their packaging and access are unverified.
- M7.5 progressive upload can expose partial new scenes; atomic visual
  promotion is not established.
- M7.6 large-world cross-domain edit, Undo/Redo, save/reopen and failure
  injection have not been exercised. CPU fallback may rebuild projection;
  incremental spatial publication stages the grid map.

This report does **not** declare the web-to-Qt migration complete. Full CTest,
sanitizers, final Oracles, release assets in deployed packages, measured
budgets, visible Windows and physical Android validation are NOT RUN in this
session, as requested for later Codex verification. `integration` remains
unmerged.
