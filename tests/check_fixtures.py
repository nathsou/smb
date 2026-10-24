#!/usr/bin/env python3
"""Compile and run the independent fixtures in isolated directories, through
both backends.

- tests/nrom128.asm: an NROM-128 program without SMB names (dispatchers,
  mirrors, overlapping instructions, indexed hardware addresses);
- the same program assembled to an iNES image and recompiled from that image
  with tests/nrom128.map, as games are;
- tests/semantics.asm: generated instruction code checked against
  independent models (tests/semantics.c);
- tests/mmc1/*.asm: a program for an MMC1 board (banks switched in at $8000,
  the window reached from the fixed bank, computed jumps, a wait in a
  subroutine, a call that returns past its caller), recompiled from its
  iNES image with tests/mmc1/mmc1.map, and compared frame by frame with the
  reference interpreter (tests/interpreter.c), natively and in WebAssembly.

Each harness runs natively against the generated C, and compiled into a
WebAssembly runtime module against the generated WebAssembly game module
(tests/web/fixture.mjs), with registers in locals and, structured as
dispatch loops, in memory.
"""
import pathlib
import re
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
GENERATED = {"code.c", "code.h", "vars.h", "data.c", "data.h", "constants.h"}
WASM_VARIANTS = [[], ["--wasm-dispatch", "--wasm-memory-vars"]]


def run(*args):
    subprocess.run(args, cwd=ROOT, check=True)


def check(source, harness, rom_map=None):
    with tempfile.TemporaryDirectory(prefix="smb-fixture-") as directory:
        output = pathlib.Path(directory)
        lib = output / "lib"
        # The fixture as an iNES image: the WebAssembly build loads its PRG.
        prg = output / "prg.bin"
        run("moon", "run", "src/inspect", "--", "--input", source, "--output", str(prg))
        image = prg.read_bytes()
        rom = output / "fixture.nes"
        header = b"NES\x1a" + bytes([len(image) // 16384, 1]) + bytes(10)
        rom.write_bytes(header + image + bytes(8192))
        program = ["--rom", str(rom), "--map", rom_map] if rom_map else ["--input", source]
        run("moon", "run", "src/main", "--", *program, "--output", str(lib))
        # Compile with precisely the same runtime as SMB, plus the PPU adapter.
        for path in (ROOT / "codegen/lib").iterdir():
            if path.name not in GENERATED and path.suffix in {".h", ".c"}:
                shutil.copyfile(path, lib / path.name)
        shutil.copyfile(ROOT / harness, output / "main.c")
        sources = ["cpu", "code", "data", "video", "ppu_bus", "apu", "sound"]
        executable = str(output / "fixture")
        run("cc", "-std=c99", "-O2", "-DPPU_BUS", "-o", executable, str(output / "main.c"),
            *(str(lib / (name + ".c")) for name in sources))
        run(executable)

        runtime = output / "runtime.wasm"
        run("clang", "-O2", "--target=wasm32", "-nostdlib", "-DPPU_BUS",
            "-I", str(ROOT / "tests/web/shim"), "-I", str(output), "-I", str(lib),
            "-Wl,--import-memory", "-Wl,--export-all", "-Wl,--no-entry", "-Wl,--allow-undefined",
            "-o", str(runtime), str(output / "main.c"),
            *(str(lib / (name + ".c")) for name in ["cpu", "video", "ppu_bus", "apu", "sound", "prg"]))
        for variant in WASM_VARIANTS:
            game = output / "game.wasm"
            run("moon", "run", "src/main", "--", *program, "--output", str(output / "unused"),
                "--wasm", str(game), *variant)
            run("node", "tests/web/fixture.mjs", str(runtime), str(game), str(rom))


# The runtime of programs that drive the PPU registers and switch banks.
CARTRIDGE = ["cpu", "video", "render", "ppu_bus", "mapper", "apu", "sound", "cartridge", "rec", "state_hash"]


def waits(rom, rom_map):
    """The wait loops the recompiler finds, as the interpreter takes them."""
    printed = subprocess.run(["moon", "run", "src/inspect", "--", "--rom", rom, "--map", rom_map],
                             cwd=ROOT, check=True, capture_output=True, text=True).stdout
    # BANK:FROM:TO for a wait in a bank
    found = re.findall(r"Wait loop at (?:\d+:)?\$([0-9a-f]+), back from (?:(\d+):)?\$([0-9a-f]+)", printed)
    return ",".join((f"{bank}:" if bank else "") + f"{back}:{start}" for start, bank, back in found)


def check_banked(frames=200):
    with tempfile.TemporaryDirectory(prefix="smb-fixture-") as directory:
        output = pathlib.Path(directory)
        lib = output / "lib"
        rom = str(output / "mmc1.nes")
        rom_map = "tests/mmc1/mmc1.map"
        run("python3", "tests/mmc1/build.py", rom)
        run("moon", "run", "src/main", "--", "--rom", rom, "--map", rom_map, "--output", str(lib))
        for path in (ROOT / "codegen/lib").iterdir():
            if path.name not in GENERATED and path.suffix in {".h", ".c"}:
                shutil.copyfile(path, lib / path.name)
        shutil.copyfile(ROOT / "tests/mmc1.c", output / "main.c")
        flags = ["-std=c99", "-O2", "-DPPU_BUS", "-DMAPPER", "-I", str(lib)]
        lib_sources = lambda names: [str(lib / (name + ".c")) for name in names]
        executable = str(output / "fixture")
        run("cc", *flags, "-o", executable, str(output / "main.c"), *lib_sources(["code", "data"] + CARTRIDGE))
        run(executable)
        # the recompiled program and the interpreter, frame by frame
        replay = str(output / "replay")
        interpreter = str(output / "interpreter")
        trace = str(output / "interpreter.trace")
        run("cc", *flags, "-o", replay, "codegen/replay.c", *lib_sources(["code", "data", "state"] + CARTRIDGE))
        run("cc", *flags, "-o", interpreter, "tests/interpreter.c", *lib_sources(CARTRIDGE + ["prg"]))
        run(interpreter, rom, "--waits", waits(rom, rom_map), "--frames", str(frames), "--write-trace", trace)
        run(replay, rom, "--frames", str(frames), "--trace", trace)

        runtime = output / "runtime.wasm"
        run("clang", "-O2", "--target=wasm32", "-nostdlib", "-DPPU_BUS", "-DMAPPER",
            "-I", str(ROOT / "tests/web/shim"), "-I", str(output), "-I", str(lib),
            "-Wl,--import-memory", "-Wl,--export-all", "-Wl,--no-entry", "-Wl,--allow-undefined",
            "-o", str(runtime), str(output / "main.c"),
            *lib_sources(["cpu", "video", "ppu_bus", "mapper", "apu", "sound", "prg"]))
        for variant in WASM_VARIANTS:
            game = output / "game.wasm"
            run("moon", "run", "src/main", "--", "--rom", rom, "--map", rom_map,
                "--output", str(output / "unused"), "--wasm", str(game), *variant)
            run("node", "tests/web/fixture.mjs", str(runtime), str(game), rom)
            run("node", "tests/web/replay_cartridge.mjs", "web/runtime-cartridge.wasm", str(game), rom,
                "/dev/null", trace)


if __name__ == "__main__":
    check("tests/nrom128.asm", "tests/nrom128.c")
    check("tests/nrom128.asm", "tests/nrom128.c", rom_map="tests/nrom128.map")
    print("Independent NROM-128 program passed from its iNES image")
    check("tests/semantics.asm", "tests/semantics.c")
    run("make", "runtime-cartridge-wasm")
    check_banked()
