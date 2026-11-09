#!/usr/bin/env python3
"""Differential test of every generated routine against a reference build.

The reference is the same program generated with --reference: the IR as
built, every instruction with all of its effects (every flag), before any
optimisation. Both
are linked into one binary, the reference routines renamed ref_*, and
tests/routines/diff.c runs each routine from many states and compares them.
With registers as locals (the game generated with --locals), each routine is
run through its adapter in machine.c, which takes its parameters from the
machine's registers and writes its results back.
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


def routines(code, prefix=""):
    """Generated routine names with the registers and flags their callers
    read (the functions named with `prefix`, which it is stripped of)."""
    found, outputs = [], set()
    for line in code.splitlines():
        interface = INTERFACE.match(line)
        if interface:
            outputs = set(name.strip() for name in interface.group(2).split(","))
            continue
        function = re.match(rf"^void {prefix}(\w+)\(void\) \{{", line)
        if function:
            found.append((function.group(1), outputs))
        if not line.startswith("//"):
            outputs = set()
    return found


def check_native_interface():
    """The hand-written routines (codegen/native) call and are called with the
    signatures --locals gives them: routines.h, committed with the adapters
    for a game that keeps the machine's registers (machine_calls.c), is the
    one generated with the game, and agrees with code.h."""
    for name in ["routines.h", "machine_calls.c"]:
        if (ROOT / "codegen/native" / name).read_text() != (GAME / name).read_text():
            sys.exit(f"codegen/native/{name} is not the generated one: run make native-interface")
    with tempfile.TemporaryDirectory(prefix="smb-interface-") as tmp:
        both = pathlib.Path(tmp) / "both.c"
        both.write_text('#include "code.h"\n#include "routines.h"\n')
        run("cc", "-std=gnu99", "-fsyntax-only", "-I", str(GAME), "-I", str(ROOT / "codegen/lib"),
            "-I", str(ROOT / "codegen/native"), str(both))


def main():
    check_native_interface()
    with tempfile.TemporaryDirectory(prefix="smb-routines-") as tmp:
        tmp = pathlib.Path(tmp)
        # with locals, the program under test is generated again with its
        # computed accesses checked (--check-scratch), as make codegen's
        game = GAME
        if (GAME / "machine.c").exists():
            game = tmp / "game"
            run("moon", "run", "src/main", "--", "--rom", "smb.nes", "--map", "maps/smb.map",
                "--native", "codegen/native/routines.txt", "--locals", "--check-scratch",
                "--output", str(game))
        run("moon", "run", "src/main", "--", "--rom", "smb.nes", "--map", "maps/smb.map",
            "--native", "codegen/native/routines.txt", "--reference", "--output", str(tmp / "ref"))
        reference = (tmp / "ref/code.c").read_text()
        renamed = sorted(set(FUNCTION.findall(reference)) |
                         {name for name, _ in routines(reference)} |
                         {"cpu_reset_entry", "cpu_nmi_entry"})
        (tmp / "rename.h").write_text("".join(f"#define {n} ref_{n}\n" for n in renamed))

        # (symbol, routine, outputs): with registers as locals, the routines
        # through their adapters, and the entry points as generated
        code = (game / "code.c").read_text()
        adapters = game / "machine.c"
        adapter_text = adapters.read_text() if adapters.exists() else ""
        if adapters.exists():
            current = [(f"machine_{name}", name, outputs)
                       for name, outputs in routines(adapters.read_text(), "machine_")]
            current += [(name, name, outputs) for name, outputs in routines(code)
                        if name.startswith("cpu_") and name.endswith("_entry")]
        else:
            current = [(name, name, outputs) for name, outputs in routines(code)]
        entries = []
        for symbol, name, outputs in current:
            flags = ", ".join("true" if f in outputs else "false"
                              for f in ("a", "x", "y", "carry", "zero", "negative", "overflow"))
            # with locals, the scratch bytes it gives back (t0 to t7); else all
            scratch = sum(1 << k for k in range(8) if f"t{k}" in outputs) if adapters.exists() else 0xff
            enter = f"enter_{name}" if f"void enter_{name}(void)" in adapter_text else "NULL"
            entries.append(f'    {{"{name}", {symbol}, ref_{name}, {flags}, {scratch:#04x}, {enter}}},\n')
        declarations = "".join(f"void ref_{name}(void);\n" for name, _ in routines(reference))
        declarations += "".join(f"void {symbol}(void);\n" for symbol, name, _ in current if symbol != name)
        declarations += "".join(f"void enter_{name}(void);\n" for _, name, _ in current
                                if f"void enter_{name}(void)" in adapter_text)
        (tmp / "routines.h").write_text(
            declarations + "static const Routine ROUTINES[] = {\n" + "".join(entries) + "};\n"
            f"#define ROUTINE_COUNT {len(entries)}\n")

        flags = ["-std=gnu99", "-O2", "-I", str(ROOT / "codegen"), "-I", str(tmp), "-I", str(game), "-I", str(LIB)]
        objects = []
        # reference code: its own generated headers, routines renamed
        ref_object = tmp / "ref_code.o"
        run("cc", *flags, "-I", str(tmp / "ref"), "-I", str(LIB), "-include", str(tmp / "rename.h"),
            "-c", str(tmp / "ref/code.c"), "-o", str(ref_object))
        objects.append(str(ref_object))
        sources = [game / "code.c", game / "data.c", ROOT / "codegen/native/display.c",
                   ROOT / "tests/routines/diff.c"] + [LIB / f"{n}.c" for n in RUNTIME]
        if adapters.exists():
            sources.append(adapters)
        executable = tmp / "routine-diff"
        run("cc", *flags, "-o", str(executable), *map(str, sources), *objects)
        result = subprocess.run([str(executable)], cwd=ROOT)
        sys.exit(result.returncode)


if __name__ == "__main__":
    main()
