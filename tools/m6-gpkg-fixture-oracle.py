"""Independently inspect GeoPackages written by the pinned web worker."""
import hashlib
import json
import sqlite3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "tests/fixtures/web-m6"
manifest = json.loads((ROOT / "manifest.json").read_text())
for name in ("gis-gpkg-worker.js", "gis-adapters.js"):
    body = (ROOT / "source" / name).read_bytes()
    blob = hashlib.sha1(f"blob {len(body)}\0".encode() + body).hexdigest()
    assert blob == manifest["sourceBlobs"][name], (name, blob)

for filename, is_project in (("web-gis.gpkg", False), ("web-project.gpkg", True)):
    connection = sqlite3.connect((ROOT / filename).as_uri() + "?mode=ro", uri=True)
    try:
        assert connection.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
        assert connection.execute("PRAGMA application_id").fetchone()[0] == 1196444487
        assert connection.execute("PRAGMA user_version").fetchone()[0] == 10300
        tables = {row[0] for row in connection.execute(
            "SELECT name FROM sqlite_master WHERE type='table'")}
        assert {"gpkg_spatial_ref_sys", "gpkg_contents", "gpkg_geometry_columns",
                "countries", "subunits", "regions", "language_distribution",
                "generic_features_point", "places"} <= tables
        assert ("pandolab_project_settings" in tables) == is_project
        assert ("pandolab_country_assets" in tables) == is_project
        columns = connection.execute(
            "SELECT table_name,column_name,geometry_type_name,srs_id,z,m "
            "FROM gpkg_geometry_columns ORDER BY table_name").fetchall()
        assert len(columns) == 10
        assert all(name == "geom" and srs == 4326 and z == m == 0
                   for _, name, _, srs, z, m in columns)
        for table, column, kind, *_ in columns:
            assert table in tables and kind
            metadata = connection.execute(
                "SELECT data_type,srs_id FROM gpkg_contents WHERE table_name=?",
                (table,)).fetchone()
            assert metadata == ("features", 4326)
            for (geometry,) in connection.execute(f'SELECT "{column}" FROM "{table}"'):
                assert geometry[:2] == b"GP" and geometry[2] == 0
                assert int.from_bytes(geometry[4:8], "little") == 4326
        country = connection.execute(
            "SELECT pandolab_id,pandolab_name,geom FROM countries").fetchone()
        assert country[:2] == ("AAA", "Alpha") and country[2][9] == 6
        row = connection.execute(
            "SELECT entry_id,layer_id,source_mode,territorial_unit_id,share "
            "FROM language_distribution").fetchone()
        assert row == ("entry:1", "lang:1", "territorial", "AAA", 60.0)
        if is_project:
            settings = connection.execute(
                "SELECT json_value FROM pandolab_project_settings "
                "WHERE setting_key='project_state'").fetchone()[0]
            assert "flagDataUrl" not in json.loads(settings)["countryOverrides"]["AAA"]
            asset = connection.execute(
                "SELECT mime_type,image_data FROM pandolab_country_assets "
                "WHERE country_id='AAA'").fetchone()
            assert asset == ("image/svg+xml", b'<svg xmlns="http://www.w3.org/2000/svg" width="2" height="2"><rect width="2" height="2" fill="red"/></svg>')
        print(f"{filename}: SQLite integrity, 10 vector tables, EPSG:4326, "
              f"{'project' if is_project else 'GIS-only'} tables verified")
    finally:
        connection.close()
