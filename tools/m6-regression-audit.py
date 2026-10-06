"""Fail the complete regression gate on omitted, failed, or skipped tests."""

import argparse
import re
import xml.etree.ElementTree as ET
from pathlib import Path


REQUIRED = {
    "edit_coordinate_tests",
    "edit_geometry_tests",
    "territory_cut_adapter_tests",
    "territorial_preview_engine_tests",
    "territory_selection_engine_tests",
    "m976_shared_boundary_engine_tests",
    "m974_source_history_contract",
    "m974_snap_tests",
    "m974_snap_provider_tests",
    "m974_snap_workflow_tests",
    "m974_snap_ui_tests",
    "m974_snap_provider_probe_contract",
    "m974_boundary_session_tests",
    "m974_boundary_hierarchy_tests",
    "m974_boundary_ui_tests",
    "m974_regression_gate_tests",
    "m974_boundary_delivery_tests",
    "m974_native_boundary_contract",
    "m974_native_snap_contract",
    "m974_boundary_supplemental_tests",
    "m974_snap_order_tests",
    "m974_native_supplemental_contract",
    "m974_source_order_contract",
    "m4_geometry_web_parity",
    "m5_hydro_web_fixture",
    "m5_content_web_parity",
    "m5_label_safe_area_web",
    "presentation_editor_tests",
    "map_render_tests",
    "hydro_full_dataset_tests",
    "m6_temporal_web_parity",
    "m6_historical_catalog_web_parity",
    "m6_web_gis_zip_fixture",
    "m6_gpkg_web_fixture",
    "m6_gis_export_sqlite_oracle",
    "m6_project_gpkg_sqlite_oracle",
    "project_geopackage_controller_tests",
    "gis_import_controller_tests",
    "gis_failure_matrix_tests",
    "gis_allocation_tests",
    "ui_tests",
}


def audit(xml_path: Path, log_path: Path) -> tuple[int, int, int]:
    root = ET.parse(xml_path).getroot()
    cases = root.findall(".//testcase")
    names = [case.get("name", "") for case in cases]
    missing = REQUIRED.difference(names)
    if missing:
        raise ValueError(f"missing regression gates: {', '.join(sorted(missing))}")
    if len(names) != len(set(names)):
        raise ValueError("duplicate test names in CTest JUnit result")
    if len(cases) < 78:
        raise ValueError(f"expected at least 78 registered tests; found {len(cases)}")
    failures = sum(bool(case.findall("failure") or case.findall("error")) for case in cases)
    skipped = sum(bool(case.findall("skipped")) for case in cases)
    failures += int(root.get("failures", "0")) if not failures else 0
    skipped += int(root.get("skipped", "0")) if not skipped else 0
    log = log_path.read_text(encoding="utf-8", errors="replace")
    qt_skips = len(re.findall(r"(?m)^\s*SKIP\s*:", log))
    if qt_skips:
        skipped += qt_skips
    if failures or skipped:
        raise ValueError(f"regression gate failed: {failures} failed, {skipped} skipped")
    return len(cases), failures, skipped


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("xml", type=Path)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    count, failed, skipped = audit(args.xml, args.log)
    print(f"M3–M6 full regression: {count}/{count} passed, {failed} failed, {skipped} skipped")
