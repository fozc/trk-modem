"""Compile atomic-support guards and exercise real SPSC thread transfers."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", default="gcc")
parser.add_argument("--arm-cc", default=None)
args = parser.parse_args()
root = Path(__file__).resolve().parents[3]
output = root / "test/build/ring_buffer"
output.mkdir(parents=True, exist_ok=True)
includes = ["-I" + str(root / directory) for directory in
            ["Application/gsm", "Application/libs"]]
sources = [str(root / directory) for directory in
           ["Application/gsm/ring_buff.c", "Application/libs/ring_buf.c"]]
warnings = ["-Wall", "-Wextra", "-Werror", "-Wshadow", "-Wconversion",
            "-Wdouble-promotion", "-Wformat=2", "-pedantic"]


def check(command, message=None):
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    if message is None:
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
    elif result.returncode == 0 or message not in result.stderr:
        raise RuntimeError("Expected compiler rejection: " + message + "\n" +
                           result.stdout + result.stderr)
    return result.stdout


for header, name in [("ring_buff.h", "ring_buff"), ("ring_buf.h", "RingBuf")]:
    probe = output / (name + "_probe.c")
    probe.write_text(f'#include "{header}"\n', encoding="ascii")
    base = [args.cc, *includes, "-fsyntax-only", str(probe)]
    check(base + ["-std=c11", *warnings])
    check(base + ["-std=c99"], name + " requires C11 atomics")
    check(base + ["-std=c11", "-D__STDC_NO_ATOMICS__=1"],
          name + " requires <stdatomic.h>")
    empty = output / "without_atomics"
    empty.mkdir(exist_ok=True)
    for stub in ["stdint.h", "stdbool.h"]:
        (empty / stub).write_text("", encoding="ascii")
    check([args.cc, *includes, "-std=c11", "-E", "-nostdinc",
           "-I" + str(empty), str(probe)],
          name + " requires the <stdatomic.h> header")
    print(f"PASS: {name} accepts C11 and rejects C99/no atomics/missing header")

for optimization in ["-O0", "-O2"]:
    executable = output / ("threads_" + optimization[1:] + ".exe")
    check([args.cc, "-std=c11", *warnings, optimization, "-pthread",
           *includes, str(root / "test/integration/libs/test_ring_buffer_threads.c"),
           *sources, "-o", str(executable)])
    print(optimization + ": " + check([str(executable)]).strip())

if args.arm_cc:
    for cpu, accepted in [("cortex-m0", False), ("cortex-m0plus", False),
                          ("cortex-m23", True), ("cortex-m3", True),
                          ("cortex-m4", True), ("cortex-m7", True),
                          ("cortex-m33", True)]:
        for name in ["ring_buff", "RingBuf"]:
            probe = output / (name + "_probe.c")
            command = [args.arm_cc, "-std=c11", *warnings, "-mthumb",
                       "-mcpu=" + cpu, *includes, "-fsyntax-only", str(probe)]
            check(command, None if accepted else name + " requires lock-free")
        print(f"PASS: {cpu} " + ("accepted" if accepted else
                                  "rejected: index atomics are not lock-free"))
    for source in sources:
        stem = Path(source).stem
        for optimization in ["-O0", "-O2"]:
            assembly = output / (stem + optimization[1:] + "_m33.s")
            check([args.arm_cc, "-std=c11", *warnings, "-mthumb",
                   "-mcpu=cortex-m33", optimization, *includes, "-S", source,
                   "-o", str(assembly)])
            contents = assembly.read_text(encoding="utf-8")
            for forbidden in ["__atomic_", "__sync_", "__aeabi_uidiv", "udiv",
                              "sdiv", "ldrex", "strex", "cpsid"]:
                if forbidden in contents:
                    raise RuntimeError(f"Unexpected {forbidden} in {assembly}")
            if "lda" not in contents or "stl" not in contents:
                raise RuntimeError(f"Missing acquire/release instructions: {assembly}")
    print("PASS: M33 O0/O2 assembly: acquire/release, no atomic runtime/division/RMW/IRQ mask")
else:
    print("SKIP: ARM CPU/assembly checks (supply --arm-cc)")
