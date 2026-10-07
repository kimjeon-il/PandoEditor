# Platform boundaries source deployment

2026-10-07 deployment candidate. App implementation: `2493b3ba1e8206af4afcf5dc3bb20e9294206607`.
Web deployment: `0c55e8e4e8bacb3b125091c9b3a12ff535b5bdb6`.
The final App release tag includes this record; production code is identical to
the implementation commit. Exact release HEAD and live Web asset hashes are
recorded separately in local deployment evidence and GitHub release metadata.

Web main advanced by twelve country-lineage/schema-10 commits after the original
refactor baseline. Merge `d9cdd0d9576b` retains those accepted changes and the
platform-boundary implementation without a production merge conflict. The current
schema remains 10; the schema-9 baseline in the original validation ledger describes
that earlier source pair, not the deployment's format. Original oracle/fixture pins
remain immutable. Deployment build metadata was regenerated from its source.

Integration checks: focused storage/history/model unit invocation 40/40; lineage,
preservation and territorial unit invocation 25/25 (the invocations overlap).
Python architecture boundary checks 13/13; runtime imports 305 modules with no
circular dependency; build/version metadata consistent. Focused production browser
selection and schema-10 save/reopen/recovery scenarios passed 4/4 (zero failed or
skipped, exit 0). Current Web/native selection comparison is 14/14, zero
mismatches; pinned committed and working fixture/contract hashes match.

App product code was not changed during deployment. Its relevant native/controller
and Windows Qt input evidence is in platform-boundaries-validation.md. Mobile
chooser activation remains a known baseline failure. Existing P1–P8/M9.8 incomplete
performance acceptance, prior diagnostic failures and unavailable full-input/device
coverage retain their previous status. Deployment does not establish new full
Web/App feature parity or a new hardware/performance acceptance.

Release: `v0.1.0-platform-boundaries-preview.1`, source-only prerelease. GitHub source
ZIP/tar.gz is sufficient; no executable, installer or portable asset is created.
Neither earlier release assets nor historical source pins are replaced.
