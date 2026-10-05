# T2-2 integration validation

The integration candidate merges `feat/timeline-app@ee1405b584123b1c17fc7fb405a2bb68bf727326`
onto `563fdd623b0c44e4445a28442b4fb9c264f863c2`, preserving the published
M9.2-M9.5 implementation and the integration flag changes at `18dbc9dd`.
The existing checkouts and their ignored build/distribution outputs remain intact.

Merge adaptations keep the timeline records as the owners of lifetime, geometry
binding and administrative parent state. M9.5 scene invalidation compares these
records and identity metadata, and flag resolution reads the canonical metadata.
Native `defaultCountryId` and `defaultFlagDataUrl` remain lossless. Web export
rejects these native-only fields explicitly rather than discarding them. Existing
web flag metadata remains supported; no migration, alternate reader or political
parent interpretation was added.

The production codec test first reproduced two silent native flag-default losses.
Both now reject at the web export boundary while native file round trips retain
the values. The administrative flag fixture uses the parent argument, preserves
the General ID through Undo/Redo and file reopen, and covers default/embedded/none
policies plus an explicit reset. No tests were deleted or skipped.

## Windows scoped results

Qt 6.8.3 / MinGW GCC 13.1 x64 Release, `-O3 -DNDEBUG -UNDEBUG`, task-local LLD
20.1.8. Assertions remain enabled. Only the application and required test/probe
targets were built. Qt interaction uses offscreen/software rendering.

| Check | Result |
| --- | --- |
| Configure and application/selected-target build | Pass |
| Persistence plus changed picker/label/cache paths | 13/13 CTests, 0 failures/skips |
| Production project files inside CTest | 31 storage + 9 numeric + 2 native flag-default boundary cases, 0 failures/skips |
| Six selected Qt flag methods, including three policy data rows | 10 total Qt cases including initialization/cleanup, 0 failures/skips |
| Flag placement against pinned web algorithm | 48 cases passed |
| Pinned object/math/temporal/history oracle | 2525 comparisons passed; 30 source blobs verified |
| Actual web/native file exchange, web `788f43fd27062851bdadf7fa6964819d43534685` | 16/16, 0 failures/skips |
| Source assets | 17/17 passed |

The first source-asset check found Windows CRLF conversion in existing pinned
files. Eleven files were restored to the LF bytes required by `.gitattributes`,
and each matches its staged Git blob. Neither source contents nor expected hashes
were changed. The initial administrative fixture supplied its parent in the
sovereignty argument and was correctly rejected; that fixture argument was fixed.

Linux production results from [run 37197734241](https://github.com/kimjeon-il/PandoEditor/actions/runs/37197734241)
on `fcd7d16704c9a552b6b76464ab9070e9baa18223` were downloaded and checked:
13 CTests, 10 selected Qt flag cases, 48 flag-placement cases, 17 source assets
and 16 actual exchange cases passed, with no failed/skipped production tests.
The native file suite passed its 31 + 9 + 2 cases. That run's additional M32
probe did not execute: its executable is in `build/`, and the workflow used
`build/app/`. The pipeline hid the process error, so the overall green job is
not evidence of an M32 pass. The follow-up changes only this workflow and report:
it corrects the path, enables Bash pipefail, and exposes `probe_only` to rerun
that comparison without repeating the passed production suites. M32's Linux
result must be read from that separate run; it is not asserted here in advance.

Evidence is in `D:/Codex/releases/timeline-t22-20261004`: Windows JUnit,
`windows-LastTest.log`, flag/oracle/exchange logs and source-asset report.
The Linux manual `Timeline persistence validation` workflow uses the same 13
CTests, selected flag methods and production exchange path. Its exact commit
and results are recorded in the workflow run and downloadable evidence artifact;
this Windows report does not assert an unexecuted Linux result.

## Scope and activation limits

At the user's request, no full CTest, full hydro fetch or full regression audit
is run. The integration push uses `[skip ci]` to avoid broad automatic workflows;
only the focused timeline workflow is dispatched separately.

Only static projects with one unbounded lifetime, binding and parent record per
territorial entity activate in the existing UI. Rich timelines remain supported
for production file storage and exchange and reject before UI publication.
The native and web current project formats are version 9, with record schema 1;
older development save formats have no migration reader.

The user canceled portable packaging during this run. No new portable archive,
binary release, existing-package replacement or desktop/GPU certification is
claimed. T3/T4 and timeline UI work remain outside this integration change.

## T2-2 fixed-candidate follow-up — 2026-10-05

Executed review checkpoint (not evidence for a later changed source revision):

- `APP_BASE_SHA`: `54aa51d11e38bf17b89f83919bf8443aba32f783`, freshly checked
  against remote main; isolated branch `codex/timeline-t22-app-followup`.
- Reviewed application source: `60c1c10c54811a7ccb09afb86022b75ef6385b5a`.
- `WEB_CANDIDATE_SHA`: `7547c473e355c6656daf965e652dfe801ca70183`, detached,
  read-only verification checkout. No moving web ref was used as the oracle.
- Both contract Git blobs have SHA-256
  `6faaa45917b6322d6cbb94bfa85c6eb10518a17e2400b62adea42b49f91e66cc`.
  The Windows CRLF consumption hash is
  `49194028cee9b18bdfe8af331e920ebec9be7df37f2137a998fc0a1791afef29`;
  these are distinct byte representations of the unchanged contract.
- Original handoff manifest byte hash:
  `c81429244ad6419dbea44d87d3f23bf726a415be375937f4ec644b6442678e9c`.
  Its original filename is
  `manifest-7547c473e355c6656daf965e652dfe801ca70183.json`.
  The exact copy is `D:/Codex/evidence/timeline-t22-app-final-20261005/web-manifest.json`.
  Source Git blobs and fixture bytes were checked against this manifest;
  only text CRLF/LF checkout conversion was accepted. Binary fixtures were exact.

The nine frozen exchange files are `static`, `complex`, and
`calendar-boundaries`, each with `.json`, `.gpkg`, and `.expected.json`, under
the fixed web candidate's `tests/fixtures/timeline-exchange/`. The independent
expected files were read verbatim, never regenerated. The matrix includes
discontinuous lifetimes, year/month/date and BCE/extended years, leap boundaries,
parent/coverage changes, shared/past/unreferenced archive versions, holes,
islands and antimeridian geometry. Comparisons key only identity/record/archive
rows by ID (and archive version); nested arrays, endpoint spelling and coordinate
values remain exact. No tolerance, endpoint conversion or general array sort is
used by the final checker.

### Observed mismatch and correction

The initial fixed-web executable checker processed 10 cases: 8 passed, 2 failed,
0 skipped, exit 1. The first divergence was the app file read rejection category:

| Minimal mutation in `calendar-boundaries.json` | Web/independent expected | App before correction | Responsibility |
| --- | --- | --- | --- |
| `life:A.validFrom = "0000-02"` | `TIMELINE_INTERVAL` | `INVALID_DATE: year zero` | App |
| `B:new.validFrom = "1900-02-29"` | `TIMELINE_INTERVAL` | `INVALID_DATE: day` | App |

The codec parsed record dates before the existing typed timeline normalizer.
Only the three timeline collections now defer date validation to that normalizer.
Distribution content retains its original wire date parsing. A fresh native build
first demonstrated 24/24 category regression failures, exit 1; the corrected
build processed all 24 with zero failures/skips. Eight decoder-only blank/content
date guards also passed. No schema, contract, fixture, expected, web production
code, geometry version or activation rule changed. No migration or UI was added.

### Executed checkpoint evidence

Environment: Windows, Qt 6.8.3, MinGW GCC 13.1, Ninja Debug, Node 24,
`QT_QPA_PLATFORM=offscreen`, `QT_QUICK_BACKEND=software`.
Fresh build: `D:/Codex/builds/timeline-t22-app-final`, separate from all prior
build/package directories and original checkouts.

| Check | Actual result |
| --- | --- |
| M32 native process (`property_probe.exe`, real generated target at build root) | exit 0, 2525 expected/generated/processed, 0 mismatches, completed; PID and binary hash recorded |
| Missing M32 executable guard | exit 1, no comparisons; structured report states absent/incomplete |
| Focused CTest | 5/5, zero failures/skips: temporal, records, storage, project codec, editor persistence |
| Fixed-web record verdict parity | 60/60 |
| Fixed-web storage parity | 25/25 verdicts, 11/11 complete snapshots |
| Supplemental production content/autosave/precision exchange | 16/16, zero failures/skips |
| Reviewed fixed matrix, web→app→web | 6 expected/processed/passed, 0 failures/skips |
| Reviewed fixed matrix, app→web→app | 6 expected/processed/passed, 0 failures/skips |
| Fixed-file invalid mutations | 15/15 rejected by both, zero failures/skips |
| Exact comparator loss guards | 7/7 |

The 15 invalid mutations include eight independently specified typed record
errors and seven malformed wire types. The latter compare rejection only;
first-error priority for malformed wire types is not asserted as a shared code
contract. Discontinuous lifetimes in valid fixtures remain accepted.

The actual production web serializer and GIS Worker execute from the fixed
candidate. Native trace files expose read, native JSON save/reopen, native
GeoPackage save/reopen and production `encodeWeb` export; every completed
canonical checkpoint is compared before reporting success. A native trace status
records the failing operation so a save/export failure is not mislabeled as a
read failure. Actual native PIDs/exit codes and expected/processed direction
counts are recorded, and omitted/early/zero cases fail the checker.

`json`/`gpkg` in matrix names denote frozen input fixture/container routes.
The existing app→web production export is **encodeWeb JSON**. Web-produced
GeoPackages are read by the actual app GeoPackage reader; app native GeoPackages
are native storage/reopen containers. A production web Worker read of the native
container was separately confirmed to reject `PL-SCHEMA-MISSING`; direct native
package interoperability is outside the common export contract and is not a
passing exchange claim. Container byte equality is not canonical equality.

Native `defaultCountryId`/`defaultFlagDataUrl` preservation and explicit web-export
refusal pass separately in the project/editor suites; common exchange inputs do
not contain these fields. Failed native reads, private restoration and complex
activation retain content/archive, selection, history/future, dirty/save target,
project identity and scene publication/resource generation. Subsequent actual
Undo/Redo restores the previous/future bytes. Static editing retains logical IDs,
shared and previous archive versions; cursor moves remain view-only.

The existing workflow's executable path and Bash pipefail were already corrected.
The subsequent [M32-only Linux run 37198613496](https://github.com/kimjeon-il/PandoEditor/actions/runs/37198613496)
at `bf29925683e3ba4347c40b55495f16a29fa7e4e3` actually completed 2525 comparisons;
this is historical evidence, not a result for this candidate. The follow-up adds
structured process/count evidence and workflow assertions; it leaves the fixed
path and `probe_only` selection intact. Other feature oracles remain pinned to
their existing sources. Against the old exchange web oracle `788f43f…`, candidate
production serializer/project-state changed; temporal/record/storage/Worker
sources and contract did not. Calendar fixtures/checker are additional inputs.

Evidence root: `D:/Codex/evidence/timeline-t22-app-final-20261005`.
Reviewed matrix: `reviewed/bidirectional-cdkhwc/results.json` (app working tree
clean at the exact reviewed SHA); `reviewed-ctest.xml`, `m32-reviewed.json`, and
the retained RED/failure logs document the other executed boundaries. Fresh
whole-branch review found no remaining source blockers at `54aa51d…60c1c10`.
Final frozen-pair execution/CI evidence is kept externally so recording its SHA
does not change the commit being verified. This checkpoint does not assert an
unexecuted final run or a later Linux result. Full CTest, renderer/GPU audit,
T3/T4/UI, main merge, deployment and portable packaging were not executed.
