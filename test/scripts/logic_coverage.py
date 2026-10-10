"""Inventory all production C files against a Ceedling Cobertura report.

Uninstrumented files remain unmeasured, never silently disappear from the
inventory. Reported totals exclude test doubles, headers and vendor code.
Integration and target tests are separate evidence, not included here.
"""

import argparse
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET


def read_counts(element):
    lines = element.findall("./lines/line")
    branches_hit = 0
    branches_total = 0
    for line in lines:
        if line.get("branch") == "true":
            match = re.fullmatch(r"[\d.]+% \((\d+)/(\d+)\)",
                                 line.get("condition-coverage", ""))
            if match is None:
                raise ValueError("Missing branch counts")
            hit, total = map(int, match.groups())
            if not 0 <= hit <= total:
                raise ValueError("Inconsistent branch counts")
            branches_hit += hit
            branches_total += total
    return {
        "lines_hit": sum(int(line.get("hits", "0")) > 0 for line in lines),
        "lines_total": len(lines),
        "branches_hit": branches_hit,
        "branches_total": branches_total,
        "uncovered_lines": [int(line.get("number")) for line in lines
                            if int(line.get("hits", "0")) == 0],
        "partial_branch_lines": [int(line.get("number")) for line in lines
                                 if line.get("branch") == "true" and
                                 not line.get("condition-coverage", "")
                                 .startswith("100%")],
    }


def inventory(document, production_files):
    classes = {}
    for element in document.findall(".//class"):
        name = element.get("filename", "").replace("\\", "/")
        if name not in production_files:
            continue
        if name in classes:
            raise ValueError(f"Duplicate coverage class: {name}")
        classes[name] = element
    rows = []
    for name in sorted(production_files):
        row = {"file": name, "status": "unmeasured"}
        if name in classes:
            row.update(read_counts(classes[name]))
            row["status"] = "measured"
        rows.append(row)
    measured = [row for row in rows if row["status"] == "measured"]
    totals = {key: sum(row[key] for row in measured) for key in
              ("lines_hit", "lines_total", "branches_hit", "branches_total")}
    return {
        "scope": "Ceedling production C files only; not whole-firmware coverage",
        "production_files": len(rows),
        "measured_files": len(measured),
        "unmeasured_files": len(rows) - len(measured),
        "measured_totals": totals,
        "files": rows,
    }


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--coverage", type=Path, default=root / "test/build/"
                        "ceedling/artifacts/gcov/gcovr/"
                        "GcovCoverageCobertura.xml")
    parser.add_argument("--output", type=Path, default=root /
                        "test/build/logic-coverage.json")
    args = parser.parse_args()
    try:
        files = {path.relative_to(root).as_posix()
                 for path in (root / "Application").rglob("*.c")}
        document = ET.parse(args.coverage).getroot()
        report = inventory(document, files)
        if report["measured_files"] == 0:
            raise ValueError("Report contains no production C coverage")
        report["coverage_timestamp"] = document.get("timestamp")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n",
                               encoding="utf-8")
        print(f"Production C files: {report['production_files']}; "
              f"measured: {report['measured_files']}; "
              f"unmeasured: {report['unmeasured_files']}")
        print(f"Coverage inventory: {args.output}")
        return 0
    except (OSError, ET.ParseError, ValueError) as error:
        print(f"Cannot read coverage inventory: {error}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
