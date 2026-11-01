"""Parse Ceedling's real HTTP JSON outputs using Python's independent JSON parser."""
from __future__ import annotations

import ast
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def main():
    output = ROOT / "test/build/http-json-oracle/responses.jsonl"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.unlink(missing_ok=True)
    board_output = output.with_name("board.jsonl")
    board_output.unlink(missing_ok=True)
    environment = dict(os.environ, HTTP_JSON_OUTPUT=str(output), HTTP_BOARD_OUTPUT=str(board_output))
    result = subprocess.run(["ruby", "-S", "ceedling", "test:pattern[(http_json_scenario|http_board_scenario)]"],
                            cwd=ROOT / "test", env=environment,
                            capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode:
        print(result.stdout + result.stderr)
        result.check_returncode()
    if not output.exists():
        raise RuntimeError("Ceedling did not produce current HTTP JSON responses")
    source = (ROOT / "Application/web-server/http_handlers.c").read_text(encoding="utf-8-sig")
    # Literal replies must remain complete JSON; their length is calculated
    # by the production helper rather than a handwritten numeric argument.
    literals = re.findall(
        r'http_json_reply\(\s*((?:"(?:\\.|[^"\\])*"\s*)+)\);', source)
    assert literals
    for literal in literals:
        fragments = re.findall(r'"(?:\\.|[^"\\])*"', literal)
        json.loads("".join(ast.literal_eval(part) for part in fragments))
    device, iec, modbus, logs, empty_logs, faults, terminal = [
        json.loads(line) for line in output.read_text(encoding="utf-8").splitlines()]
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
    assert 'quote" slash\\ control\n UTF8:\u0130' in logs["recs"]
    assert empty_logs == {"recs": "", "t": 20}
    assert faults["tc"] == [15, 15, 15] and faults["pc"] == [15, 15, 15]
    assert all(len(faults[key].splitlines()) == 15
               for key in ["L1T", "L1P", "L2T", "L2P", "L3T", "L3P"])
    assert terminal == {"rx": "ok\r\n"}
    boards = [json.loads(line) for line in board_output.read_text(encoding="utf-8").splitlines()]
    assert len(boards) == 2
    expected = [dict(GsmRAT=4, GsmRxlev=99, GsmRscp=255, GsmRsrp=41,
                     GsmRsrq=20, GsmCREG=0, GsmCGREG=0, GsmCEREG=1),
                dict(GsmRAT=2, GsmRxlev=51, GsmRscp=255, GsmRsrp=255,
                     GsmRsrq=255, GsmCREG=1, GsmCGREG=5, GsmCEREG=0)]
    for data, fields in zip(boards, expected):
        assert data["GsmSig"] == 13
        assert data["DIP"] == [1, 0]
        for key, value in fields.items():
            assert data[key] == value, key
    print("PASS real HTTP JSON: device, IEC/IOAs, Modbus map, paged logs, "
          "90 faults, terminal escapes/no execution on invalid JSON, FW metadata, 2G/4G board")


if __name__ == "__main__":
    main()
