# P5–P7 fixed-source validation

Fixed Web: `ebcfae4d27b29cbbea6416a7045a4806930204be`.
App base: `2e65cf9f4a1d29e01d406029de60c5c9ff0b6eb5`.
This is an in-progress execution record. The final app candidate is not yet
fixed; these dirty-tree results do not establish final-commit acceptance.

## P5 source audit

Actual command (Python 3.12.10, Windows 11):

```powershell
python tools/audit-place-review.py --web-repo 'D:/dev/Pandoeditor(Web)' --report 'D:/Codex/evidence/p1-p8-m98-20261006/p5-review-audit-01.json'
```

Exit 0. The original pinned Web generator's `apply_review_overrides` processed
exactly 64 non-coordinate overrides: 17 retain and 47 exclude, 0 failed,
0 skipped. All 64 original expected source records and coordinates remained
unchanged. The separate 35 coordinate-correction overrides were excluded from
this execution; no Romanian review or coordinate correction was applied.

The report retains every selected ID, original override, applied reason,
expected source record, original Git-blob SHA-256 and generation path. Original
Web files were read using `git show <fixed SHA>:<path>`; none were changed.

The pinned summary is stale: its recorded ledger hash does not match the
original ledger blob, and **0/64** summary rows include the selected override
action and appended reason (**64 discrepancies**). This is a source-report
discrepancy, not permission to regenerate expected values or runtime data.

Production Web and bundled native manifests are byte-identical, 108 bytes,
SHA-256 `148660b93529a60640f6a340117f976a7e2822ab8f34fa1814023920fc82f4b7`,
revision `empty-v1`. Actual production-data application/display and full-data
performance acceptance remain **BLOCKED**. The offline refined-candidate
summary does not establish deployed place data.

## P6/P7 execution boundaries

Synthetic PLAC cases exercise the actual native store/provider, label engine,
selection/copy commands and production codecs. They establish mechanisms;
they do not establish actual city-data display acceptance. Font geometry,
unsupported observations, source-report discrepancies and semantic mismatches
are recorded separately. Final native counts and fixed-commit comparison
receipts will be added after the current implementation and review are frozen.

Initial controller execution exposed a missing `placeBuiltin` selection domain.
The domain correction and explicit editable-copy path have been re-executed:
`post-integration09-place_controller_tests-01` observed 10 passing Qt rows, no failures
or skips and native exit 0. This is synthetic mechanism evidence in the dirty
implementation tree, not a final source-commit or populated-data gate.

The latest native/Web comparison (`place-density-cross-comparison-04.json`)
processed 42 cases with zero semantic mismatches: the previous 39 cases plus
three application quality-prefilter cases. The actual native probe PID was
8668, exit 0. Original pinned Web application prefilter source and production
layout code were executed with synthetic inputs. Reduced/full density,
selected/pinned protection and stable source order now agree in these cases.
Four font geometry differences remain explicit. The original fixture bytes
and old case expectations were preserved. Final real-data visual acceptance
remains BLOCKED; this comparison must not be described as full pixel parity.
