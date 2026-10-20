"""Build an isolated signed bench fixture, restoring source in finally.

Requires the project's configured ARM toolchain and generated Release
makefiles. The output is dangerous test firmware, not a production image.
"""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def fixture_sources(original):
    bsp, app, make = original
    marker = b"void bsp_init()"
    assert bsp.count(marker) == 1, "unexpected BSP init layout"
    bsp = bsp.replace(marker,
        b'#include "../../test/system/device_fault/fixture.h"\n\n' + marker, 1)
    marker = b"\tapp_ipc_init();"
    assert app.count(marker) == 1, "unexpected shell init layout"
    app = app.replace(marker, marker +
        b"\n    extern void hw_fault_fixture_init(void);\n"
        b"    hw_fault_fixture_init();", 1)
    marker = b"-Wl,--gc-sections"
    assert make.count(marker) == 1, "unexpected linker recipe"
    make = make.replace(marker, marker +
        b" -Wl,--wrap=HAL_UART_AbortTransmit -Wl,--wrap=HAL_DMA_DeInit", 1)
    return bsp, app, make


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--outdir", type=Path, required=True)
    args = parser.parse_args()
    args.outdir.mkdir(parents=True, exist_ok=False)
    paths = [ROOT / name for name in ("Application/bsp/bsp.c",
             "Application/app_main.c", "Release/makefile")]
    original = [path.read_bytes() for path in paths]
    modified = fixture_sources(original)
    manifest = {"test_only": True, "production_image": False,
                "source_sha256": {str(path.relative_to(ROOT)):
                    hashlib.sha256(data).hexdigest()
                    for path, data in zip(paths, original)}}
    try:
        for path, data in zip(paths, modified):
            path.write_bytes(data)
        with (args.outdir / "build.log").open("wb") as log:
            subprocess.run(["make", "-C", str(ROOT / "Release"),
                            "-j8", "all"], cwd=ROOT, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
        latest = max((ROOT / "Release").glob("*.efw"),
                     key=lambda path: path.stat().st_mtime_ns)
        shutil.copy2(latest, args.outdir / "fixture.efw")
        shutil.copy2(ROOT / "Release/troika-smart-breaker-modem.elf",
                     args.outdir / "fixture.elf")
        shutil.copy2(Path(__file__).with_name("fixture.h"),
                     args.outdir / "fixture-tested.h")
        manifest["efw_sha256"] = hashlib.sha256(latest.read_bytes()).hexdigest()
    finally:
        for path, data in zip(paths, original):
            path.write_bytes(data)
        manifest["sources_restored"] = all(path.read_bytes() == data
                                           for path,data in zip(paths, original))
        (args.outdir / "manifest.json").write_text(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
