# Web v9 exchange fixtures

These are web production-codec fixtures for the separate app import/export task.
The app round trip has **not** been verified by this web task.

| File | Purpose |
| --- | --- |
| static.json / static.gpkg | Four statically editable identities, records and complete archive |
| complex.json / complex.gpkg | The same identities with changing shapes, parents and discontinuous lifetimes |
| *.expected.json | Exact identities, timeline records, geometry archive and nested metadata expected after exchange |

Both files use project schemaVersion **9** and timelineRecords schemaVersion **1**.
Territorial Feature.geometry is null. A/B/C are general; R is regional.
Names, 서울 capital and nested source/originalName metadata must remain unchanged.
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

Reproduce from the repository root with Node and the bundled sql.js WASM:

```powershell
$env:NODE_OPTIONS='--experimental-vm-modules'
node tests/fixtures/generate-timeline-exchange.mjs
```

JSON content has a fixed savedAt. Binary GeoPackage housekeeping timestamps may
vary when regenerated; compare the embedded project semantics with expected.json.
