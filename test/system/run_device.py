#!/usr/bin/env python3
"""List and run one hardware test with its original arguments."""

import argparse
from pathlib import Path
import subprocess
import sys


TEST_ROOT = Path(__file__).resolve().parents[1]
TESTS = {
    "relay-din": ("system/iec104_master/run_relay_din.py",
                  "IEC104 GI20, relay SBO and digital input changes"),
    "iec104": ("system/iec104_master/run_suite.py", "IEC104 master acceptance"),
    "ntp-reset": ("system/iec104_master/check_ntp_reset.py",
                  "RTC/NTP after RTU reset"),
    "modbus": ("integration/modbus_device/run_tests.py", "RS485 Modbus RTU"),
    "web": ("integration/web_device/run_tests.py", "Web API acceptance"),
    "web-roundtrip": ("integration/web_device/run_roundtrip.py",
                      "Web configuration writes and restoration"),
    "rf-hil": ("system/rf_hil/run_hil.py", "RTU with simulated RF hub"),
    "rf-real-mh": ("system/rf_real_mh/run_smoke.py", "RTU with physical RF hub"),
    "monitor": ("system/device_monitor/run_monitor.py", "Device health monitoring"),
    "gsm-soak": ("system/gsm_soak.py", "Long running GSM HTTP and IEC104"),
    "gsm-disconnect": ("system/gsm_disconnect_test.py", "GSM link recovery"),
    "http-drop": ("system/http_port_drop_test.py", "HTTP client disconnect"),
    "gsm-resources": ("system/gsm_http_resources_test.py", "Embedded HTTP resources"),
    "gsm-firmware": ("system/gsm_firmware_transfer.py", "Firmware transfer resume"),
    "gsm-fault": ("system/gsm_fault/run_tests.py", "GSM fault fixture"),
    "device-fault": ("system/device_fault/run_faults.py", "UART and HardFault fixture"),
}


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        epilog="Use --list for the catalog or TEST --help for test options.",
    )
    parser.add_argument("--list", action="store_true", help="list tests without running them")
    parser.add_argument("test", nargs="?", choices=TESTS, metavar="TEST")
    parser.add_argument("test_args", nargs=argparse.REMAINDER, metavar="ARGS")
    args = parser.parse_args()
    if args.list:
        if args.test:
            parser.error("--list does not take a test name; use TEST --list for cases")
        for name, (_, description) in TESTS.items():
            print(f"{name:<16} {description}")
        return 0
    if not args.test:
        parser.print_help()
        return 0

    script, _ = TESTS[args.test]
    try:
        return subprocess.call([sys.executable, str(TEST_ROOT / script), *args.test_args])
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
