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
registers in locals and, structured as dispatch loops, in memory. With
--smb3 (smb3.nes in the root): the same for Super Mario Bros. 3, over
rec/smb3.rec against tests/golden/smb3.trace, and likewise --bomberman,
--tetris, --zelda, --chessmaster (with preemption, as its map says) and
--kirby. For each, the page's WebGL renderer is compared with the software
renderer and the themes are drawn (tests/web/cartridge_gpu.cjs,
Playwright), and for SMB3 the desktop GPU renderer too (under Xvfb)."""
import argparse
import pathlib
import re
import subprocess
import tempfile

from check_fixtures import WASM_VARIANTS, waits

ROOT = pathlib.Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run(args, cwd=ROOT, check=True)


# The cartridges (their ROMs in the root as NAME.nes, their maps in maps/,
# their recordings in rec/, the interpreter's traces in tests/golden/): the
# frames the page's renderers are compared at.
CARTRIDGES = {
    "metroid": "1200,3000",
    "smb3": "1950,2600",
    "bomberman": "300,800",
    "tetris": "1000,2500",
    "zelda": "1200,2200",
    "chessmaster": "1700,4300",
    "kirby": "1500,3000",
}


def check_cartridge(name, frames):
    """The reference interpreter against the game's golden trace over its
    recording (preempted where the map says), then the recompiled game
    natively (save states loaded back along the way) and in WebAssembly,
    with registers in locals and, structured as dispatch loops, in memory,
    and the page's WebGL renderer against the software one, with the
    themes."""
    rom, rom_map, recording = f"{name}.nes", f"maps/{name}.map", f"rec/{name}.rec"
    trace = f"tests/golden/{name}.trace"
    run("make", "codegen", "replay", "interpreter", f"GAME={name}")
    preempt = re.findall(r"^preempt\s+(\d+)", (ROOT / rom_map).read_text(), re.M)
    polls = ["--preempt", preempt[0], "--polls", f"codegen/{name}/polls.txt"] if preempt else []
    with tempfile.TemporaryDirectory(prefix=f"smb-{name}-") as directory:
        interpreted = str(pathlib.Path(directory) / "interpreter.trace")
        run("./interpreter", rom, "--waits", waits(rom, rom_map), "--rec", recording,
            "--write-trace", interpreted, *polls)
        run("cmp", trace, interpreted)
        run("./replay", rom, "--rec", recording, "--trace", trace, "--check-states", "500")
        game = str(pathlib.Path(directory) / "game.wasm")
        for variant in WASM_VARIANTS:
            run("moon", "run", "src/main", "--", "--rom", rom, "--map", rom_map,
                "--output", str(pathlib.Path(directory) / "unused"), "--wasm", game, *variant)
            run("node", "tests/web/replay_cartridge.mjs", "web/runtime-cartridge.wasm", game, rom,
                recording, trace)
        # the page's WebGL renderer against the software one, and the themes
        run("node", "tests/web/cartridge_gpu.cjs", game, rom, recording, rom_map, "8", frames)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", action="store_true")
    for name in CARTRIDGES:
        parser.add_argument(f"--{name}", action="store_true")
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
    for name, frames in CARTRIDGES.items():
        if getattr(args, name):
            check_cartridge(name, frames)
    if args.smb3:
        # the GPU renderer (web/video.frag) against the software renderer
        run("make", "gpu-check", "GAME=smb3")
        run("xvfb-run", "-a", "./gpu_check", "--every", "4")
