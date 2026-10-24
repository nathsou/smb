#!/usr/bin/env python3
"""Compile and run the independent fixtures in isolated directories, through
both backends.

- tests/nrom128.asm: an NROM-128 program without SMB names (dispatchers,
  mirrors, overlapping instructions, indexed hardware addresses);
- the same program assembled to an iNES image and recompiled from that image
  with tests/nrom128.map, as games are;
- tests/semantics.asm: generated instruction code checked against
  independent models (tests/semantics.c).

Each harness runs natively against the generated C, and compiled into a
WebAssembly runtime module against the generated WebAssembly game module
(tests/web/fixture.mjs), with registers in locals and, structured as
dispatch loops, in memory.
"""
import pathlib
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


if __name__ == "__main__":
    check("tests/nrom128.asm", "tests/nrom128.c")
    check("tests/nrom128.asm", "tests/nrom128.c", rom_map="tests/nrom128.map")
    print("Independent NROM-128 program passed from its iNES image")
    check("tests/semantics.asm", "tests/semantics.c")
