#!/usr/bin/env python3
"""Checks the recompiler and, with a ROM, the recompiled game.

Without a ROM (as in CI): the analysis tests, the independent fixtures
through the C and WebAssembly backends, the map header, and the runtime
module. With --rom (smb.nes in the root): recompile the game, replay the
recording natively against tests/golden/warpless.trace (debug and ROM
tiles) and tests/golden/warpless.audio (APU writes and samples, also
through the page's AudioWorklet), the routine-level differential test, and the replay of the split
WebAssembly build. With --metroid (metroid.nes in the root): recompile
Metroid, check the reference interpreter (tests/interpreter.c) against
tests/golden/metroid.trace over rec/metroid.rec, then the recompiled game
natively (save states loaded back along the way) and in WebAssembly, with
registers in locals and, structured as dispatch loops, in memory."""
import argparse
import pathlib
import subprocess
import tempfile

from check_fixtures import WASM_VARIANTS, waits

ROOT = pathlib.Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run(args, cwd=ROOT, check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", action="store_true")
    parser.add_argument("--metroid", action="store_true")
    args = parser.parse_args()
    run("moon", "check")
    run("moon", "test")
    run("python3", "tests/check_fixtures.py")
    run("make", "header")
    run("git", "diff", "--exit-code", "--", "codegen/native/smb.h")
    run("make", "runtime-wasm", "runtime-cartridge-wasm", "apu-wasm")
    # The synthesizer's frame queue at its edges.
    run("cc", "-std=c99", "-O2", "-Wall", "-Icodegen/lib", "-o", "audio_queue",
        "tests/audio_queue.c", "codegen/lib/apu.c")
    run("./audio_queue")
    if args.rom:
        run("make", "codegen")
        run("make", "hash")
        run("./hash", "--debug-chr", "--trace", "tests/golden/warpless.trace",
            "--audio-trace", "tests/golden/warpless.audio")
        run("./hash")
        # Every generated routine against the unoptimised IR.
        run("python3", "tests/check_routines.py")
        run("make", "game-wasm")
        run("node", "tests/web/replay_split.mjs")
        # The page's audio path: the AudioWorklet processor and apu.wasm.
        run("node", "tests/web/worklet.mjs")
    if args.metroid:
        trace = "tests/golden/metroid.trace"
        run("make", "codegen", "replay", "interpreter", "GAME=metroid")
        with tempfile.TemporaryDirectory(prefix="smb-metroid-") as directory:
            interpreted = str(pathlib.Path(directory) / "interpreter.trace")
            run("./interpreter", "metroid.nes", "--waits", waits("metroid.nes", "maps/metroid.map"),
                "--rec", "rec/metroid.rec", "--write-trace", interpreted)
            run("cmp", trace, interpreted)
            run("./replay", "metroid.nes", "--rec", "rec/metroid.rec", "--trace", trace, "--check-states", "500")
            game = str(pathlib.Path(directory) / "game.wasm")
            for variant in WASM_VARIANTS:
                run("moon", "run", "src/main", "--", "--rom", "metroid.nes", "--map", "maps/metroid.map",
                    "--output", str(pathlib.Path(directory) / "unused"), "--wasm", game, *variant)
                run("node", "tests/web/replay_cartridge.mjs", "web/runtime-cartridge.wasm", game, "metroid.nes",
                    "rec/metroid.rec", trace)
