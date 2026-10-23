"""Run real JSON clients with conflicting C compiler architecture defaults."""

import argparse
import ctypes as ct
from pathlib import Path
import subprocess
from unittest.mock import patch

import run_checks
import run_comparison
import run_http_checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    args = parser.parse_args()
    opposite_arch = "-m32" if ct.sizeof(ct.c_void_p) == 8 else "-m64"
    real_run = subprocess.run
    compiler_calls = 0

    def run_with_compiler_default(command, *positional, **options):
        nonlocal compiler_calls
        if command[0] == args.cc:
            # Emulate the CI gcc wrapper; leave g++ defaults unchanged.
            command = [command[0], opposite_arch, *command[1:]]
            compiler_calls += 1
        return real_run(command, *positional, **options)

    with patch.object(subprocess, "run", side_effect=run_with_compiler_default):
        run_checks.configuration_checks(args.cc)
        api = run_checks.build(args.cc, False)
        assert run_checks.transcode(api, b'{"abi":32}') == b'{"abi":32}'
        run_comparison.prepare_sources()
        run_comparison.build_host(args.cc)
        with patch("sys.argv", [str(Path(run_http_checks.__file__)),
                                "--cc", args.cc]):
            run_http_checks.main()

    assert compiler_calls > 0
    print("PASS host ABI: conflicting gcc default; real C/C++ and ctypes")


if __name__ == "__main__":
    main()
