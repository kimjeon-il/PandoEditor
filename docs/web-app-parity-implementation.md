# Web / App parity implementation ledger

Approved specification: the 2026-10-08 user-supplied six-stage implementation plan.
Baseline Web: 582908e9645b79b212f7db5105e596c9986a6ee1.
Baseline App: faa4ced0824dd8eb05484365590dc129e8e64b2a (new reference-image work is preserved).

Ruling: use dedicated codex/web-app-parity branches in the existing checkouts,
preserving the earlier checkout workflow and installed build dependencies.
The three existing German geometry CI changes in Web are not part of this work.
No production behavior, geometry, formats, timing, or history semantics may change.

## Progress

- Baseline selection corpus: 1 Web test passed, 14 ordered observations.
- Inventory: existing final-oracle runner lists historical suites, including
  missing targets; these cannot be relabeled as current behavioral parity.
- Implementation: in progress. No overall parity or completion claim.

## Implemented evidence framework

- Canonical Web registry: 23 domains, existing unit/browser/native tests and
  the 25 previous final-oracle entries retained as historical evidence.
- Strict three-way comparator, opt-in exact geometry representation handling,
  impact selection, expected observation IDs, generated JSON/Markdown matrix.
- Current paired adapters: selection (14), command/history (10), timeline
  records (60 native + 8 unsupported), timeline storage (25 + 6 unsupported).
- Added current properties (14), structure (21), containment (14), clipping (10),
  controlled async lifecycle (5), catalog queries (11), file exchange (11),
  catalog loading (4), and distribution scale calculations (7). Focused comparisons pass; loading's supporting
  physical-data test retains the baseline newline failure.
- Full content exchange checks all project fields; only explicitly identified
  record/archive collections are rekeyed. Reserved object IDs have negative tests.
- Contract descriptions are domain-specific. Provider asset paths participate in
  impact selection; N/A requires explicit justification. Distribution/place UI
  gaps remain NOT_RUN, not N/A.
- Native source/binary receipts; CTest discovery is checked against the receipt.
- Approved-registry report accounting, separate regression/behavioral gates,
  existing release packaging/device/performance conditions retained.
- Both repositories have PR/main/manual-pair workflow definitions. These have
  not been executed on GitHub; their presence is not CI success evidence.

## Baseline observations (local dirty candidate, not official evidence)

The first all-domain model run executed 125 supporting/historical checks and
produced 189 rows: 148 PASS, 4 FAIL, 14 UNSUPPORTED, 23 NOT_RUN, 0 ERROR.
PASS rows include supporting tests and must not be read as 148 parity scenarios.
The report is `D:/Codex/evidence/web-app-parity-20261008/baseline.json`.

The later all-registered model run is `registered-current.json` in that same
evidence directory: 231 PASS, 2 FAIL, 2 KNOWN_DIFFERENCE, 14 UNSUPPORTED,
23 NOT_RUN, 0 ERROR (272 rows). It predates the final batch/distribution additions.
Their focused `batch-distribution.json` run has 31 PASS, 1 NOT_RUN, no failures
or execution errors. Seven batch cases exercise invalid-reference atomicity,
no-op and one-unit Undo/Redo; seven distribution cases exercise range/alpha
calculations only. Neither establishes complete editing or display parity.

Two compared history observations differ only at `/dirty`: Undo and Redo to
the saved document leave Web dirty and App clean. Exact known-difference entries
record this reviewed baseline; product code was not changed.

Three native supporting checks failed: terrain resource cache, terrain DEM
provider and physical data store. The Qt installation lacks the WebP writer;
the physical-data failure observes 2114 bytes where its fixed metadata test
expects 2024 (working checkout line endings). New CI installs qtimageformats and
disables checkout newline conversion; local failure evidence is retained.

## Remaining implementation, not claimed complete

- Paired current adapters/fixtures for complete editing sessions, object-specific
  content mutations, autosave recovery/failure, provider cancellation and matched
  UI/render flows. Existing supporting suites do not replace these observations.
- Candidate commit pair verification and an explicitly reviewed source-pin
  update. The previous approved pin has deliberately not been auto-rewritten.
- Clean-checkout official runs, Windows actual UI runs, and GitHub CI execution.

Windows selection UI attempt: Web Playwright 1/1 passed; the initial Qt run
exceeded the original 180-second process timeout and remains recorded as ERROR.
The UI budget is now 600 seconds and each invocation has fresh file logs.
A direct diagnostic rerun of the same built selection_ui_tests finished in
378087 ms with 16 passed, 2 failed, 0 skipped (exit 1). Both failures were the
mobile-360 chooser visibility assertion in overlapChooserAndMapModifiers
(line 225) and chooserCancelAndStaleResults (line 318). Evidence:
`D:/Codex/evidence/web-app-parity-20261008/selection-ui-full.txt`.
This direct rerun is separate diagnostic evidence, not a replacement for the
fingerprinted integrated report or proof of a product root cause. No UI/product
fix was made. Android physical-device validation was not performed.

Framework negative tests currently cover exact field mismatch, same wrong
answer, missing/null/order, geometry/hole changes, known-difference expansion,
fixture/source/binary drift, wrong CTest executable and deleted report cases.

## Task boundaries

1. Common registry and baseline inventory.
2. Strict comparison, provenance, impact selection, matrix and gate integration.
3. Current model/selection/command/history observations.
4. Editing/geometry/async observations and historical evidence separation.
5. Storage/timeline/assets/loading evidence.
6. UI/render evidence, CI wiring, final validation and independent review.

Dependencies: every adapter consumes a registry case and emits observations;
the runner records evidence without inventing missing observations; the gate
requires complete case accounting and verified source/build identity.

## Publication candidate (2026-10-08)

Web incorporated remote main dd9d585 before publication. The merged candidate
1c47399 re-ran the 22 parity unit tests: 21 pass, 1 fail. The catalog query
contract differs for korean-lineage, korean-name and germany-1900 after the
upstream date-aware library changes. Original expectations and production
behavior remain unchanged. Earlier reports describe their original source pair,
not this publication candidate. App framework tests remain 8/8 passing.
Publication does not assert complete CI or behavioral parity.
