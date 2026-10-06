"""Report Ceedling's latest JUnit inventory without inflating test counts.

Run from any directory. A partial or failing run returns nonzero.
Only Unity test cases are counted; loop iterations are not separate tests.
"""
import argparse
from pathlib import Path
import re
import xml.etree.ElementTree as ET


def report_inventory(document, expected):
    suites = document.findall("testsuite")
    actual = {suite.get("name", "") for suite in suites}
    missing = expected - actual
    unexpected = actual - expected
    duplicate = len(actual) != len(suites)
    totals = {key: sum(int(suite.get(key, "0")) for suite in suites)
              for key in ("tests", "failures", "errors", "skipped")}
    count_error = int(document.get("tests", "-1")) != totals["tests"]
    passed = totals["tests"] - sum(totals[key] for key in
                                  ("failures", "errors", "skipped"))
    print("| File | Tests | Failures | Errors | Skipped |")
    print("|---|---:|---:|---:|---:|")
    for suite in sorted(suites, key=lambda item: item.get("name", "")):
        fields = [suite.get(key, "0") for key in
                  ("tests", "failures", "errors", "skipped")]
        print(f"| {suite.get('name')}.c | {' | '.join(fields)} |")
    print(f"\nFiles: {len(actual)}; tested: {totals['tests']}; "
          f"passed: {passed}; failures: {totals['failures']}; "
          f"errors: {totals['errors']}; skipped: {totals['skipped']}")
    if missing:
        print("Partial run; missing: " + ", ".join(sorted(missing)))
    if unexpected:
        print("Unexpected suites: " + ", ".join(sorted(unexpected)))
    if duplicate or count_error:
        print("JUnit counts or suite names are inconsistent.")
    complete = not (missing or unexpected or duplicate or count_error)
    print("Inventory: " + ("complete" if complete else "incomplete"))
    return 0 if complete and passed == totals["tests"] else 1


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--junit", type=Path, default=root / "build" /
                        "ceedling/artifacts/test/junit_tests_report.xml")
    args = parser.parse_args()
    try:
        document = ET.parse(args.junit).getroot()
        config = (root / "project.yml").read_text(encoding="utf-8")
        folders = re.findall(r"^\s*-\s*\+:([\w]+)/\*\*\s*$", config, re.M)
        expected = {file.relative_to(root).with_suffix("").as_posix()
                    for folder in folders
                    for file in (root / folder).rglob("test_*.c")}
        return report_inventory(document, expected)
    except (OSError, ET.ParseError, ValueError) as error:
        print(f"Cannot read inventory: {error}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
