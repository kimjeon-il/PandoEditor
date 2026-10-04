# T2-1 timeline record model implementation plan

> **For agentic workers:** Use executing-plans in the existing timeline branches.

**Goal:** Implement the shared timeline record value and strict validation in JS and C++.
**Architecture:** Pure record normalization using existing temporal functions and an explicit
read-only entity/geometry context. No new application state owner or live schema activation.
**Tech Stack:** JavaScript ES modules / node:test; C++17 / existing GeometryRef and Validity.
**Spec:** `docs/timeline-records.md`.

## Global constraints

Preserve exact day/month/year precision, inclusive endpoints, null open bounds,
logical IDs, and general/regional semantics. No monthly snapshots, event replay,
legacy migrations, source-coordinate copying, renderer mutation, or main-branch changes.

## Review focus

Single-day gaps/cycles not visible at month end; simultaneous inclusive transitions;
BCE/CE and leap-day adjacency; input mutation after failed validation; misleading
claims of live persistence integration while only the model is implemented.

### 1. Shared executable cases and JS model

Files: `tests/fixtures/timeline-records.json`, `tests/fixtures/timeline-records-cases.mjs`,
`tests/unit/timeline-records.test.mjs`, `assets/js/modules/timeline-records.js`.

- [ ] Create common fixtures covering every error and temporal edge above.
- [ ] Run tests against the minimal nonvalidating API and observe assertion failures.
- [ ] Implement `normalizeTimelineRecords(input, context)` without modifying temporal semantics.
- [ ] Verify rejection, immutable input, frozen output, precision retention, and record JSON round trip.

### 2. Native model and same-fixture tests

Files: `core/include/pandoeditor/timeline-records.h`, `core/src/timeline-records.cpp`,
`tests/timeline_records_tests.cpp`, `tools/generate-timeline-records-tests.mjs`,
`tools/timeline-records-parity.mjs`, `core/CMakeLists.txt`.

- [ ] Define typed records reusing GeometryRef/Validity and explicit context dependencies.
- [ ] Generate native inputs from the exact shared JSON corpus; test-only generation,
  no new product JSON parser or bundled alternate temporal implementation.
- [ ] Observe rejection tests fail against a nonvalidating native implementation.
- [ ] Implement the same boundary-sweep validation, keeping JSON decoding out of this step.
- [ ] Register source and generated-fixture tests with the existing CMake core target.
- [ ] Run focused native tests and UBSan; compare actual native verdicts to actual JS verdicts.

### 3. Verification and publication

- [ ] Self-review schema, ownership, date boundary handling, and build registration.
- [ ] Rerun focused suites, source syntax, fixture-generation consistency, and diff whitespace.
- [ ] Publish only to `feat/timeline-web` and `feat/timeline-app` without force updates.
- [ ] Verify remote heads and blob hashes; report T2-1 complete and T2-2 integration pending.

## Execution ruling

T2 is split into T2-1 (this model) and T2-2 (live persistence/ownership integration).
The current full project models differ. Activating only part of a schema transition
would risk silently losing records; model validation must not be reported as live
save/restore support. Repository access is through GitHub; the local verification
workspace contains exact fetched dependencies, not a complete application checkout.
