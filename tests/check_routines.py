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


def check_native_calls():
    """With registers as locals, the hand-written routines (codegen/native),
    which share their C with the WebAssembly build, may only call routines
    that keep the machine's registers: `void name(void)` in code.h."""
    header = (GAME / "code.h").read_text()
    routines_part = header[header.index("void cpu_irq_entry(void);"):]
    with_locals = {name for name, params in re.findall(r"^\w+ (\w+)\((.*)\);$", routines_part, re.M)
                   if params != "void" or not re.search(rf"^void {name}\(void\);$", routines_part, re.M)}
    for source in sorted((ROOT / "codegen/native").glob("*.c")):
        called = set(re.findall(r"\b(\w+)\s*\(", source.read_text()))
        wrong = sorted(called & with_locals)
        if wrong:
            sys.exit(f"{source.relative_to(ROOT)} calls routines with locals: {', '.join(wrong)}")


def main():
    check_native_calls()
    with tempfile.TemporaryDirectory(prefix="smb-routines-") as tmp:
        tmp = pathlib.Path(tmp)
        run("moon", "run", "src/main", "--", "--rom", "smb.nes", "--map", "maps/smb.map",
            "--native", "codegen/native/routines.txt", "--reference", "--output", str(tmp / "ref"))
        reference = (tmp / "ref/code.c").read_text()
        renamed = sorted(set(FUNCTION.findall(reference)) |
                         {name for name, _ in routines(reference)} |
                         {"cpu_reset_entry", "cpu_nmi_entry"})
        (tmp / "rename.h").write_text("".join(f"#define {n} ref_{n}\n" for n in renamed))

        # (symbol, routine, outputs): with registers as locals, the routines
        # through their adapters, and the entry points as generated
        code = (GAME / "code.c").read_text()
        adapters = GAME / "machine.c"
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
            entries.append(f'    {{"{name}", {symbol}, ref_{name}, {flags}}},\n')
        declarations = "".join(f"void ref_{name}(void);\n" for name, _ in routines(reference))
        declarations += "".join(f"void {symbol}(void);\n" for symbol, name, _ in current if symbol != name)
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
        if adapters.exists():
            sources.append(adapters)
        executable = tmp / "routine-diff"
        run("cc", *flags, "-o", str(executable), *map(str, sources), *objects)
        result = subprocess.run([str(executable)], cwd=ROOT)
        sys.exit(result.returncode)


if __name__ == "__main__":
    main()
