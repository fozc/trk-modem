"""Exercise current HTTP JSON handlers and parse their output independently."""
from __future__ import annotations

import argparse
import ast
import re
import json
import subprocess

from run_comparison import function, includes
from run_checks import BUILD, HOST_FLAGS, HERE, ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    args = parser.parse_args()
    BUILD.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "Application/web-server/http_handlers.c").read_text(
        encoding="utf-8-sig")
    names = ["http_json_reply", "http_json_init", "http_json_send", "write_device_identity",
             "handle_get_device_config_json", "write_iec_lines",
             "handle_get_iec_config_json", "handle_get_modbus_config_json",
             "handle_get_syslogs_json", "handle_get_fault_records_json",
             "parse_shell_request", "handle_post_web_shell", "parse_fw_metadata"]
    (BUILD / "http_production.inc").write_text(
        "\n\n".join(function(source, name)[1] for name in names) + "\n",
        encoding="utf-8", newline="\n")
    # Literal replies must remain complete JSON; their length is calculated
    # by the production helper rather than a handwritten numeric argument.
    literals = re.findall(
        r'http_json_reply\(\s*((?:"(?:\\.|[^"\\])*"\s*)+)\);', source)
    assert literals
    for literal in literals:
        fragments = re.findall(r'"(?:\\.|[^"\\])*"', literal)
        json.loads("".join(ast.literal_eval(part) for part in fragments))
    executable = BUILD / "http_json_checks.exe"
    command = [args.cc, *HOST_FLAGS, "-Wno-array-parameter", "-Wno-sign-conversion",
               "-Wno-conversion", "-DUNIT_TEST", "-DNO_CONSOLE_LOG", "-O2",
               *includes(), "-I", str(ROOT / "test/support"),
               str(HERE / "http_fixture.c"), str(ROOT / "Application/json/json.c"),
               str(ROOT / "Application/libs/xprintf.c"),
               str(ROOT / "Application/libiec104/iec104_util.c"),
               str(ROOT / "Application/libiec104/cp56time2a.c"),
               str(ROOT / "Application/web-server/modbus_map_json.c"),
               "-lm", "-o", str(executable)]
    subprocess.run(command, check=True)
    output = subprocess.check_output([str(executable)], encoding="utf-8")
    device, iec, modbus, logs, empty_logs, faults, terminal = [
        json.loads(line) for line in output.splitlines()]
    assert device["SimKartAPN"] == 'a"b\\c\n'
    assert device["PreserveOutputStates"] is True
    assert device["LifeTime"] == 123456789
    assert iec["ScadaIPAdresi"] == "192.168.1.1"
    count = len(iec["Hatlar"]["inUse"])
    assert iec["Hatlar"]["IOA_R_AnlikAkim"] == list(range(100, 100 + count))
    assert iec["Hatlar"]["IOA_T_TripFailed"] == list(range(200, 200 + count))
    assert iec["Hatlar"]["TemporaryFaultBase"] == list(range(300, 300 + count))
    assert modbus["CihazID"] == 7 and modbus["BaudRate"] == 115200
    assert modbus["RegisterMap"]["AddressBase"] == 40000
    assert logs["t"] == 20 and logs["recs"].startswith("#9 ")
    assert '#11 ' in logs["recs"] and '#12 ' not in logs["recs"]
    assert 'quote" slash\\ control\n UTF8:İ' in logs["recs"]
    assert empty_logs == {"recs": "", "t": 20}
    assert faults["tc"] == [15, 15, 15] and faults["pc"] == [15, 15, 15]
    assert all(len(faults[key].splitlines()) == 15
               for key in ["L1T", "L1P", "L2T", "L2P", "L3T", "L3P"])
    assert terminal == {"rx": "ok\r\n"}
    print("PASS real HTTP JSON: device, IEC/IOAs, Modbus map, paged logs, "
          "90 faults, terminal escapes/no execution on invalid JSON, FW metadata")


if __name__ == "__main__":
    main()
