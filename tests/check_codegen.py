#!/usr/bin/env python3
"""ROM-free regeneration check; --rom also checks the recorded playthrough."""
import argparse
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run(args, cwd=ROOT, check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", action="store_true")
    args = parser.parse_args()
    run("moon", "check")
    run("moon", "test")
    run("make", "codegen")
    run("git", "diff", "--exit-code", "--", "codegen/lib/code.c",
        "codegen/lib/code.h", "codegen/lib/data.c", "codegen/lib/data.h",
        "codegen/lib/constants.h")
    run("make", "hash")
    if args.rom:
        run("./hash")
