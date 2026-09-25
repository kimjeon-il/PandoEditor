"""Independent SQLite inspection of the Qt project GeoPackage writer."""
import json
import pathlib
import sqlite3
import subprocess
import sys
import tempfile


with tempfile.TemporaryDirectory() as temporary:
    subprocess.run([sys.argv[1], "--emit", temporary], check=True)
    package = pathlib.Path(temporary) / "project.gpkg"
    with package.open("rb") as stream:
        assert stream.read(16) == b"SQLite format 3\x00"
    with sqlite3.connect(package) as database:
        assert database.execute("pragma integrity_check").fetchone() == ("ok",)
        assert database.execute("pragma application_id").fetchone() == (1196444487,)
        contents = dict(database.execute("select table_name,data_type from gpkg_contents"))
        assert contents["countries"] == "features"
        assert all(contents[name] == "attributes" for name in
                   ("pandolab_project_settings", "pandolab_country_assets",
                    "pandolab_source_info"))
        assert database.execute("select count(*) from countries").fetchone() == (3,)
        assert database.execute(
            "select distinct srs_id from gpkg_geometry_columns"
        ).fetchall() == [(4326,)]
        rows = database.execute(
            "select country_id,mime_type,image_data from pandolab_country_assets"
        ).fetchall()
        assert len(rows) == 1
        assert rows[0] == ("C", "image/svg+xml",
                           b'<svg xmlns="http://www.w3.org/2000/svg"/>')
        state = json.loads(database.execute(
            "select json_value from pandolab_project_settings where setting_key='project_state'"
        ).fetchone()[0])
        assert state["format"] == "pandoeditor-project" and state["version"] == 7
        assert {unit["id"] for unit in state["units"]} == {"A", "B", "C"}
        assert next(unit for unit in state["units"] if unit["id"] == "A")\
            ["libraryOrigin"]["libraryId"] == "history:A"
        policies = {symbol["ref"]["id"]: symbol for symbol in state["content"]["symbols"]}
        assert "A" not in policies  # implicit default policy
        assert policies["B"]["policy"] == "none"
        assert policies["C"]["policy"] == "embedded"
        assert policies["C"]["embeddedDataUrl"] == ""  # binary in the asset table
print("project GeoPackage: SQLite schema, 4326, provenance, three flag policies OK")
