# Recompiling the original disassembly

This work starts at `2143a91`. `src/smb.asm` now matches the original checked-in
assembly, `3988359:src/smb.asm`, byte for byte. Its SHA-256 is
`e5c17c42783f430029ab90aa37c73d51c2c320898e533b3ff196dad38c50ae42`.
The implementation was developed independently of the earlier experimental
recompiler branch.

The compiler needs no added routine markers, return markers, rewritten jumps,
removed instructions, or extra annotations in this input. It still benefits
from the original disassembly's names, comments, and `.dw` table boundaries.
It is an assembly-source recompiler, not a general ROM decompiler.

## Cases and their replacements

### Routine markers, jump-to-call rewrites, and shared return labels

Previously, routine ownership depended on source order and `__sub` markers.
Branches to shared tails or shared `RTS` instructions needed annotations or
manual `JMP` to `JSR`/`RTS` substitutions to become valid C.

The replacement builds an instruction CFG. Reset/NMI vectors, ordinary `JSR`
targets, and proven dispatch targets seed callable entries. Each entry owns its
reachable region; a machine block can belong to more than one region. A jump
to another callable entry is outlined only when that entry cannot reach the
predecessor. This keeps cyclic control flow local instead of converting loops
to recursive C calls. Shared tails may be duplicated; pure return chains are
canonicalized to an internal return node. No marker is written into the source.

For example, both `A: ...; jmp Tail` and `B: ...; jmp Tail` can retain a local
copy of `Tail` or call its already-established entry when the acyclic condition
holds. A backedge `Tail -> A` prevents that outlining.

Implementation: `src/lower/lower.mbt` (`collect_basic_blocks`,
`collect_subroutines`, `canonicalize_returns`).

### Deleted padding, vectors, aliases, and relocated tables

The old source layout mixed function discovery with table extraction. Keeping
unreachable code and bytes confused that process; table addresses were rebuilt
rather than preserved, and a few aliases required name-specific handling.

The new assembler resolves labels/expressions and iterates instruction sizing
until zero-page choices stabilize. It encodes all 151 documented opcodes and
preserves `.org`, mixed `.db`/`.dw`, aliases, unreachable bytes, and vectors in
one address-indexed image. Duplicate writes, invalid operands, unresolved
symbols, and out-of-range branches are errors. Code and data are views of the
same bytes. Generated `ROM_ADDR_*` constants also permit code addresses in data
expressions without colliding with C function names.

For SMB the resulting 32 KiB image matches every PRG byte of the local ROM.
Discovery visits 10,680 instruction addresses. The unused IRQ vector is not
blindly decoded as code: reset masks IRQ, and a reachable instruction that can
unmask it conservatively adds its vector. Hardware IRQ scheduling is not yet
implemented by the runtime.

Implementation: `src/image/image.mbt`, `opcodes.mbt`, `program.mbt`; data emission
in `src/transpile/transpile.mbt`.

### Raw BIT opcodes and overlapping instruction streams

An idiom such as this has two valid instruction views:

```asm
First:  ldy #$00
        .db $2c       ; BIT absolute consumes the next two bytes
Second: ldy #$04
        rts
```

Linear source lowering cannot see that the first path executes `BIT $04A0`,
while an entry at `Second` executes `LDY #$04`. The source previously replaced
such overlaps with ordinary jumps.

Recursive byte decoding starts at proven entries and follows real instruction
successors. It permits instruction starts inside another instruction or a data
directive. Normalization inserts explicit edges where source order differs from
machine fallthrough. Several instructions can be recovered from one `.db` line.
The BIT remains unless its C/Z/N/V outputs are dead and its read is ordinary
RAM/ROM; MMIO reads are never discarded by this rule. Timing is outside this
frame-based backend's semantics.

Implementation: `src/image/program.mbt`, flag liveness in `src/lower`, and BIT
emission in `src/transpile/transpile.mbt`.

### Inline jump tables and return-address manipulation

The original dispatcher pops its caller's return address, reads the inline
`.dw` table through a zero-page pointer, and jumps indirectly. A normal C call
cannot reproduce this stack behavior. Previously the helper was removed from
the input and the compiler recognized a specific routine name.

The replacement identifies a candidate table immediately after a call, then
partially executes the called helper for every table index. The incoming return
address is an explicit token; unrelated registers and RAM start unknown. The
proof requires exactly two token pops and verifies that every tested index
resolves to its corresponding table entry. The interpreter understands the
small instruction subset needed for this proof, rather than matching symbol
names or hardcoded scratch addresses.

```text
for index in 0 .. table.length:
    state = unknown_cpu(A=index, stack=return_token(call_pc + 2))
    result = bounded_execute(helper, state, immutable_prg)
    require result.return_token_pops == 2
    require result.indirect_destination == table[index]
```

The emitted helper executes the recovered setup, including flag/register/RAM
side effects, once in a shared C function. Call sites switch on its resulting
address and call named handlers. All 18 SMB sites are recovered. The independent
fixture renames the helper and moves its scratch bytes. An out-of-table target
traps; this is not a proof that every dynamic caller index is in range.
Immutable ROM-backed indirect jumps are also resolved, including the 6502
indirect-JMP page-wrap behavior. Other indirect jumps are diagnosed.

Implementation: `src/image/program.mbt`, dispatcher emission in
`src/transpile/transpile.mbt`.

### Manual branch simplifications and source-order loop assumptions

A finite constant domain propagates A/X/Y and C/Z/N/V to a fixed point. Joins
retain a value only when incoming paths agree. Conditional edges refine their
flag facts; proven edges eliminate impossible paths before decoding padding.
Calls conservatively invalidate value facts. Stack checking preserves branch
conditions, which handles complementary branches without inventing an
impossible path that pops the return token.

C structuring checks all incoming CFG edges before folding a contiguous region
into a loop, if, or if/else. Interior external entries prevent folding; crossing
or irreducible edges retain `goto`. This is intentionally conservative.

A separate backward interprocedural flag analysis propagates the caller's
post-call demands to callee returns, then propagates callee entry demands to the
caller. Flag-preserving callees pass those demands through; flag definitions
kill them. External exits and foreground idle yields preserve observable flags.
Overflow is included: CMP preserves V, while ADC/SBC/BIT define it.
Recovered dispatcher helpers use the same per-instruction transfer in reverse,
so a helper that omits SMB's initial ASL correctly preserves incoming carry.

Adjacent immediate compare/branch pairs become `if (a < value)` or equivalent
C comparisons only when none of the comparison flags escape the branch. This
local def-use transformation avoids changing signed arithmetic semantics or
removing flag values a successor still needs.

Implementation: `src/image/values.mbt`, `stack.mbt`, `src/lower/structure.mbt`,
and `collect_flag_liveness` in `src/lower/lower.mbt`.

### Reset's idle loop and real stack effects

The reset loop no longer needs to be replaced by an interrupt return. A proven
self-jump in the reset region yields to the host with its guest PC recorded.
The frame adapter invokes the vector-derived NMI entry and saves/restores PC/P
on the guest stack. Normal JSRs likewise materialize their original return
address bytes; the return helper checks and consumes them. Tail transfers do
not create a fictitious guest call frame.

Static per-entry stack checking rejects negative/unbalanced data depth,
unrecognized return-token pops, stack-pointer replacement in called routines,
ordinary callees using RTI, and called idle loops needing suspended C frames.
Entry contracts are keyed by both address and role. If an interrupt handler is
also reached by JSR, its ordinary-call context is checked separately and RTI is
rejected there; vector status cannot leak into the ordinary call contract.
Runtime token checks catch unsupported return-address modifications through RAM.
This is a checked function-backend contract, not arbitrary continuation support.

Implementation: `src/image/stack.mbt`, generated vector wrappers, and
`codegen/lib/cpu.c`.

### Hardware and memory accesses tied to SMB names

Address resolution now selects specialization by numeric bus range rather than
symbol spelling. Proven RAM stores stay readable direct accesses. Unknown or
indexed destinations use the bus, which handles RAM/PPU mirrors, OAM DMA,
controller strobe, and APU writes. Zero-page indexing and indirect pointers wrap
at 8 bits; absolute indexed addresses retain their index.

The native instruction layer now supplies all documented instruction/addressing
forms, ADC/SBC overflow, BIT overflow, and PHP/PLP status behavior. NES decimal
status is retained while arithmetic remains binary. The independent NROM-128
fixture exercises these address distinctions without SMB names.

## Readability and provenance

The generated C keeps named functions, original block and instruction comments,
and data-table comments. Comments from inverted branches are explicitly marked
`Original branch:`. Unneeded source labels remain comments; synthetic labels
are omitted when no jump uses them. Generic dispatch setup is shared rather
than repeated at every call site.

Compared with the generated C at `2143a91`:

| Measure | Before | After |
| --- | ---: | ---: |
| `goto` statements | 977 | 696 |
| Structured `do` loops | 0 | 108 |
| Direct register comparisons in `if` | 0 | 295 |
| Lines in `code.c` | 15,932 | 18,087 |

The larger file includes explicit guest call frames and restored source
comments. Size alone is not the readability target; review the named functions,
loops, direct conditions, and comments. The original-address data image also
contains code bytes, so data output is larger by design.

## Completed work and remaining subtasks

The implementation commits follow the migration stages: regression baseline;
CFG callable regions/shared returns; byte image and encoding; restored source
and dispatch proof; safe structuring/shared helper emission; generic bus and
instruction semantics; interprocedural flags/stack contract; independent inputs
and comment/comparison recovery; CI and documentation.

All compiler-specific edits to this SMB assembly have been removed. The next
subtasks extend supported programs or improve output further; they are not
required to annotate this source:

1. **General computed-target analysis.** Add finite sets/ranges for pointer bytes
   and memory def-use facts, including split low/high tables and RTS dispatch.
   Prove bounds and target sets across callers. Keep unknown targets explicit.
2. **Continuation fallback.** Add a resumable guest-PC backend for unproven
   indirect jumps, nonstandard stack use, BRK/IRQ, and non-idle foreground loops.
   Integrate it at proven boundaries without turning all generated code into
   one large PC switch. Differential tests must cover fallback/native crossings.
3. **Broader value recovery.** Build register SSA and memory-effect summaries for
   parameters, return values, expressions, and counted loops. Require alias and
   flag proofs before folding memory operations or eliminating guest state.
4. **Mirrored code views and further inputs.** NROM-128 data reads mirror, but
   executing through both PRG windows needs two guest-PC views of shared bytes.
   Such execution currently reports an error. Add that support and test another
   independently licensed whole game, not only this authored fixture.
5. **Timing and rendering.** Replace the frame adapter with resumable foreground
   execution and scheduled PPU/NMI/IRQ events. The PPU still has an optional SMB
   status-bar adapter; cycle accuracy, RMW bus timing, cartridge RAM, and general
   mapper support are outside this PR. Compiler portability alone does not make
   the current renderer a general NES implementation.
6. **Differential semantic coverage.** Extend independent instruction tests to
   randomized blocks and compare complete CPU/memory state against a trusted
   6502 model, including MMIO event traces and interrupts. The current image hash
   is a port regression and cannot establish universal CPU correctness.

## Validation and reproduction

The tested toolchain is MoonBit compiler `0.10.9+6e6c44045`. CI runs the ROM-free
suite and separately builds WebAssembly; PR branches do not deploy the website.

```sh
python3 tests/check_codegen.py
python3 tests/check_codegen.py --rom  # requires a local, legally obtained smb.nes
make wasm                           # clang with wasm32 and lld support
```

The suite checks deterministic checked-in generation, pristine source SHA,
27 MoonBit tests, an independently authored NROM-128 program compiled in a
temporary directory with the same runtime, and native CPU/bus semantics.
ADC/SBC tests cover every byte pair and carry input. The optional ROM check
compares all 32,768 PRG bytes and replays 7,987 frames, with expected cumulative
hash `1633679932`. ROMs and build products remain untracked. Raylib is unnecessary
for these headless checks.

```sh
moon run src/main -- --input tests/nrom128.asm --output /tmp/nrom-codegen
moon run src/inspect -- --input src/smb.asm --output /tmp/smb-prg.bin
```

The assembler currently accepts documented opcodes and this source dialect.
Unsupported illegal opcodes and unproven dynamic control flow produce errors
rather than silently generating guessed code.
