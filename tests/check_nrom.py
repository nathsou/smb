#!/usr/bin/env python3
"""Compile and execute an independent NROM program in an isolated directory."""
import pathlib
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run(args, cwd=ROOT, check=True)


with tempfile.TemporaryDirectory(prefix="smb-nrom-") as directory:
    output = pathlib.Path(directory)
    run("moon", "run", "src/main", "--", "--input", "tests/nrom128.asm",
        "--output", str(output))
    # Compile the fixture with precisely the same native runtime as SMB.
    generated = {"code.c", "code.h", "data.c", "data.h", "constants.h"}
    for path in (ROOT / "codegen/lib").iterdir():
        if path.name not in generated and path.suffix in {".h", ".c"}:
            shutil.copyfile(path, output / "lib" / path.name)
    shutil.copyfile(ROOT / "tests/nrom128.c", output / "main.c")
    sources = ["instructions", "cpu", "code", "data", "ppu", "apu"]
    executable = str(output / "nrom-test")
    run("cc", "-std=c99", "-O2", "-o", executable, str(output / "main.c"),
        *(str(output / "lib" / (name + ".c")) for name in sources))
    run(executable)
