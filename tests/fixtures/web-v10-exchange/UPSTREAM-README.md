# Web/App v10 exchange fixtures

These are fixed web production-codec fixtures for the app import/export task.
Pinned execution evidence and remaining app discrepancies are recorded in
`docs/timeline-persistence.md`; a web-only pass does not establish app parity.

| File | Purpose |
| --- | --- |
| static.json / static.gpkg | Four statically editable identities, records and complete archive |
| complex.json / complex.gpkg | The same identities with changing shapes, parents and discontinuous lifetimes |
| calendar-boundaries.json / calendar-boundaries.gpkg | Extended/BCE leap years and February transitions in 1900/2000 |
| *.expected.json | Exact identities, timeline records, geometry archive and nested metadata expected after exchange |

All files use project schemaVersion **10**, territorial identity **6**, and timelineRecords schemaVersion **1**.
Territorial Feature.geometry is null. A/B/C are general; R is regional.
Names, 서울 capital, sourceEntityId/sourceGeometryVersion and nested
source/originalName metadata must remain unchanged. No sourceLibraryId alias is read.
No sovereignty is inferred from hierarchy.

The archive has four entries: shape v1 (holes, islands and dateline coordinates),
shape v2, an unbound Point and an unbound LineString. Static objects share shape v1;
shape v2 must survive despite being unreferenced. Static GeoPackage spatial layers
are derived. Complex GeoPackage has no spatial layers; its embedded project JSON
contains the entire authoritative document.

In the complex file A uses shape v1 through 1914-06 and v2 from 1914-07.
B has lifetimes -0001 through 0001 (BCE/CE, no year zero) and 1910-01-02 through
1920-03. In its modern lifetime B belongs to A through 1915 and C from 1916;
coverage changes from partition to explicit. Year, month and day precision must
be preserved exactly. Opening this file in the current web editor must fail with
TIMELINE_ACTIVATION before changing the active project.

The calendar fixture keeps the same four identities and complete archive. Each
identity has one bounded February–March lifetime and an explicit root parent:
A in +12000, B in 1900, C in -0400, R in 2000. A/C/R switch from shape v1 to v2
on February 29; B switches on March 1 because 1900 is not leap. The old binding
ends on February 28 inclusively. Month endpoints stay month precision and day
endpoints stay day precision. The calendar fixture also rejects UI activation.
The temporal expectations are literal values derived from the existing contract,
independent of the serializer. Identities/archive reuse the pinned static oracle.
Do not regenerate expected files from codec output.

Reproduce from the Web repository root with Node and the bundled sql.js WASM:

```powershell
node tests/fixtures/generate-timeline-exchange.mjs calendar-boundaries
node --test tests/unit/timeline-geopackage.test.mjs
```

JSON content has a fixed savedAt. Binary GeoPackage housekeeping timestamps may
vary when regenerated; compare the embedded project semantics with expected.json.
Explicit case arguments regenerate only those cases; no arguments regenerate all
three inputs. The generator never writes `*.expected.json`.

For real native QFile/web decode, native v10 encode/decode, app encodeWeb and web
Worker reread against the same independent oracles:

```powershell
node tools/check-timeline-exchange.mjs D:/build/Pandoeditor-country-lineage/timeline_project_tests.exe test-results/timeline-exchange-native
```

The Qt/MinGW runtime directories must be on PATH. This uses the existing app
`timeline_project_tests` probe, not a replacement codec. It reports every negative
case and exits nonzero for any rejected-input category mismatch or output
publication. Year-zero and nonleap-day inputs must report TIMELINE_INTERVAL;
inclusive overlap must report TIMELINE_OVERLAP and missing leap day TIMELINE_GAP.
The current Web/App candidate pair and actual results are pinned in the validation
manifest described by docs/timeline-persistence.md; old reference results are not
a substitute for executing the fixed candidates.
The existing app record/storage/exchange parity tools remain additional checks.
