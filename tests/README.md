# Recompiler regression fixtures

`smb-original.asm` is the unmodified doppelganger source already present in
this repository's history as Git object `3988359:src/smb.asm`.  Its SHA-256 is
recorded in `smb-original.asm.sha256`; the migration test compares the active
source against this value after the source cleanup is complete.

The functional SMB regression is the existing `rec/warpless.rec` recording.
When a legally obtained SMB ROM is available, `make hash` must produce the
cumulative hash recorded in `codegen/hash.c`.

The fixture deliberately references the immutable Git object instead of
duplicating the copyrighted disassembly in a second working-tree file.
