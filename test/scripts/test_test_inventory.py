"""Guard against reporting partial/failed Ceedling runs as a green total."""
from contextlib import redirect_stdout
from io import StringIO
import unittest
import xml.etree.ElementTree as ET

from test_inventory import report_inventory


class InventoryTests(unittest.TestCase):
    def run_report(self, suites, total, expected=None):
        document = ET.Element("testsuites", tests=str(total))
        for suite in suites:
            ET.SubElement(document, "testsuite", suite)
        output = StringIO()
        with redirect_stdout(output):
            result = report_inventory(document, expected or {"gsm/test_a"})
        return result, output.getvalue()

    def test_complete_green_run(self):
        result, output = self.run_report(
            [{"name": "gsm/test_a", "tests": "3"}], 3)
        self.assertEqual(0, result)
        self.assertIn("passed: 3", output)
        self.assertIn("Inventory: complete", output)

    def test_single_package_is_partial_when_other_sources_exist(self):
        result, output = self.run_report(
            [{"name": "gsm/test_a", "tests": "3"}], 3,
            {"gsm/test_a", "libs/test_b"})
        self.assertEqual(1, result)
        self.assertIn("Partial run; missing: libs/test_b", output)

    def test_failures_are_subtracted_from_pass_count(self):
        result, output = self.run_report(
            [{"name": "gsm/test_a", "tests": "3", "failures": "1"}], 3)
        self.assertEqual(1, result)
        self.assertIn("passed: 2; failures: 1", output)

    def test_ignored_and_error_tests_are_not_passed(self):
        result, output = self.run_report(
            [{"name": "gsm/test_a", "tests": "3", "skipped": "1",
              "errors": "1"}], 3)
        self.assertEqual(1, result)
        self.assertIn("passed: 1", output)

    def test_duplicate_suites_cannot_inflate_green_total(self):
        result, output = self.run_report(
            [{"name": "gsm/test_a", "tests": "3"}] * 2, 6)
        self.assertEqual(1, result)
        self.assertIn("inconsistent", output)

    def test_stale_or_unknown_suite_is_not_current_inventory(self):
        result, output = self.run_report(
            [{"name": "libs/test_deleted", "tests": "3"}], 3)
        self.assertEqual(1, result)
        self.assertIn("Unexpected suites: libs/test_deleted", output)

    def test_wrong_top_level_count_is_rejected(self):
        result, output = self.run_report(
            [{"name": "gsm/test_a", "tests": "3"}], 100)
        self.assertEqual(1, result)
        self.assertIn("inconsistent", output)


if __name__ == "__main__":
    unittest.main()
