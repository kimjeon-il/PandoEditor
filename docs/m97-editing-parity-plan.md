# M9.7.1 implementation plan

## Scope

Build a reproducible production-source editing oracle and differential contract before changing application behavior. Reuse the M4 native command probe, pinned clipping library, existing history and retained-reference tests. Do not count the M33 helper or manually constructed promotion expectations as production parity.

## Sources and decisions

- Native baseline: PandoEditor 54aa51d11e38bf17b89f83919bf8443aba32f783.
- Dedicated branch: codex/m97-web-editing-parity.
- Historical reference only: b2c7bb991bb793a06d5cbdc44e8ae0b56a208122.
- User-approved current behavioral oracle: 53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47.
- Web repository identity: GitHub repository 1335531825, renamed from kimjeon-il/world-map to kimjeon-il/Pando.
- Schema 9 exchange is already implemented and must not be downgraded.
- Approved policy: current production parent detachment keeps geometry and object identity. The former b2 promotion subtraction is historical behavior, not a current expectation.
- Resolve any current-vs-historical behavioral difference before encoding expected identity, promotion or reference-rewrite semantics. Schema-only adapters may not manufacture production expected results.

## Ordered tasks

1. Verify source provenance and current-vs-historical executable entrypoints. Record exact blobs, dependencies and limitations.
2. Specify deterministic corpus and observation protocol: operation input, preview, cancel, confirm, generated/deleted objects, retained references, undo and redo. Separate mathematical geometry comparison from ordered identity and lifecycle observations.
3. Test-first source manifest verification and comparator, including deliberately mutated geometry/ref/history and unexpected success/failure. Pin original production source bytes without rewriting algorithms.
4. Run real web calculation entrypoints to generate baseline outcomes. Reuse the native command preparation/confirmation probe for supported inputs; report unsupported and mismatched cases explicitly, never as passing parity.
5. Add repeatable CI checks and evidence artifacts. Independently review, verify the final dedicated-branch commit and its CI. Stage 1 acceptance means oracle infrastructure is valid, not that all M9.7 behavior already matches.

## Review focus

- Expected values must come from production web functions, not a second implementation in the harness.
- Missing native observations, rejected input, and absent lifecycle coverage cannot count as parity.
- Known mismatches require narrow case/field expectations; unrelated failures and unexpected passes fail the baseline gate.
- Hash verification covers every transitive production dependency before import or VM evaluation.
- Ring starting vertex/winding normalization cannot conceal changed geometry; refs and created/deleted identities stay exact.
- Preserve native model semantics, M9.2–M9.6 changes and existing coverage. The approved M9.7.5 persistence change writes native v10 ownership provenance, retains native v9 reads, and keeps web exchange at v9; see `m975-current-model-bridge.md`.

## Boundaries

No selection, annex, split, snap, shared-boundary or Engine product changes in this stage. No main merge, release or branch deletion. A full native test pass and a web-only oracle pass are different evidence.
