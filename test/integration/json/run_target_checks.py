"""Repeatable standalone Cortex-M33 size, stack and dependency gates."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess

from run_checks import BUILD, FLAGS, ROOT


def command(arguments):
    return subprocess.check_output([str(arg) for arg in arguments], text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", required=True)
    args = parser.parse_args()
    compiler = Path(args.cc)
    extension = compiler.suffix
    prefix = str(compiler)[:-len("gcc" + extension)]
    size = prefix + "size" + extension
    nm = prefix + "nm" + extension
    BUILD.mkdir(parents=True, exist_ok=True)
    common = [str(compiler), *FLAGS, "-mcpu=cortex-m33", "-mthumb",
              "-mfpu=fpv5-sp-d16", "-mfloat-abi=hard", "-ffunction-sections",
              "-fdata-sections", "-fstack-usage", "-Wstack-usage=512"]
    report = {}
    for optimization in ["Os", "O2"]:
        target = BUILD / ("json_arm_" + optimization + ".o")
        subprocess.run([*common, "-" + optimization, "-c",
                        str(ROOT / "libraries/json/json.c"), "-o", str(target)],
                       check=True)
        fields = command([size, target]).splitlines()[1].split()
        text_bytes, data_bytes, bss_bytes = map(int, fields[:3])
        dependencies = [line.split()[-1] for line in
                        command([nm, "-u", target]).splitlines()]
        # fegetround reads FPSCR in this toolchain; it has no undefined
        # symbols, heap/errno use or floating-point environment writes.
        allowed = {"fegetround", "memchr", "memcmp", "memcpy", "memmove",
                   "memset", "strlen"}
        assert set(dependencies) <= allowed, dependencies
        assert text_bytes <= 12288, text_bytes
        assert data_bytes == 0 and bss_bytes == 0, fields
        frames = {}
        for line in target.with_suffix(".su").read_text().splitlines():
            symbol, frame, kind = line.split("\t")
            assert kind == "static", line
            assert int(frame) <= 512, line
            frames[symbol.split(":")[-1]] = int(frame)
        report[optimization] = {
            "object_text_rodata_bytes": text_bytes, "data_bytes": data_bytes,
            "bss_bytes": bss_bytes, "largest_frame_bytes": max(frames.values()),
            "frames_over_128": {name: frame for name, frame in frames.items()
                                if frame > 128},
            "undefined_symbols": dependencies,
        }
    # Symbol sizes provide target ABI sizes, independent of host word width.
    probe = BUILD / "json_sizes.c"
    probe.write_text('#include "json.h"\n'
                     'const char reader_size[sizeof(json_reader_t)] = {0};\n'
                     'const char writer_size[sizeof(json_writer_t)] = {0};\n')
    target = BUILD / "json_sizes.o"
    subprocess.run([*common, "-I", str(ROOT / "libraries/json"), "-c",
                    str(probe), "-o", str(target)], check=True)
    for line in command([nm, "-S", target]).splitlines():
        fields = line.split()
        if len(fields) == 4 and fields[3] in {"reader_size", "writer_size"}:
            report[fields[3] + "_bytes"] = int(fields[1], 16)
    assert report["reader_size_bytes"] <= 192
    assert report["writer_size_bytes"] <= 192
    disabled = {}
    for optimization in ["Os", "O2"]:
        target = BUILD / ("json_no_float_" + optimization + ".o")
        subprocess.run([*common, "-DJSON_ENABLE_FLOAT=0", "-" + optimization,
                        "-c", str(ROOT / "libraries/json/json.c"),
                        "-o", str(target)], check=True)
        fields = command([size, target]).splitlines()[1].split()
        text_bytes, data_bytes, bss_bytes = map(int, fields[:3])
        dependencies = [line.split()[-1] for line in
                        command([nm, "-u", target]).splitlines()]
        assert set(dependencies) <= allowed - {"fegetround"}, dependencies
        symbols = command([nm, target])
        for name in ["json_token_float", "json_write_float",
                     "json_write_float_fixed"]:
            assert name not in symbols, name
        frames = [int(line.split("\t")[1]) for line in
                  target.with_suffix(".su").read_text().splitlines()]
        assert max(frames) <= 512
        assert data_bytes == 0 and bss_bytes == 0
        assert text_bytes < report[optimization]["object_text_rodata_bytes"]
        disabled[optimization] = {
            "object_text_rodata_bytes": text_bytes,
            "largest_frame_bytes": max(frames),
            "undefined_symbols": dependencies,
        }
    target = BUILD / "json_sizes_no_float.o"
    subprocess.run([*common, "-DJSON_ENABLE_FLOAT=0", "-I",
                    str(ROOT / "libraries/json"), "-c", str(probe),
                    "-o", str(target)], check=True)
    for line in command([nm, "-S", target]).splitlines():
        fields = line.split()
        if len(fields) == 4 and fields[3] in {"reader_size", "writer_size"}:
            disabled[fields[3] + "_bytes"] = int(fields[1], 16)
    assert disabled["reader_size_bytes"] == report["reader_size_bytes"]
    assert disabled["writer_size_bytes"] < report["writer_size_bytes"]
    report["float_disabled"] = disabled
    print(command([str(compiler), "--version"]).splitlines()[0])
    print(json.dumps(report, indent=2))
    (BUILD / "target_metrics.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
