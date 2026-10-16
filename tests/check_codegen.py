#!/usr/bin/env python3
"""Checks the recompiler and the generated program.

Without a ROM: analysis tests, the independent fixtures and the ROM-free
replay of the checked-in generated code. With --rom (smb.nes in the root):
regeneration from the ROM and the map, the routine-level differential test
and the replay with the ROM's tiles."""
import argparse
import pathlib
import subprocess
import hashlib

ROOT = pathlib.Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run(args, cwd=ROOT, check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", action="store_true")
    args = parser.parse_args()
    run("moon", "check")
    run("moon", "test")
    run("python3", "tests/check_fixtures.py")
    expected = "e5c17c42783f430029ab90aa37c73d51c2c320898e533b3ff196dad38c50ae42"
    assert hashlib.sha256((ROOT / "src/smb.asm").read_bytes()).hexdigest() == expected
    run("moon", "run", "src/inspect")
    if args.rom:
        rom = (ROOT / "smb.nes").read_bytes()
        assert rom[:4] == b"NES\x1a" and rom[4] == 2
        start = 16 + (512 if rom[6] & 4 else 0)
        assert (ROOT / "codegen/prg.bin").read_bytes() == rom[start:start + 32768]
    if args.rom:
        run("make", "codegen")
        run("git", "diff", "--exit-code", "--", "codegen/lib/code.c",
            "codegen/lib/code.h", "codegen/lib/data.c", "codegen/lib/data.h",
            "codegen/lib/constants.h")
    # ROM-free replay: generated debug tiles, per-frame state oracle.
    trace = "tests/golden/warpless.trace"
    run("make", "hash")
    run("./hash", "--debug-chr", "--trace", trace)
    if args.rom:
        # Every generated routine against the unfolded (--no-fold) reference.
        run("python3", "tests/check_routines.py")
        run("./hash")
