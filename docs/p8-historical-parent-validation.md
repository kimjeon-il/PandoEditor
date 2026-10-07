# P8 historical ownership parent choices

Fixed Web: `ebcfae4d27b29cbbea6416a7045a4806930204be`.
App predecessor: `691ca18a621d63723cf2002e15bb35f234e8087b`.
Evidence: `D:/Codex/evidence/p1-p8-m98-20261006`.

Additional review found that the live QML ownership selector calls
`historicalParents(countryId)`, which listed the General country and direct
children only. For `A → Z → X`, the native adapter could accept X but the UI
could not offer it. Fixed Web `library-ownership.js` recursively lists all
General descendants in Korean name/ID order, with depth indentation.
This is an App defect, not a contract or fixture defect.

`parent-web-oracle-128` extracted seven exact original Git blobs into a fresh
external directory without editing the Web checkout. Its original eight
ownership unit tests executed with Node exit0. The actual production parent
choice function also executed five root queries against the same deliberately
reordered nested input as the native regression: expected IDs `A,D,Z,X,B`,
labels `Root,　가,　가,　　Nested,　나`; descendant Z, Regional R, missing and empty
root queries return empty. Source SHA256/blob IDs and original bytes remain in
its source manifest; shared expected files were not regenerated.

Native RED `parent-depth-red-129`: PID10084, OS exit1, Qt2pass/1fail/0skip;
the actual list has4 entries rather than the five required by fixed Web.
Minimum input stores units in `X,B,C,Z,A,D,R` order, relations X→Z→A and
B/D→A, plus separate C/R roots. The first divergence is the parent-choice
query, before any import, project mutation or file exchange.

The minimal fix follows static timeline parent records from a valid General
root, sorts siblings by Korean name and ID, uses a visited set and emits one
U+3000 indent per level. IDs remain separate from labels. This read-only query
changes no stored parent/coverage/archive/name/precision data.

Native GREEN `parent-depth-green-130`: PID2284, OS exit0, Qt30pass/0fail/0skip
including the original catalog suite and new regression. The new query checks
the fixed ordering/depth, invalid root exclusion and complete document bytes,
dirty and Undo/Redo availability unchanged. It executed on the predecessor
HEAD plus the two uncommitted scope-owned source/test changes; it is not
relabeled as a clean final-commit execution. Final source reruns follow in the
final report.

The existing reviewer independently inspected this root-authored fix and test
without editing/building/running native processes, and found no additional
issue. Their earlier authorship of other catalog code is not presented as a
new independent review of that original implementation. Schemas, contract,
production catalog data, old fixture/source pins and Web code remain unchanged.
The three completed predecessor691 device diagnostics remain their own FAIL
evidence and cannot certify this subsequent App source change.
