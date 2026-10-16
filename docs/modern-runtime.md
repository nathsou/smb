# Toward a modern C program

The recompiler produces C that preserves the original program exactly (see
[recompiler-migration.md](recompiler-migration.md)). This work starts moving
that output away from the emulation layers it relied on: the PPU's register
protocol and the 6502 instruction helpers. Each step is checked to preserve
the game state on every frame of the replay, and every generated routine is
compared against the previous output.

## Oracles

### ROM-free replay and per-frame state hash

`codegen/lib/debug_chr.c` generates placeholder pattern tables: SMB's font
layout for background tiles $00-$23, a blank $24, and every other tile
showing its index in hex, with transparent pixels so sprite priority stays
visible. SMB reads its title screen from CHR ROM ($1EC0) as a VRAM update
list, so the debug bank carries a small replacement list ("DEBUG TILES").
The desktop and web builds fall back to these tiles when no ROM is given.

`codegen/hash.c` replays `rec/warpless.rec` (7,987 frames) and computes two
hashes:

- the framebuffer hash, which depends on the tiles and the renderer;
- a game-state hash of everything the renderer consumes plus live RAM
  (stack scratch $0140-$01FF excluded; the guest stack never goes below
  $01E0 and SMB's page-1 variables end at $0139). It contains no pixels, so
  it stays valid while rendering changes.

`tests/golden/warpless.trace` holds the per-frame state hashes; `./hash
--debug-chr --trace tests/golden/warpless.trace` reports the first divergent
frame. The ROM-free suite replays both call conventions, and
`tests/web/replay.mjs` replays the WebAssembly build the same way.

The replay executes about 66% of the generated code.

### Routine-level differential test

`tests/check_routines.py` generates the program a second time in the previous
style (`--instruction-helpers`: calls into `instructions.c`), links both into
one binary with the reference routines renamed `ref_*`, and runs
`tests/routines/diff.c`. For every generated routine it takes 50 states from
the replay, randomises A/X/Y, SP and the flags (and, in every other state, 48
RAM bytes), runs both versions under a trap and timeout guard, and compares
registers, RAM, the display state, traps, and the flags that the routine's
callers read. This exercises paths the replay never takes. In a sample of 24
random single-operator mutations of the generated code it caught 19; the
others were boundary changes (`>=` to `>`) or paths that these states rarely
reach.

## Routine interfaces

`src/lower/registers.mbt` adds an interprocedural register analysis next to
the flag analysis:

- A/X/Y liveness with matched call/return demands, like flags;
- may-write and must-write summaries per routine (callees included);
- upward-exposed reads: registers a routine reads before writing them.

Every generated function is annotated with its interface, for example

```c
// in: x, y; out: a, carry
void GetObjRelativePosition(void) {
```

Inputs are registers read before being written; outputs are registers and
flags the routine may change that its callers read after it returns. A
routine's callers are its JSR sites plus, transitively, the callers of every
routine that jumps or falls through into it. (The demand at an exit block is
wider: SMB shares RTS instructions between routines, so an exit block also
carries what other routines' callers need.) A compare or BIT only consumes
its register if one of its flags is live, so the BIT that hides a second
entry point in SMB's `.db $2c` idiom no longer makes A an input.

Roots' exits (RTI and the idle yield where interrupts are delivered) now
demand only what the roots read on entry, for flags as for registers. The idle
loop reads nothing and the runtime restores P around interrupts; previously
every flag was live there, which kept V live through nearly all code.

## Hand-written routines

`--native FILE` (for SMB, `codegen/native/routines.txt`) lists routines whose
bodies are written by hand. The generator emits their prototypes and, in place
of the body, the contract the replacement must keep:

```c
// InitializeNameTables: native implementation, not generated.
// It may read: none.
// It must produce: y.
```

## Display without a PPU register model

The routines that drove the PPU registers (`Start`,
`NonMaskableInterrupt`, `UpdateScreen`, `InitScroll`, `WritePPUReg1`,
`InitializeNameTables`, `WriteNTAddr`, `DrawTitleScreen`) are now C in
`codegen/native/display.c`. Instead of a shared write latch, an
auto-incrementing VRAM address, a buffered data port and vblank / sprite 0
polling loops, they update a `Video` struct (`codegen/lib/video.h`):
nametables, palette, sprites, control, mask, scroll and the status-bar split.
They keep the original RAM side effects and the registers their callers read.

The old PPU module is split in three:

- `video.c`: the display state and VRAM address decoding;
- `render.c`: the software renderer, a function of `Video` only;
- `ppu_bus.c`: the register interface over `Video`, compiled only with
  `-DPPU_BUS` for generic programs (the NROM fixture and the CPU semantics
  test).

SMB's build does not link `ppu_bus.c`: a register access left in its
generated code is a link error, and a computed access to $2000-$3FFF or
$4014 traps.

## GPU rendering

`web/video.frag` draws a frame from `Video`: pattern tables and nametables
are textures, and the palette, sprites and master palette are uniforms. The
desktop build (`codegen/gpu.c`, raylib) and the web page (`web/gpu.js`,
WebGL2) share the shader and use it by default; `--software` (desktop) and
`?software` (web) select the software renderer, which remains the reference.

Both were compared with the software renderer pixel for pixel on all 7,987
frames: natively with Mesa under Xvfb (`make gpu-check`, then
`xvfb-run ./gpu_check --debug-chr`) and in headless Chromium with SwiftShader
(`NODE_PATH=$(npm root -g) node tests/web/gpu_check.cjs`). The only intended
difference, a masked left column showing the backdrop rather than the
previous frame's pixels, never occurs in SMB.

## Instructions as C

The generator now writes each instruction as C over the register and flag
variables, updating only the flags that are live afterwards:

```c
a = ram[SavedJoypad1Bits]; // check to see if start is pressed
a &= Start_Button; zero_flag = a == 0; // on controller 1
```

Operands are classified from their address range: provable RAM becomes a
`ram[...]` lvalue, provable PRG ROM a `ROM(...)` read, and anything else
(I/O, pointers) still goes through the bus (`read_byte`). Read-modify-write
instructions outside proven RAM, and PHP/PLP, keep their helpers. For SMB no
helper call remains, so `instructions.c` is no longer part of its build.
`--instruction-helpers` restores the previous output as the reference for the
differential test.

A load (or transfer) followed by a store of the same register becomes one
assignment when the register is dead after the store and the load's flags are
dead. In SMB this applies to 668 pairs:

```c
ram[GamePauseTimer] = 0x2b; // set pause timer
ram[PrimaryHardMode] = ram[WorldSelectEnableFlag];
```

The differential test compares registers and flags only where the routine's
callers read them, since folded loads leave dead registers with other
values.

## Remaining work

- Registers as C locals and parameters: the interfaces give each routine's
  inputs and outputs, but callers and callees still share global registers.
- Typed game state: a struct over RAM with named, sized fields, keeping the
  flat byte array underneath for SMB's out-of-bounds idioms.
- Folding flag computations into the branches that consume them, beyond the
  existing immediate compare/branch pairs.
- The status-bar split is decided after the frame's game logic, as before;
  hardware decides it at the sprite 0 hit, earlier in the frame.
- Audio still runs on an APU model driven by register writes.
