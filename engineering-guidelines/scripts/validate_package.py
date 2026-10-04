#!/usr/bin/env python3
"""Check this instruction package and optional host examples.

No external Python packages are required. This is not a firmware audit.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
SKILLS = (
    "embedded-design",
    "embedded-verification",
    "embedded-debugging",
    "embedded-project-intake",
)
REQUIRED = (
    "AGENTS.md", "KULLANIM.md", "hardware.md", "architecture.md",
    "DOGRULAMA.md", "references/documentation.md",
    "skills/embedded-design/references/c-rules.md",
    "skills/embedded-design/references/atomic-isr.md",
    "skills/embedded-design/references/cpp-subset.md",
    "examples/atomic_mailbox.c.txt", "examples/alignment.cpp.txt",
)


def validate_structure() -> None:
    errors = []
    for relative in REQUIRED:
        if not (ROOT / relative).is_file():
            errors.append(f"Missing file: {relative}")
    for path in ROOT.rglob("*"):
        if not path.is_file() or any(
            part in (".validation", "__pycache__") for part in path.parts
        ):
            continue
        data = path.read_bytes()
        try:
            content = data.decode("utf-8")
        except UnicodeDecodeError:
            errors.append(f"Not UTF-8: {path.relative_to(ROOT)}")
            continue
        if data.startswith(b"\xef\xbb\xbf") or b"\r" in data:
            errors.append(f"Expected UTF-8 without BOM and LF: {path.name}")
        if not data.endswith(b"\n"):
            errors.append(f"No final newline: {path.name}")
        if path.name.endswith((".c.txt", ".cpp.txt")):
            if not content.isascii():
                errors.append(f"Non-ASCII example: {path.name}")
        if path.suffix != ".md":
            continue
        prose = re.sub(r"(?ms)^```.*?^```[^\n]*", "", content)
        for target in re.findall(r"\]\(([^)]+)\)", prose):
            target = target.strip().strip("<>")
            if "://" in target or target.startswith("#"):
                continue
            destination = unquote(target.split("#", 1)[0])
            if not (path.parent / destination).resolve().exists():
                errors.append(f"Broken link: {path.relative_to(ROOT)} -> {target}")
    for name in SKILLS:
        path = ROOT / "skills" / name / "SKILL.md"
        if not path.is_file():
            errors.append(f"Missing skill: {name}")
            continue
        match = re.match(
            r"\A---\nname: ([a-z0-9-]+)\ndescription: ([^\n]+)\n---\n",
            path.read_text(encoding="utf-8"),
        )
        if not match or match.group(1) != name:
            errors.append(f"Invalid name/description frontmatter: {name}")
    if errors:
        raise SystemExit("\n".join(errors))
    print("PASS: files, four skill entrypoints, local links, UTF-8/LF, ASCII examples")


def run_examples() -> None:
    build = ROOT / ".validation"
    build.mkdir(exist_ok=True)
    common = [
        "-Wall", "-Wextra", "-Werror", "-Wshadow", "-Wconversion",
        "-Wdouble-promotion", "-Wformat=2", "-pedantic", "-O2",
    ]
    cases = (
        ("gcc", "c", "-std=c11", "atomic_mailbox.c.txt", "mailbox", []),
        ("g++", "c++", "-std=c++20", "alignment.cpp.txt", "alignment",
         ["-fno-exceptions", "-fno-rtti"]),
    )
    for compiler, language, standard, source, name, extra in cases:
        executable = shutil.which(compiler)
        if executable is None:
            raise SystemExit(f"Required compiler unavailable: {compiler}")
        version = subprocess.run(
            [executable, "--version"], check=True, text=True,
            capture_output=True,
        ).stdout.splitlines()[0]
        output = build / (name + ".exe")
        command = [
            executable, "-x", language, standard, *common, *extra,
            str(ROOT / "examples" / source), "-o", str(output),
        ]
        print(version, flush=True)
        print(subprocess.list2cmdline(command), flush=True)
        subprocess.run(command, check=True, timeout=60)
        subprocess.run([str(output)], check=True, timeout=10)
        print(f"PASS: {source}", flush=True)
    print("Scope: sequential host contracts and host alignment only; no target ISR/DMA proof")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--examples", action="store_true")
    args = parser.parse_args()
    validate_structure()
    if args.examples:
        run_examples()


if __name__ == "__main__":
    main()
