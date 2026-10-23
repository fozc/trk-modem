"""Build a signed temporary JSON test image without editing production code.

Run after a normal Release build. Preserve its artifacts for restoration.
The hardware driver uses the existing serial_io/xmodem_send tools.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from run_checks import ROOT, HERE, BUILD, FLAGS
from run_comparison import fixtures, includes, prepare_sources


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--reuse-clean", action="store_true",
                        help="Reuse this session's saved clean image after a build retry")
    args = parser.parse_args()
    cc = Path(args.cc)
    folder = BUILD / "hardware"
    folder.mkdir(exist_ok=True)
    release = ROOT / "Release"
    clean = folder / "clean"
    if clean.exists() and not args.reuse_clean:
        raise RuntimeError("Saved clean image exists; archive the previous session "
                           "or explicitly pass --reuse-clean for this session")
    if not clean.exists():
        clean.mkdir()
        for suffix in ["elf", "bin", "map", "hex", "list"]:
            path = release / ("troika-smart-breaker-modem." + suffix)
            shutil.copy2(path, clean / path.name)
        package = max(release.glob("*.efw"), key=lambda path: path.stat().st_mtime)
        shutil.copy2(package, clean / "firmware.efw")
    prepare_sources()
    header = ["#include <stddef.h>", "#include <stdint.h>"]
    cases = fixtures()
    for index, (_, _, data) in enumerate(cases):
        values = ",".join(str(byte) for byte in data + b"\0")
        header.append(f"static const char input_{index}[] = {{{values}}};")
    header += ["static const struct {const char *data; size_t length; "
               "uint32_t test_case;} fixtures[] = {"]
    header += [f"{{input_{index},{len(data)}U,{kind}U}},"
               for index, (_, kind, data) in enumerate(cases)]
    header += ["};"]
    (folder / "fixtures.h").write_text("\n".join(header) + "\n")
    (folder / "fixtures.json").write_text(json.dumps(
        [{"index": index, "name": name} for index, (name, _, _) in enumerate(cases)],
        indent=2) + "\n")
    (folder / "unity_config.h").write_text(
        "void json_hw_putchar(int byte);\n"
        "#define UNITY_OUTPUT_CHAR json_hw_putchar\n"
        "#define UNITY_EXCLUDE_DOUBLE\n")
    unity = ROOT / "test/build/ceedling/vendor/unity/src"
    common = [str(cc), "-mcpu=cortex-m33", "-mthumb", "-mfpu=fpv5-sp-d16",
              "-mfloat-abi=hard", "-Os", "-ffunction-sections", "-fdata-sections",
              "-fstack-usage", "-DUSE_HAL_DRIVER", "-DSTM32U375xx",
              "-DUNITY_INCLUDE_CONFIG_H", *includes(), "-I", str(folder),
              "-I", str(unity), "-I", str(HERE / "target")]
    for path in ["Core/Inc", "Drivers/CMSIS/Include",
                 "Drivers/CMSIS/Device/ST/STM32U3xx/Include",
                 "Drivers/STM32U3xx_HAL_Driver/Inc"]:
        common += ["-I", str(ROOT / path)]
    objects = []

    def compile_file(source, label, defines=(), strict=True):
        output = folder / (label + ".o")
        flags = FLAGS if strict else ["-std=c11", "-Wall", "-Wextra"]
        subprocess.run([*common, *flags, "-Wno-variadic-macros", *defines, "-c", str(source),
                        "-o", str(output)], check=True)
        objects.append(output.as_posix())

    runner = ['#include "unity.h"', '#include "bsp.h"',
              "static void (*active_setup)(void);",
              "static void (*active_teardown)(void);",
              "void setUp(void) { bsp_kick_wdt(); active_setup(); }",
              "void tearDown(void) { active_teardown(); }"]
    calls = []
    total = 0
    for module in ["reader", "writer", "numbers"]:
        source = ROOT / "test/libs" / f"test_json_{module}.c"
        tests = re.findall(r"^void (test_\w+)\(void\)", source.read_text(), re.M)
        assert tests
        runner += [f"extern void json_{module}_{name}(void);"
                   for name in ["setup", "teardown"]]
        runner += [f"extern void {test}(void);" for test in tests]
        calls += [f"active_setup = json_{module}_setup;",
                  f"active_teardown = json_{module}_teardown;"]
        calls += [f'UnityDefaultTestRun({test}, "{test}", 1);' for test in tests]
        total += len(tests)
        compile_file(source, "test_" + module,
                     [f"-DsetUp=json_{module}_setup",
                      f"-DtearDown=json_{module}_teardown",
                      "-Djson_token_float=json_hw_token_float"])
    runner += ["int json_hw_run_unity(void) {", 'UnityBegin("json-on-device");',
               *calls, "return UnityEnd();", "}"]
    runner_path = folder / "runner.c"
    runner_path.write_text("\n".join(runner) + "\n")
    compile_file(runner_path, "runner")
    compile_file(unity / "unity.c", "unity", strict=False)
    compile_file(ROOT / "Application/json/json.c", "json")
    compile_file(HERE / "target/device_tests.c", "device_tests")
    compile_file(HERE / "compare.c", "compare",
                 ["-DUNIT_TEST", "-DNO_CONSOLE_LOG", "-DNO_SHELL_LOG",
                  "-Wno-array-parameter",
                  "-Dparse_modbus_config=json_hw_legacy_modbus",
                  "-Dparse_device_config=json_hw_legacy_device",
                  "-Dsystem_status_json_build=json_hw_legacy_status"])
    env = dict(os.environ)
    env["PATH"] = str(cc.parent) + os.pathsep + env["PATH"]
    subprocess.run(["make", "-C", str(release), "-j8", "main-build",
                    "USER_OBJS=" + " ".join(objects),
                    "LIBS=-Wl,--wrap=shell_init"], check=True, env=env)
    for suffix in ["elf", "bin", "map"]:
        shutil.copy2(release / ("troika-smart-breaker-modem." + suffix),
                     folder / ("test." + suffix))
    subprocess.run([sys.executable, str(ROOT / "tools/bin2efw.py"),
                    str(folder / "test.bin"), "-o", str(folder / "test.efw"),
                    "--sign-key", str(ROOT / "keys/private_key.pem"),
                    "-H", str(ROOT / "Application/version.h"),
                    "--encrypt-key", str(ROOT / "keys/aes_key.bin"),
                    "--compress", "--thumb"], check=True)
    print(f"READY: {total} Unity tests; {folder / 'test.efw'}")


if __name__ == "__main__":
    main()
