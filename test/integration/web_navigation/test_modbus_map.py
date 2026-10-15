"""Check RO Modbus metadata as JSON and share the real payload with UI tests."""
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / "test/build/modbus-map-web"
BUILD.mkdir(parents=True, exist_ok=True)
source = BUILD / "emit.c"
source.write_text(r'''/* Author: Fatih Ozcan
 *         fatihozcan@gmail.com */
#include "modbus_map_json.h"
#include <stdio.h>
static char buffer[8192];
int main(void)
{
    size_t length = 0;
    if (!modbus_map_json_build(buffer, sizeof(buffer), &length)) return 1;
    return (fwrite(buffer, 1, length, stdout) == length) ? 0 : 2;
}
''', encoding="ascii")
command = [os.environ.get("CC", "gcc"), "-std=c11", "-Wall", "-Wextra",
           "-Werror", "-Wshadow", "-Wconversion", "-Wdouble-promotion",
           "-Wformat=2", "-DNO_CONSOLE_LOG"]
for folder in sorted({path.parent for path in (ROOT / "Application").rglob("*.h")}):
    command += ["-I", str(folder)]
executable = BUILD / "emit.exe"
command += [str(source), str(ROOT / "Application/web-server/modbus_map_json.c"),
            str(ROOT / "Application/libs/xprintf.c"), "-o", str(executable)]
subprocess.run(command, check=True)
raw = subprocess.check_output([str(executable)])
metadata = json.loads(raw)
assert len(raw) < 7800
assert metadata["AddressBase"] == 40000
assert metadata["PhaseOffset"] == 4 and metadata["PhaseStride"] == 24
assert metadata["FeederCount"] == 4 and metadata["FaultRecords"] == 15
for group, base, count in [("Phase", 0, 24), ("Record", 0, 8),
                            ("System", 49000, 17), ("Power", 49200, 38),
                            ("Bms", 49300, 79), ("Gsm", 49400, 9)]:
    occupied = set()
    for name, address, width, kind, unit in metadata[group]:
        assert isinstance(name, str) and isinstance(unit, str)
        assert kind in {"UINT16", "INT16", "UINT32", "FLOAT32"}
        if kind in {"UINT32", "FLOAT32"}:
            assert width == 2
        registers = set(range(address, address + width))
        assert not occupied.intersection(registers), (group, name)
        occupied.update(registers)
    assert occupied == set(range(base, base + count)), group
(BUILD / "metadata.json").write_bytes(raw)
print("PASS: device metadata JSON, all field spans, no overlap, %d bytes" % len(raw))
