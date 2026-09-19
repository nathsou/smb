# Unmodified-source recompilation

This migration starts at `2143a91`. The input reference is the assembly first
checked into this repository, `3988359:src/smb.asm`. Changes are implemented
independently of the previous experimental recompiler branch.

## Stages

1. Establish reproducible generation and the existing headless regression.
2. Recover returns, shared regions, and routine boundaries from control flow.
3. Resolve original byte addresses, aliases, directives, and instruction overlap.
4. Recover inline dispatch from instruction semantics and stack continuations.
5. Restore unchanged assembly, including unreachable bytes and vector entries.
6. Improve constant/flag analysis, loop structure, and comment provenance.
7. Generalize bus accesses and document the platform scheduling contract.
8. Exercise adversarial synthetic inputs and an independent NROM fixture.

Each major stage is committed separately. The existing 7,987-frame recording
must continue to produce cumulative image hash `1633679932`. This is a port
regression, not a substitute for machine-semantics tests or PRG-byte comparison.

Run `python3 tests/check_codegen.py --rom` with a local `smb.nes` to reproduce
the full regression. Without `--rom`, no copyrighted ROM is needed. The native
headless build does not require Raylib. ROMs and build products stay untracked.
