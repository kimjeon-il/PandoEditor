# Territorial source provenance and project wire contract

This contract is shared by Pando Web and PandoEditor. The temporal semantics in
`docs/timeline-contract.md` are unchanged.

- Web project schemaVersion: **10**; native project version: **10**.
- Territorial identity/model schemaVersion: **6**; timelineRecords: **1**.
- Project `id` identifies an independently editable instance. It is never the
  catalog entity identity or a geometry archive reference.
- `properties.sourceEntityId` (native unit `sourceEntityId`) records the source
  entity ID. `sourceGeometryVersion` records the selected source versionId.
  Both are strings; empty means no catalog provenance.
- GeoPackage derived territorial tables use `source_entity_id` and
  `source_geometry_version`. Embedded project JSON remains authoritative.
- A catalog import copies one selected geometry into new static, unbounded
  project lifetime/binding/parent records. It retains the original lifetime in
  `metadata.sourceLifetime`, geometry validity in `metadata.sourceGeometryValidity`,
  the selection precision/date in `metadata.sourceReferenceDate`, and source
  attribution in `metadata.sourceInfo`.
- Source IDs/version IDs are preserved. New project instances receive their
  own IDs; existing project IDs and complete geometry archive survive exchange.
- Repeated imports are distinct instances. Source administrative parent links
  may be translated only through an explicitly selected project parent or the
  new objects in the same import batch. They imply no political sovereignty.
- Project save/restore does not require catalog access. Complex timeline projects
  remain storable/exchangeable; static UI activation policy is unchanged.
- Retired project 9, identity 5 and sourceLibraryId/source_library_id are rejected.
  No reader, migration, alias or compatibility fallback is provided.

Fixture oracle changes for this revision are limited to the new schema numbers
and provenance contract. Geometry coordinates, archive identity, temporal
precision and inclusive bounds are not regenerated from codec output.
