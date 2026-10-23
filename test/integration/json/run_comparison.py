"""Compare integrated production JSON paths against independent test adapters.

Production functions/field maps are extracted from current source, not maintained
copies. Host elapsed time wraps a C batch (no Python call inside the loop).
ARM sizes use identical compiler/linker settings, including linker GC.
"""
from __future__ import annotations

import argparse
import ctypes as ct
import hashlib
import json
from pathlib import Path
import platform
import re
import statistics
import struct
import subprocess
import sys
import time

from run_checks import BUILD, FLAGS, HERE, ROOT


def function(source, name):
    """Extract an unmodified definition; column-zero closing braces match
    this repository's functions (both K&R and Allman opening braces).
    Fail closed if a definition appears more than once or cannot be found.
    """
    pattern = (r"^(?:static\s+)?(?:const\s+)?(?:bool|int|void|unsigned int|"
               r"uint32_t|char\s*\*)\s*" + re.escape(name) +
               r"\([^;]*?\)\s*\{.*?^\}")
    matches = list(re.finditer(pattern, source, re.M | re.S))
    if len(matches) != 1:
        raise RuntimeError(f"Expected one current definition: {name}")
    return matches[0].start(), matches[0].group(0)


def prepare_sources():
    BUILD.mkdir(parents=True, exist_ok=True)
    config_path = ROOT / "Application/web-server/json_config.c"
    status_path = ROOT / "Application/web-server/system_status_json.c"
    config = config_path.read_text(encoding="utf-8-sig")
    status = status_path.read_text(encoding="utf-8-sig")
    names = ["config_reader_init", "config_reader_finish", "skip_value",
             "match_key", "parse_string",
             "parse_uint32", "parse_uint16", "parse_uint8", "parse_int32", "parse_bool",
             "validate_port", "validate_timeout", "validate_baud_rate",
             "validate_modbus_device_id", "validate_sim_pin", "validate_timezone",
             "expect_object_start", "is_object_end", "parse_device_config_internal",
             "parse_modbus_config_internal", "parse_device_config", "parse_modbus_config"]
    definitions = sorted(function(config, name) for name in names)
    reader_type = re.search(r"typedef struct\s*\{[^}]*\}\s*config_reader_t;",
                            config, re.S)
    assert reader_type is not None
    field_type = re.search(r"typedef struct\s*\{[^}]*\}\s*json_field_t;", status, re.S)
    assert field_type is not None
    power_header = (ROOT / "Application/power_board/power_board_scp.h").read_text()
    power_mask = re.search(r"^#define POWER_SOC_ABSOLUTE\s+[^\n]+", power_header, re.M)
    assert power_mask is not None
    getters = {"signal_quality_2G": 12, "signal_quality_3G": 18,
               "signal_quality_4G": 42, "4G_rsrq": 25,
               "creg": 1, "cgreg": 1, "cereg": 1}
    constants = "\n".join(f"#define gsm_info_get_{name}() ({value})"
                           for name, value in getters.items())
    legacy = ["/* Generated from current production source; do not edit. */",
              reader_type.group(0), power_mask.group(0), constants,
              *(definition for _, definition in definitions), field_type.group(0)]
    legacy += [function(status, name)[1] for name in
               ["has_fields", "system_status_json_build"]]
    (BUILD / "compare_legacy.inc").write_text("\n\n".join(legacy) + "\n",
                                              encoding="utf-8", newline="\n")
    fields = re.search(r"const json_field_t fields\[\]\s*=\s*\{.*?\};",
                       status, re.M | re.S)
    assert fields is not None
    modern = r'''
uint32_t compare_new_status(const char *input, size_t length,
                             char *output, size_t capacity)
{
    (void)input;
    (void)length;
    const system_status_t *status = &status_fixture;
    const uint16_t mask = status->power_valid_fields;
    FIELDS
    json_writer_t writer;
    if (capacity == 0U ||
        json_writer_init(&writer, output, capacity - 1U) != JSON_OK ||
        json_write_object_begin(&writer) != JSON_OK ||
        !write_status_arrays(&writer, status))
    {
        return 0U;
    }
    const char *adc_names[] = {"ADCValid", "ADCSampleAgeMs"};
    const uint32_t adc_values[] = {status->adc_valid ? 1U : 0U,
                                  status->adc_sample_age_ms};
    for (size_t index = 0U; index < 2U; ++index)
    {
        json_span_t key = {adc_names[index], strlen(adc_names[index])};
        if (json_write_key(&writer, key) != JSON_OK ||
            json_write_uint32(&writer, adc_values[index]) != JSON_OK)
        {
            return 0U;
        }
    }
    if (!write_signed_fields(&writer, fields,
                             sizeof(fields) / sizeof(fields[0])) ||
        json_write_object_end(&writer) != JSON_OK ||
        json_writer_finish(&writer) != JSON_DONE)
    {
        return 0U;
    }
    output[writer.used] = '\0';
    return (uint32_t)writer.used;
}
'''.replace("FIELDS", fields.group(0))
    (BUILD / "compare_status.inc").write_text(modern, newline="\n")
    return {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in [config_path, status_path, ROOT / "Application/libs/xprintf.c",
                         ROOT / "Application/json/json.c", ROOT / "Application/json/json.h",
                         ROOT / "Application/web-server/json_config.h",
                         ROOT / "Application/system_status.h",
                         ROOT / "Application/modem_types.h", HERE / "compare.c",
                         Path(__file__).resolve()]}


def includes():
    directories = {ROOT / "Application", ROOT / "Application/json", BUILD}
    # Real project model headers, rather than host replacements of structures.
    directories.update(path.parent for path in (ROOT / "Application").rglob("*.h"))
    return [flag for path in sorted(directories) for flag in ["-I", str(path)]]


class Request(ct.Structure):
    _fields_ = [("data", ct.c_char_p), ("length", ct.c_size_t),
                ("test_case", ct.c_uint32), ("modern", ct.c_bool)]


def build_host(cc, source=None, label=""):
    extension = ".dll" if sys.platform == "win32" else ".so"
    output = BUILD / ("json_compare" + label + extension)
    sources = [HERE / "compare.c", source or ROOT / "Application/json/json.c",
               ROOT / "Application/libs/xprintf.c"]
    command = [cc, *FLAGS, "-Wno-array-parameter", "-DUNIT_TEST",
               "-DNO_CONSOLE_LOG", "-DNO_SHELL_LOG", "-O2", "-shared", *includes(),
               *map(str, sources), "-o", str(output)]
    if sys.platform != "win32":
        command += ["-fPIC", "-lm"]
    subprocess.run(command, check=True)
    api = ct.CDLL(str(output))
    api.json_compare_once.argtypes = [ct.POINTER(Request), ct.c_void_p, ct.c_size_t]
    api.json_compare_once.restype = ct.c_uint32
    api.json_compare_batch.argtypes = [ct.POINTER(Request), ct.c_uint32]
    api.json_compare_batch.restype = ct.c_uint32
    api.json_compare_apn_offset.restype = ct.c_size_t
    api.json_compare_status_pattern.argtypes = [ct.c_uint32]
    return api


def once(api, test_case, modern, text):
    request = Request(text, len(text), test_case, modern)
    output = ct.create_string_buffer(8192)
    length = api.json_compare_once(ct.byref(request), output, len(output))
    return output.raw[:length] if length else None


def fixtures():
    modbus = {"CihazID": 7, "StopBits": 2, "SonHataKodu": 0, "BaudRate": 115200}
    device = {"WebArayuzuPortu": 8080, "WebIlkVeriZamanAsimi": 300,
              "WebBostaKalmaZamanAsimi": 60, "IEC104IlkVeriZamanAsimi": 600,
              "IEC104BostaKalmaZamanAsimi": 120, "SimKartPin": 1234,
              "SimKartAPN": "internet", "SimKartAPNSifresi": "secret",
              "SimKartAPNUsername": "user", "NtpServer": "pool.ntp.org",
              "NtpServerPortu": 123, "Time": 1792700000, "TimeZone": 3,
              "PeriyodikModemResetPeriyodu": 86400, "DevreyeAlinmaZamani": 1700000000, "PreserveOutputStates": True}
    encode = lambda value: json.dumps(value, separators=(",", ":")).encode()
    nested = dict(modbus, Extra={"a": [True, None, {"ignored": list(range(16))}]})
    return [("uint32 read", 0, b"4294967295"),
            ("float read exact", 1, b"-123.125"),
            ("Modbus POST", 2, encode(modbus)),
            ("Modbus POST + unknown", 2, encode(nested)),
            ("device POST", 3, encode(device)),
            ("status GET", 4, b""),
            ("float write exact", 5, struct.pack("<f", -123.125))]


def validate(api, cases):
    for name, case, text in cases:
        old = once(api, case, False, text)
        new = once(api, case, True, text)
        assert old is not None and old == new, (name, old, new)
    # Field order, whitespace and partial updates use the actual legacy parser.
    for case in [2, 3]:
        relevant = next(text for _, kind, text in cases if kind == case)
        data = json.loads(relevant)
        for item in [{}, *({key: value} for key, value in data.items()),
                     dict(reversed(list(data.items())))]:
            text = json.dumps(item, indent=2).encode()
            assert once(api, case, False, text) == once(api, case, True, text), text
    for text in [b'{"CihazID":0}', b'{"CihazID":248}', b'{"StopBits":3}',
                 b'{"BaudRate":100}', b'{"BaudRate":4294967296}']:
        assert once(api, 2, False, text) is None
        assert once(api, 2, True, text) is None
    print("PASS equivalent results: primitives, complete/partial/reordered configs, "
          "range errors and byte-identical status JSON")


def differences(api):
    probes = [("trailing JSON", 2, b'{"CihazID":7} trailing'),
              ("leading zero", 2, b'{"CihazID":07}'),
              ("trailing comma", 2, b'{"CihazID":7,}'),
              ("Unicode APN escape", 3, b'{"SimKartAPN":"\\u0069nternet"}'),
              ("overlong APN", 3, b'{"SimKartAPN":"12345678901234567890"}'),
              ("unknown 300-byte string", 2, b'{"Extra":"' + b'x' * 300 + b'"}'),
              ("float exponent", 1, b'1e2'), ("float rounding 0.016", 1, b'0.016'),
              ("float fixed precision", 5, struct.pack("<f", 0.123456789))]
    report = []
    for name, kind, text in probes:
        old = once(api, kind, False, text)
        new = once(api, kind, True, text)
        report.append({"case": name, "old_ok": old is not None,
                       "new_ok": new is not None, "identical": old == new,
                       "old_value": display_value(api, kind, old),
                       "new_value": display_value(api, kind, new)})
    return report


def status_checks(api):
    # Integer min/max, unsigned ages > INT32_MAX, sign bits, quality masks,
    # every scalar/array field and output-capacity boundaries.
    output = ct.create_string_buffer(8192)
    for seed in [0, 0xFFFFFFFF, *range(1, 1001)]:
        api.json_compare_status_pattern(seed)
        old = once(api, 4, False, b"")
        new = once(api, 4, True, b"")
        assert old is not None and old == new, (seed, old, new)
        json.loads(new)
        if seed in [0, 0xFFFFFFFF]:
            for capacity in [0, 1, len(old) - 1, len(old), len(old) + 1]:
                for mode in [False, True]:
                    request = Request(b"", 0, 4, mode)
                    length = api.json_compare_once(ct.byref(request), output, capacity)
                    assert (length != 0) == (capacity > len(old)), (seed, capacity, mode)
    print("PASS status serializer parity: 1002 full-range snapshots + capacity boundaries")


def display_value(api, kind, value):
    if value is None:
        return None
    if kind == 1:
        return {"value": struct.unpack("<f", value)[0], "bits_le": value.hex()}
    if kind == 3:
        offset = api.json_compare_apn_offset()
        return value[offset:offset + 16].split(b"\0", 1)[0].decode()
    return value.decode() if kind == 5 else None


def timings(api, cases, baseline=None):
    # These inputs expose fast/fallback costs. The legacy parser is not an
    # exact reference for all decimals, so they are timing-only fixtures.
    cases = list(cases) + [("float short fraction", 1, b"0.016"),
                          ("float fallback mantissa", 1, b"123456789.125")]
    results = []
    runners = [(api, False), (api, True)]
    if baseline is not None:
        runners.append((baseline, True))
    for name, kind, text in cases:
        times = [[] for _ in runners]
        iterations = 20000 if kind in [3, 4] else 100000
        for round_index in range(9):
            # Rotate first runner to reduce thermal/load/order bias.
            order = [(round_index + side) % len(runners) for side in range(len(runners))]
            for side in order:
                runner, mode = runners[side]
                request = Request(text, len(text), kind, bool(mode))
                runner.json_compare_batch(ct.byref(request), 1000)
                start = time.perf_counter_ns()
                checksum = runner.json_compare_batch(ct.byref(request), iterations)
                elapsed = time.perf_counter_ns() - start
                assert checksum != 0
                times[side].append(elapsed / iterations / 1000)
        old, new = map(statistics.median, times[:2])
        row = {"case": name, "input_bytes": len(text), "production_us": round(old, 4),
               "adapter_us": round(new, 4), "new_over_old": round(new / old, 3),
               "old_range_us": [round(min(times[0]), 4), round(max(times[0]), 4)],
               "new_range_us": [round(min(times[1]), 4), round(max(times[1]), 4)],
               "iterations_per_round": iterations, "rounds": 9}
        if baseline is not None:
            before = statistics.median(times[2])
            row.update(before_us=round(before, 4),
                       before_range_us=[round(min(times[2]), 4), round(max(times[2]), 4)],
                       speedup_over_before=round(before / new, 3))
        results.append(row)
        previous = f" before={row['before_us']:.4f} us speedup={row['speedup_over_before']:.2f}" if baseline else ""
        print(f"{name:24} production={old:8.4f} us adapter={new:8.4f} us ratio={new/old:5.2f}{previous}")
    return results


def arm_sizes(compiler, source=None, label="current"):
    compiler = Path(compiler)
    prefix = str(compiler)[:-len("gcc" + compiler.suffix)]
    size = prefix + "size" + compiler.suffix
    common = [str(compiler), *FLAGS, "-Wno-array-parameter", "-DUNIT_TEST",
              "-DNO_CONSOLE_LOG", "-DNO_SHELL_LOG", "-mcpu=cortex-m33",
              "-mthumb", "-mfpu=fpv5-sp-d16", "-mfloat-abi=hard", "-Os",
              "-ffunction-sections", "-fdata-sections", "-fstack-usage",
              "--specs=nano.specs", *includes()]
    objects = []
    for unit in [HERE / "compare.c", source or ROOT / "Application/json/json.c",
                 ROOT / "Application/libs/xprintf.c"]:
        target = BUILD / ("compare_arm_" + label + "_" + unit.stem + ".o")
        subprocess.run([*common, "-c", str(unit), "-o", str(target)], check=True)
        objects.append(str(target))
    profiles = {"uint": ["uint"], "float_read": ["float"],
                "modbus": ["modbus"], "device": ["device"],
                "status": ["status"], "float_write": ["float_write"],
                "combined": ["modbus", "device", "status", "float", "float_write"]}
    results = {}
    for profile, functions in profiles.items():
        results[profile] = {}
        for mode in ["old", "new"]:
            roots = [f"compare_{mode}_{name}" for name in functions]
            for shared in ([False, True] if profile in ["combined", "status", "float_write"]
                           else [False]):
                output = BUILD / f"size_{label}_{profile}_{mode}_{int(shared)}.elf"
                retained = roots + (["xsnprintf"] if shared else [])
                linker = ["-nostartfiles", "--specs=nosys.specs", "-Wl,--gc-sections",
                          "-Wl,-e," + roots[0], *["-Wl,-u," + symbol for symbol in retained]]
                subprocess.run([*common, *objects, *linker, "-lm", "-lc", "-o", str(output)],
                               check=True, capture_output=True, text=True)
                values = subprocess.check_output([size, str(output)], text=True)
                text, data, bss = map(int, values.splitlines()[1].split()[:3])
                key = mode + ("_formatter_retained" if shared else "")
                results[profile][key] = {"text_rodata": text, "data": data,
                                         "bss": bss, "flash": text + data}
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--arm-cc")
    parser.add_argument("--baseline-source", type=Path,
                        help="Previous json.c with the same API, measured in the same process")
    args = parser.parse_args()
    hashes = prepare_sources()
    api = build_host(args.cc)
    baseline = None
    if args.baseline_source:
        baseline = build_host(args.cc, args.baseline_source.resolve(), "_before")
    cases = fixtures()
    validate(api, cases)
    if baseline is not None:
        validate(baseline, cases)
    report = {"source_sha256": hashes,
              "comparison_role": "integrated production versus independent test adapter; not pre-migration firmware",
              "environment": {"machine": platform.machine(),
                              "python": platform.python_version(),
                              "clock": time.get_clock_info("perf_counter").implementation,
                              "host_compiler": subprocess.check_output(
                                  [args.cc, "--version"], text=True).splitlines()[0]},
              "host": timings(api, cases, baseline),
              "behavior": differences(api)}
    status_checks(api)
    if baseline is not None:
        status_checks(baseline)
        report["baseline_source_sha256"] = hashlib.sha256(
            args.baseline_source.read_bytes()).hexdigest()
    if args.arm_cc:
        if baseline is not None:
            report["arm_before"] = arm_sizes(args.arm_cc, args.baseline_source, "before")
        report["arm"] = arm_sizes(args.arm_cc)
        report["environment"]["arm_compiler"] = subprocess.check_output(
            [args.arm_cc, "--version"], text=True).splitlines()[0]
        print(json.dumps(report["arm"], indent=2))
    path = BUILD / ("comparison.json" if args.arm_cc else "comparison_host.json")
    path.write_text(json.dumps(report, indent=2) + "\n")
    print("PASS comparison; report:", path)


if __name__ == "__main__":
    main()
