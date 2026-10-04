# M9.7 editing parity contract and first-stage evidence

## Authoritative source

The behavioral oracle is **kimjeon-il/Pando@53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47**. GitHub repository ID 1335531825 was previously named `kimjeon-il/world-map`. The native baseline is PandoEditor@54aa51d11e38bf17b89f83919bf8443aba32f783.

The older b2c7bb991bb793a06d5cbdc44e8ae0b56a208122 reference is historical. Its unchanged geometry modules may explain provenance; its former country/subunit promotion, conversion and reference policies are **not** current expected behavior. The current production parent-detach operation retains geometry and entity identity. The user approved this updated reference before expectations were captured.

Schema 9 / territorial model 5 is the current exchange contract. Existing timeline exchange tests remain the exchange gate; this work introduces no legacy import path or schema downgrade.

## What executes

- `tools/m97/calculations.mjs` verifies original source Git blob hashes, then calls the unmodified drawn-annex preprocessing and final country command calculator in sequence. Snap calls the unmodified screen-distance resolver.
- `tools/m97/web-lifecycle.mjs` executes current production entity store, repository, service, command pipeline, project snapshots/history, territorial drafts and geometry-preview code. Geometry requests run through the actual Worker client and Worker entrypoint. The Node transport provides browser Worker messaging, not a replacement geometry implementation.
- All vendored files retain their original bytes. Manifests identify the original repository-relative paths and pinned Git blob IDs. No application algorithm is rewritten in a test helper.
- `tests/fixtures/web-m97/calculation-expected.json` is the captured output of those production functions at the pinned commit, not an independently coded expected-geometry implementation. `corpus.test.mjs` replays and checks it offline.
- The existing `m4_geometry_probe` remains the native production command path. `native-baseline.mjs` adapts only the input/observable output shape. Unsupported operations and missing lifecycle/reference observations stay explicitly unobserved.

## Observation contract

A lifecycle case records before, preview, cancel, confirm, Undo and Redo independently. Each stage declares whether it was observed and records its actual document, presentation, history and pending-preview state. A synchronous parent change has no staged preview/cancel; those observations are unavailable, not invented successful no-ops.

Compare entity identities and parent relations, effective geometry, created/deleted IDs, retained-reference targets and order, and actual history results. Preserve raw timeline identity and geometry-version evidence. Runtime-generated IDs may be made deterministic only through the production ID-provider port; never rewrite expected IDs to fit the app.

The generic observation comparator requires complete observed stages and a nonempty declared state shape. Its normalized profile requires objects, retained references, selection and integer history counts; its web-v9 profile requires entity/identity collections, timeline relation tables, geometry versions, distribution collections, presentation maps and pending-preview state. Missing evidence returns `incomplete` even if a baseline lists the same missing values. Known differences require exact difference paths **and values**. Unexpected agreement invalidates an obsolete known-difference baseline.

The calculation adapter compares exact geometry XOR, object identity and parent relation. Ring winding/start positions can differ without changing the covered geometry. It has no pixel tolerance or minimum-area forgiveness. A matching rejection alone does not establish matching error semantics.

## Important current-web behaviors

- Drawn annex computes selection intersected with donor territory, excluding existing target territory. The raw final command has additional source-containment validation; passing an oversized polygon directly to that command is not equivalent to the selection workflow.
- Reference cleanup occurs in the application commit path above the geometry kernel. A kernel-only result cannot prove distribution, visibility, style or label-reference behavior.
- Current Undo snapshots preserve canonical entities, timeline records and geometry versions. They exclude general presentation settings. Therefore Undo is not specified as restoring every presentation map to its original contents; the actual production result is authoritative.
- The M33 structure helper is not a production parity oracle. The legacy M4 manually constructed promotion/conversion expectations are not evidence for the updated lifecycle contract.

## Scope and remaining gates

Stage 1 establishes executable production expectations, deterministic input corpus, provenance and comparison infrastructure. It does **not** make all app editing behavior equal to the web. The native baseline collector always records `parityComplete: false`; its process success means the observation was collected, not that mismatches passed a parity gate.

The calculation corpus includes donor-contained and oversized selections, target overlap, no overlap, complete donor removal, holes, MultiPolygons, multiple donors, raw-command rejection, mouse/touch snap thresholds, nearest edge/intersection and ties, merge deletion and new-country creation. A separate real-Worker cut corpus records two/six crossings, a hole, multiple components, the date line and a non-splitting outer-to-hole bridge. These are calculation observations, not native split parity. The lifecycle corpus exercises current parent detach and staged sibling merge, full annex and creation with history and reference effects.

Remaining implementation order is unchanged:

1. M9.7.2: unified selection sessions and annex input/output behavior.
2. M9.7.3: multi-crossing, multi-component, hole and date-line splitting.
3. M9.7.4: spatial-index candidate generation, screen-distance snap and multiple shared-boundary owners.
4. M9.7.5: existing schema 9 exchange exercised after editing, including representability rejections.
5. M9.7.6: extract proven-equal calculations into the editing engine.
6. M9.7.7: all required native lifecycle/ref observations connected and differences removed before full parity acceptance.

UI hit testing/rendering, pointer transport, actual GPU performance and broad river-selection workflows are not established by the headless lifecycle host. Source-version changes require deliberate source-manifest and corpus review; expected data must never be regenerated merely to hide an app mismatch.

## Run

    node --test tools/m97/*.test.mjs
    node tools/m97/web-lifecycle.mjs > web-lifecycle-observations.json
    node tools/m97/native-baseline.mjs build/app/m4_geometry_probe native-baseline.json

The dedicated branch CI builds the application, runs the complete available native regression with a skip audit, and saves the native differences as evidence. Node source verification and production-oracle tests need no network after checkout.
