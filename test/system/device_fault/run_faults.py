"""Run authorized bench faults on the temporary fixture image only.

Requires COM18 console, COM10 RS485 and an isolated safe bench. This is
not a physical DMA fault test: the IRQ timeout is real, HAL failures are
injected after the real HAL side effects. Never run on a field device.
"""
import argparse
import json
import re
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/hwtest"))
sys.path.insert(0, str(ROOT / "test/integration/modbus_device"))
from serial_io import ConsoleSession
import run_tests as modbus
import serial


def verify_hardfault(lines):
    """Compare the actual exception dump against the post-boot elog."""
    patterns = {"pc": r"PC\s*[:=]\s*0x([0-9a-fA-F]+)",
                "lr": r"LR\s*[:=]\s*0x([0-9a-fA-F]+)",
                "cfsr": r"SCB->CFSR\s*=\s*0x([0-9a-fA-F]+)",
                "hfsr": r"SCB->HFSR\s*=\s*0x([0-9a-fA-F]+)"}
    stored = re.search(r"HARDFAULT\s+pc=0x([0-9a-fA-F]+) "
                       r"lr=0x([0-9a-fA-F]+) cfsr=0x([0-9a-fA-F]+) "
                       r"hfsr=0x([0-9a-fA-F]+)", lines)
    assert stored, "persistent fault register record missing"
    values = {}
    for index, (key, pattern) in enumerate(patterns.items(), 1):
        match = re.search(pattern, lines)
        assert match, "fault dump register missing: " + key
        value = int(match.group(1), 16)
        assert value == int(stored.group(index), 16), key + " mismatch"
        values[key] = value
    assert values["hfsr"] & 0x40000000, "FORCED bit missing"
    assert values["cfsr"] & 0x10000, "UNDEFINSTR bit missing"
    return values


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--console", required=True)
    parser.add_argument("--rs485", required=True)
    parser.add_argument("--outdir", required=True, type=Path)
    args = parser.parse_args()
    args.outdir.mkdir(parents=True, exist_ok=True)
    console = ConsoleSession(args.console,
                             logfile=str(args.outdir / "console.log"))
    results = {}
    try:
        assert console.send_and_wait("cslog on", "Console logger started!", 10)
        console.send("su admin"); time.sleep(.4)
        console.send("admin"); time.sleep(.4)
        assert console.send_and_wait("hwtest status", "HWTEST: uart=", 10)
        with serial.Serial(args.rs485, 115200, timeout=.1) as rs485:
            for mode in ("timeout", "recover", "fail"):
                console.mark()
                mark = console.cursor
                assert console.send_and_wait("hwtest " + mode,
                                             "HWTEST: armed " + mode, 10)
                modbus.read_regs(rs485, 0, 1)
                result = console.wait_for(
                    r"MODBUS: TX completion timeout, recovery=(\d)",
                    timeout_s=10, since=mark)
                assert result, "production timeout did not execute: " + mode
                recovered = result[2].group(1) == "1"
                assert recovered == (mode != "fail"), (mode, recovered)
                assert console.send_and_wait("hwtest resume",
                                             "HWTEST: DMA IRQ restored", 10)
                answer = modbus.read_regs(rs485, 0, 1)
                if mode != "fail":
                    assert answer == [3], (mode, answer)
                else:
                    assert answer is None, answer
                    time.sleep(8)
                    assert console.send_and_wait("hwtest status",
                                                 "HWTEST: uart=", 10)
                results[mode] = {"recovered": recovered, "next_read": answer}
                (args.outdir / "results.json").write_text(json.dumps(results, indent=2))
                print(mode, "PASS", flush=True)
        console.send("elog dump 12"); time.sleep(3)
        console.mark(); mark = console.cursor
        console.send("hwtest hardfault")
        assert console.wait_for("HFSR: FORCED", timeout_s=10, since=mark)
        assert console.wait_for("UNDEFINSTR", timeout_s=10, since=mark)
        assert console.wait_for("watchdog will reset", timeout_s=10, since=mark)
        time.sleep(15)
        assert console.send_and_wait("cslog on", "Console logger started!", 10)
        console.send("su admin"); time.sleep(.4)
        console.send("admin"); time.sleep(.4)
        console.send("elog dump 12"); time.sleep(3)
        console.send("bootlog dump 6"); time.sleep(2)
        lines = "\n".join(line for _, line in console.new_lines(since=mark))
        assert "HARDFAULT" in lines, "persistent HardFault record missing"
        results["hardfault"] = verify_hardfault(lines)
        (args.outdir / "results.json").write_text(json.dumps(results, indent=2))
        print("HardFault captured and persistent record found; compare registers in console.log", flush=True)
    finally:
        console.close()


if __name__ == "__main__":
    main()
