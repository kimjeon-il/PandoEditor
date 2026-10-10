# Local HGIS 1914 source

Place the normalized HGIS Germany state-boundary GeoJSON here as:

```text
hgis-germany-states-1914.geojson
```

Expected source contract:

- dataset: `Germany State Boundaries, 1914, German Historical GIS`
- layer: `GHGIS1914CORE` / `vector_public:GHGIS1914GERMANY`
- geometry: 26 Polygon/MultiPolygon state features
- CRS: EPSG:4326
- state-name field: `STAAT_NAME`
- catalog bounds: west 4.490331, south 47.036290, east 23.157687, north 56.025202

Use `python tools/fetch_german_empire_1914_source.py` to fetch/normalize it, or
pass an already downloaded EPSG:4326 GeoJSON with `--from-file`.

The GeoJSON itself is ignored by Git because HGIS Germany's metadata restricts
distribution/use to non-commercial academic research unless separately licensed.
