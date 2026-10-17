#!/usr/bin/env python3
"""Differential test of every generated routine against a reference build.

The reference is the same program generated with --reference: the IR as
built, every instruction with all of its effects (every flag), before any
optimisation. Both
are linked into one binary, the reference routines renamed ref_*, and
tests/routines/diff.c runs each routine from many states and compares them.
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
LIB = ROOT / "codegen/lib"
GAME = ROOT / "codegen/game"  # make codegen
RUNTIME = ["cpu", "video", "render", "apu", "sound", "state", "debug_chr", "rec", "common"]
INTERFACE = re.compile(r"^// in: (.*); out: (.*)$")
FUNCTION = re.compile(r"^(?:void|static uint16_t) (\w+)\(", re.M)


def run(*args, **kwargs):
    subprocess.run(args, cwd=ROOT, check=True, **kwargs)


def routines(code):
    """Generated routine names with the registers and flags their callers read."""
    found, outputs = [], set()
    for line in code.splitlines():
        interface = INTERFACE.match(line)
        if interface:
            outputs = set(name.strip() for name in interface.group(2).split(","))
            continue
        function = re.match(r"^void (\w+)\(void\) \{", line)
        if function:
            found.append((function.group(1), outputs))
        if not line.startswith("//"):
            outputs = set()
    return found


def main():
    with tempfile.TemporaryDirectory(prefix="smb-routines-") as tmp:
        tmp = pathlib.Path(tmp)
        run("moon", "run", "src/main", "--", "--rom", "smb.nes", "--map", "maps/smb.map",
            "--native", "codegen/native/routines.txt", "--reference", "--output", str(tmp / "ref"))
        reference = (tmp / "ref/code.c").read_text()
        renamed = sorted(set(FUNCTION.findall(reference)) |
                         {name for name, _ in routines(reference)} |
                         {"cpu_reset_entry", "cpu_nmi_entry"})
        (tmp / "rename.h").write_text("".join(f"#define {n} ref_{n}\n" for n in renamed))

        entries = []
        for name, outputs in routines((GAME / "code.c").read_text()):
            flags = ", ".join("true" if f in outputs else "false"
                              for f in ("a", "x", "y", "carry", "zero", "negative", "overflow"))
            entries.append(f'    {{"{name}", {name}, ref_{name}, {flags}}},\n')
        declarations = "".join(f"void ref_{name}(void);\n" for name, _ in routines(reference))
        (tmp / "routines.h").write_text(
            declarations + "static const Routine ROUTINES[] = {\n" + "".join(entries) + "};\n"
            f"#define ROUTINE_COUNT {len(entries)}\n")

        flags = ["-std=gnu99", "-O2", "-I", str(ROOT / "codegen"), "-I", str(tmp), "-I", str(GAME), "-I", str(LIB)]
        objects = []
        # reference code: its own generated headers, routines renamed
        ref_object = tmp / "ref_code.o"
        run("cc", *flags, "-I", str(tmp / "ref"), "-I", str(LIB), "-include", str(tmp / "rename.h"),
            "-c", str(tmp / "ref/code.c"), "-o", str(ref_object))
        objects.append(str(ref_object))
        sources = [GAME / "code.c", GAME / "data.c", ROOT / "codegen/native/display.c",
                   ROOT / "tests/routines/diff.c"] + [LIB / f"{n}.c" for n in RUNTIME]
        executable = tmp / "routine-diff"
        run("cc", *flags, "-o", str(executable), *map(str, sources), *objects)
        result = subprocess.run([str(executable)], cwd=ROOT)
        sys.exit(result.returncode)


if __name__ == "__main__":
    main()
