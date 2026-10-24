#!/usr/bin/env python3
"""Assembles tests/mmc1 into an iNES image: banks 0-2 at $8000 and the
fixed bank at $C000 (MMC1, 64 KiB of PRG ROM, CHR RAM).

    python3 tests/mmc1/build.py OUT.nes"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
BANKS = [("bank0", 0x8000), ("bank1", 0x8000), ("bank2", 0x8000), ("fixed", 0xc000)]


def assemble(name, base):
    with tempfile.TemporaryDirectory() as directory:
        out = pathlib.Path(directory) / "bank.bin"
        printed = subprocess.run(
            ["moon", "run", "src/inspect", "--", "--input", f"tests/mmc1/{name}.asm",
             "--output", str(out), "--bank"],
            cwd=ROOT, check=True, capture_output=True, text=True).stdout
        origin = int(printed.split("origin=")[1].split(",")[0])
        image = out.read_bytes()
    bank = bytearray(b"\xff" * 0x4000)
    bank[origin - base:origin - base + len(image)] = image
    return bytes(bank)


if __name__ == "__main__":
    prg = b"".join(assemble(name, base) for name, base in BANKS)
    header = b"NES\x1a" + bytes([len(prg) // 0x4000, 0, 0x10]) + bytes(9)
    pathlib.Path(sys.argv[1]).write_bytes(header + prg)
