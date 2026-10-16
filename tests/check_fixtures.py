#!/usr/bin/env python3
"""Compile and run the independent fixtures in isolated directories.

- tests/nrom128.asm: an NROM-128 program without SMB names (dispatchers,
  mirrors, overlapping instructions, indexed hardware addresses);
- the same program assembled to an iNES image and recompiled from that image
  with tests/nrom128.map, as games are;
- tests/semantics.asm: generated instruction C checked against independent
  models (tests/semantics.c).
"""
import pathlib
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
GENERATED = {"code.c", "code.h", "data.c", "data.h", "constants.h"}
# Generic programs reach the PPU through its register interface.
SUPPORT = ROOT / "tests/support"


def run(*args):
    subprocess.run(args, cwd=ROOT, check=True)


def check(source, harness, rom_map=None):
    with tempfile.TemporaryDirectory(prefix="smb-fixture-") as directory:
        output = pathlib.Path(directory)
        if rom_map:
            prg = output / "prg.bin"
            run("moon", "run", "src/inspect", "--", "--input", source, "--output", str(prg))
            image = prg.read_bytes()
            rom = output / "fixture.nes"
            header = b"NES\x1a" + bytes([len(image) // 16384, 1]) + bytes(10)
            rom.write_bytes(header + image + bytes(8192))
            run("moon", "run", "src/main", "--", "--rom", str(rom), "--map", rom_map, "--output", str(output))
        else:
            run("moon", "run", "src/main", "--", "--input", source, "--output", str(output))
        # Compile with precisely the same runtime as SMB, plus the PPU adapter.
        for path in list((ROOT / "codegen/lib").iterdir()) + list(SUPPORT.iterdir()):
            if path.name not in GENERATED and path.suffix in {".h", ".c"}:
                shutil.copyfile(path, output / "lib" / path.name)
        shutil.copyfile(ROOT / harness, output / "main.c")
        sources = ["cpu", "code", "data", "video", "ppu_bus", "apu"]
        executable = str(output / "fixture")
        run("cc", "-std=c99", "-O2", "-DPPU_BUS", "-o", executable, str(output / "main.c"),
            *(str(output / "lib" / (name + ".c")) for name in sources))
        run(executable)


if __name__ == "__main__":
    check("tests/nrom128.asm", "tests/nrom128.c")
    check("tests/nrom128.asm", "tests/nrom128.c", rom_map="tests/nrom128.map")
    print("Independent NROM-128 program passed from its iNES image")
    check("tests/semantics.asm", "tests/semantics.c")
