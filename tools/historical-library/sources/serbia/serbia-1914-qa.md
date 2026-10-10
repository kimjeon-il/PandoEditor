# Serbia 1914 — QA

Reference date: 1914-07-28

## Source
- ACDH-CH HistoGIS: Srbija, valid 1913-08-11–1919-12-31
- Source file: `single_files/srbija__1913-08-11_1919-12-31.geojson`
- Source feature id: 10118
- Source layer: Europe Stateborders 1919

## Austria-Hungary shared-border checks
- Iron Gates / AH–Romania–Serbia junction: [22.472058, 44.714050] — present
- Sava/Drina / northern Bosnia junction: [19.357532, 44.899240] — present
- Bosnia–Montenegro–Serbia junction: [19.226442, 43.527993] — present
- AH Serbia-sector v2 vertices found on Serbia ring: 171/172
- Bosnia–Serbia E vertices found on Serbia ring: 210/211
- Note: Bosnia QA already records two edges with equivalent linework but different segmentation (~6.91 m midpoint offset); retained as the same historical frontier.

## Geometry
- Geometry type: MultiPolygon
- Coordinate vertices: 12114
- BBOX: [19.103576, 40.85366, 23.034051, 44.977094]
- Coordinate rounding: 6 decimal places

## Status
Ready as a 1914 Kingdom of Serbia base geometry. The north/west frontier is compatible with the previously reconstructed Austria-Hungary frontier; remaining south/east edges come from the HistoGIS Serbia state polygon valid for 1913–1919.
