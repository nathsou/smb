#!/usr/bin/env python3
"""Checks the recompiler and, with a ROM, the recompiled game.

Without a ROM (as in CI): the analysis tests, the independent fixtures
through the C and WebAssembly backends, the map header, and the runtime
module. With --rom (smb.nes in the root): recompile the game, replay the
recording natively against tests/golden/warpless.trace (debug and ROM
tiles), the routine-level differential test, and the replay of the split
WebAssembly build."""
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
    run("python3", "tests/check_fixtures.py")
    run("make", "header")
    run("git", "diff", "--exit-code", "--", "codegen/native/smb.h")
    run("make", "runtime-wasm")
    if args.rom:
        run("make", "codegen")
        run("make", "hash")
        run("./hash", "--debug-chr", "--trace", "tests/golden/warpless.trace")
        run("./hash")
        # Every generated routine against the unoptimised IR.
        run("python3", "tests/check_routines.py")
        run("make", "game-wasm")
        run("node", "tests/web/replay_split.mjs")
