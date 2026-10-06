"""The aggregate must reject omission of any M9.7.4 regression target."""
import importlib.util
from pathlib import Path
import tempfile
import sys
sys.dont_write_bytecode = True
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("regression_audit", ROOT / "tools/m6-regression-audit.py")
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)
STAGE4 = {
    "m974_source_history_contract",
    "m974_snap_tests", "m974_snap_provider_tests", "m974_snap_workflow_tests",
    "m974_snap_ui_tests", "m974_snap_provider_probe_contract",
    "m974_boundary_session_tests", "m974_boundary_hierarchy_tests", "m974_boundary_ui_tests",
    "m974_regression_gate_tests", "m974_boundary_delivery_tests", "m974_native_boundary_contract", "m974_native_snap_contract", "m974_boundary_supplemental_tests", "m974_snap_order_tests", "m974_native_supplemental_contract", "m974_source_order_contract",
}

class RequiredStage4Tests(unittest.TestCase):
    def fixture(self, folder, names, extra=""):
        xml = folder / "results.xml"
        xml.write_text("<testsuite>" + "".join(f'<testcase name="{name}" />' for name in sorted(names)) + extra + "</testsuite>")
        log = folder / "results.log"
        log.write_text("Totals: 1 passed, 0 failed, 0 skipped\n")
        return xml, log

    def test_each_stage4_test_is_required(self):
        names = AUDIT.REQUIRED | STAGE4 | {f"other-{index}" for index in range(100)}
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            for omitted in sorted(STAGE4):
                with self.subTest(omitted=omitted):
                    xml, log = self.fixture(folder, names - {omitted})
                    with self.assertRaisesRegex(ValueError, "missing regression gates"):
                        AUDIT.audit(xml, log)

    def test_complete_suite_and_existing_skip_rejection(self):
        names = AUDIT.REQUIRED | STAGE4 | {f"other-{index}" for index in range(100)}
        with tempfile.TemporaryDirectory() as directory:
            xml, log = self.fixture(Path(directory), names)
            self.assertEqual(AUDIT.audit(xml, log), (len(names), 0, 0))
            log.write_text("SKIP : unsupported device\n")
            with self.assertRaisesRegex(ValueError, "1 skipped"):
                AUDIT.audit(xml, log)

    def test_pure_boundary_target_cannot_be_omitted(self):
        name = "m976_shared_boundary_engine_tests"
        names = AUDIT.REQUIRED | STAGE4 | {f"other-{index}" for index in range(100)}
        with tempfile.TemporaryDirectory() as directory:
            xml, log = self.fixture(Path(directory), names | {name})
            self.assertEqual(AUDIT.audit(xml, log), (len(names | {name}), 0, 0))
            xml, log = self.fixture(Path(directory), names - {name})
            with self.assertRaisesRegex(ValueError, "missing regression gates"):
                AUDIT.audit(xml, log)

    def test_pure_selection_target_cannot_be_omitted(self):
        name = "territory_selection_engine_tests"
        names = AUDIT.REQUIRED | STAGE4 | {f"other-{index}" for index in range(100)}
        with tempfile.TemporaryDirectory() as directory:
            xml, log = self.fixture(Path(directory), names | {name})
            self.assertEqual(AUDIT.audit(xml, log), (len(names | {name}), 0, 0))
            xml, log = self.fixture(Path(directory), names - {name})
            with self.assertRaisesRegex(ValueError, "missing regression gates"):
                AUDIT.audit(xml, log)

    def test_typed_cut_adapter_target_cannot_be_omitted(self):
        name = "territory_cut_adapter_tests"
        names = AUDIT.REQUIRED | STAGE4 | {f"other-{index}" for index in range(100)}
        with tempfile.TemporaryDirectory() as directory:
            xml, log = self.fixture(Path(directory), names | {name})
            self.assertEqual(AUDIT.audit(xml, log), (len(names | {name}), 0, 0))
            xml, log = self.fixture(Path(directory), names - {name})
            with self.assertRaisesRegex(ValueError, "missing regression gates"):
                AUDIT.audit(xml, log)

    def test_boundary_session_replacement_cannot_be_omitted(self):
        name = "m977_boundary_session_replacement_contract"
        names = AUDIT.REQUIRED | {name} | {f"other-{index}" for index in range(100)}
        with tempfile.TemporaryDirectory() as directory:
            xml, log = self.fixture(Path(directory), names)
            self.assertEqual(AUDIT.audit(xml, log), (len(names), 0, 0))
            xml, log = self.fixture(Path(directory), names - {name})
            with self.assertRaisesRegex(ValueError, "missing regression gates"):
                AUDIT.audit(xml, log)

    def test_boundary_timing_diagnostic_cannot_be_omitted(self):
        name = "m977_boundary_timing_contract"
        names = AUDIT.REQUIRED | {name} | {f"other-{index}" for index in range(100)}
        with tempfile.TemporaryDirectory() as directory:
            xml, log = self.fixture(Path(directory), names)
            self.assertEqual(AUDIT.audit(xml, log), (len(names), 0, 0))
            xml, log = self.fixture(Path(directory), names - {name})
            with self.assertRaisesRegex(ValueError, "missing regression gates"):
                AUDIT.audit(xml, log)

if __name__ == "__main__":
    unittest.main()
