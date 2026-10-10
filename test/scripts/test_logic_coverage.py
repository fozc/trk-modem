"""Verify coverage cannot hide uninstrumented firmware or count fake code."""

import unittest
import xml.etree.ElementTree as ET

from logic_coverage import inventory


class LogicCoverageTests(unittest.TestCase):
    def document(self, name="Application/a.c"):
        document = ET.Element("coverage")
        item = ET.SubElement(document, "class", filename=name)
        lines = ET.SubElement(item, "lines")
        ET.SubElement(lines, "line", number="7", hits="3", branch="true",
                      **{"condition-coverage": "50% (1/2)"})
        ET.SubElement(lines, "line", number="8", hits="0", branch="false")
        return document

    def test_uninstrumented_file_stays_visible_without_invented_denominator(self):
        report = inventory(self.document(), {"Application/a.c", "Application/b.c"})
        self.assertEqual(2, report["production_files"])
        self.assertEqual(1, report["unmeasured_files"])
        self.assertEqual({"file": "Application/b.c", "status": "unmeasured"},
                         report["files"][1])
        self.assertEqual(2, report["measured_totals"]["lines_total"])

    def test_fake_vendor_and_header_code_do_not_inflate_production_totals(self):
        document = self.document()
        for name in ("test/fake.c", "Drivers/hal.c", "Application/a.h"):
            document.append(self.document(name)[0])
        report = inventory(document, {"Application/a.c"})
        self.assertEqual(1, report["measured_files"])
        self.assertEqual(1, report["measured_totals"]["lines_hit"])

    def test_uncovered_lines_and_partial_branches_are_actionable(self):
        row = inventory(self.document(), {"Application/a.c"})["files"][0]
        self.assertEqual([8], row["uncovered_lines"])
        self.assertEqual([7], row["partial_branch_lines"])
        self.assertEqual(1, row["branches_hit"])
        self.assertEqual(2, row["branches_total"])

    def test_duplicate_source_report_is_rejected(self):
        document = self.document()
        document.append(self.document()[0])
        with self.assertRaises(ValueError):
            inventory(document, {"Application/a.c"})

    def test_windows_paths_are_matched_to_repository_sources(self):
        report = inventory(self.document("Application\\a.c"), {"Application/a.c"})
        self.assertEqual(1, report["measured_files"])

    def test_malformed_or_impossible_branch_counts_are_rejected(self):
        for coverage in ("", "50% (3/2)", "50%"):
            document = self.document()
            document.find(".//line").set("condition-coverage", coverage)
            with self.assertRaises(ValueError):
                inventory(document, {"Application/a.c"})


if __name__ == "__main__":
    unittest.main()
