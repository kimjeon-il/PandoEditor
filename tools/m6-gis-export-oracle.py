#!/usr/bin/env python3
"""Independent SQLite/ZIP check of the stage 7 native GIS-only export."""
import json
import sqlite3
import struct
import subprocess
import sys
import tempfile
import zipfile
from contextlib import closing
from pathlib import Path


def tables(db):
    return {row[0] for row in db.execute("SELECT name FROM sqlite_master WHERE type='table'")}


def main(executable):
    with tempfile.TemporaryDirectory() as directory:
        subprocess.run([executable, "--emit", directory], check=True)
        root = Path(directory)
        with zipfile.ZipFile(root / "selected.zip") as bundle:
            assert bundle.testzip() is None
            manifest = json.loads(bundle.read("manifest.json"))
            assert manifest["pandolabExport"] is True
            assert manifest["schemaVersion"] == 3
            assert manifest["crs"] == "EPSG:4326"
            assert [(item["file"], item["targetType"]) for item in manifest["layers"]] == [
                ("countries.geojson", "country"),
                ("generic_features.geojson", "generic"),
                ("language_distribution.geojson", "distribution"),
                ("labels.geojson", "label"),
            ]
            assert manifest["layers"][2]["distributionType"] == "language"
            assert set(bundle.namelist()) == {"manifest.json"} | {
                item["file"] for item in manifest["layers"]
            }
            country = json.loads(bundle.read("countries.geojson"))["features"][0]
            assert len(country["geometry"]["coordinates"][0]) == 2
            assert country["properties"]["source_library_id"] == "historical-country:A"
            distribution = json.loads(bundle.read("language_distribution.geojson"))["features"][0]
            assert distribution["properties"]["source_mode"] == "territorial"
            assert distribution["properties"]["territorial_unit_id"] == "A"
        with closing(sqlite3.connect(root / "countries.gpkg")) as db:
            all_tables = tables(db)
            assert "countries" in all_tables
            assert "language_distribution" not in all_tables
            assert "pandolab_project_settings" not in all_tables
            assert "pandolab_country_assets" not in all_tables
            assert db.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
            assert db.execute("PRAGMA application_id").fetchone()[0] == 1196444487
            assert db.execute("SELECT srs_id FROM gpkg_geometry_columns WHERE table_name='countries'").fetchone()[0] == 4326
            assert db.execute("SELECT pandolab_id,source_library_id FROM countries").fetchone() == ("A", "historical-country:A")
            blob = db.execute("SELECT geom FROM countries").fetchone()[0]
            assert blob[:2] == b"GP" and blob[3] == 1
            assert struct.unpack_from("<I", blob, 4)[0] == 4326
            assert blob[8] == 1 and struct.unpack_from("<I", blob, 9)[0] == 6
            assert struct.unpack_from("<I", blob, 13)[0] == 1
            assert blob[17] == 1 and struct.unpack_from("<I", blob, 18)[0] == 3
            assert struct.unpack_from("<I", blob, 22)[0] == 2  # Outer ring and hole.
        with closing(sqlite3.connect(root / "selected.gpkg")) as db:
            all_tables = tables(db)
            assert {"countries", "places", "generic_features_point", "generic_features_line",
                    "generic_features_polygon", "language_distribution", "ethnicity_distribution",
                    "religion_distribution"} <= all_tables
            assert "pandolab_project_settings" not in all_tables
            assert "pandolab_country_assets" not in all_tables
            assert db.execute("SELECT COUNT(*) FROM language_distribution").fetchone()[0] == 1
            assert db.execute("SELECT source_mode,territorial_unit_id,share FROM language_distribution").fetchone() == ("territorial", "A", 73)
            assert db.execute("SELECT COUNT(*) FROM generic_features_point").fetchone()[0] == 1
            assert db.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
    print("native GIS ZIP/GeoPackage: independent SQLite and zipfile checks passed")


if __name__ == "__main__":
    main(sys.argv[1])
