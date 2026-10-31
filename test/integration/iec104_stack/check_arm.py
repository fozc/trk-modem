#!/usr/bin/env python3
"""Compile all new production modules with the project's Cortex-M33 setup.

Uses the saved Release include paths/defines; never edits generated makefiles
or flashes the device. Existing dependency headers are system headers because
they contain GNU macros; all new C is checked with strict C11 diagnostics.
"""

import argparse
from pathlib import Path
import re
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=shutil.which("arm-none-eabi-gcc"))
    args = parser.parse_args()
    if not args.compiler:
        parser.error("Provide --compiler or add project ARM toolchain to PATH")
    makefile = ROOT / "Release/Application/libiec104/subdir.mk"
    text = makefile.read_text(encoding="utf-8")
    command_line = next(line.strip() for line in text.splitlines()
                        if line.strip().startswith("arm-none-eabi-gcc "))
    original = shlex.split(command_line)
    flags = [flag for flag in original if flag.startswith("-D")]
    for flag in original:
        if flag.startswith("-I"):
            flags += ["-isystem", str((ROOT / "Release" / flag[2:]).resolve())]
    flags += ["-std=c11", "-pedantic", "-Wall", "-Wextra", "-Werror",
              "-Wshadow", "-Wconversion", "-Wdouble-promotion", "-Wformat=2",
              "-mcpu=cortex-m33", "-mthumb", "-mfpu=fpv5-sp-d16",
              "-mfloat-abi=hard", "-Os", "-ffunction-sections",
              "-fdata-sections", "-fstack-usage"]
    folder = ROOT / "Application/iec104_stack"
    output = ROOT / "test/build/iec104-stack/arm"
    output.mkdir(parents=True, exist_ok=True)
    sources = sorted(folder.glob("*.c"))
    sources += sorted((ROOT / "Application/iec104_modem").glob("*.c"))
    flags += ["-I" + str(folder), "-I" + str(ROOT / "Application/iec104_modem")]
    for source in sources:
        subprocess.run([args.compiler, *flags, "-c", str(source), "-o",
                        str(output / (source.stem + ".o"))], check=True)
    nm = str(Path(args.compiler).with_name("arm-none-eabi-nm.exe" if
             Path(args.compiler).suffix == ".exe" else "arm-none-eabi-nm"))
    objects = [output / (source.stem + ".o") for source in sources]
    symbols = subprocess.check_output([nm, "-u", *map(str, objects)],
                                      text=True)
    if re.search(r"\b(malloc|calloc|realloc|free|__atomic_\w+)\b", symbols):
        raise SystemExit("Unexpected heap or atomic runtime dependency")
    print(f"ARM strict C11: {len(sources)} modules passed; no heap dependency")
    for source in sources:
        usage = output / (source.stem + ".su")
        lines = usage.read_text().splitlines()
        largest = max((int(line.split("\t")[1]) for line in lines), default=0)
        print(f"{usage.stem}: largest function stack {largest} bytes")


if __name__ == "__main__":
    main()
