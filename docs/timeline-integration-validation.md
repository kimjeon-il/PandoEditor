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
