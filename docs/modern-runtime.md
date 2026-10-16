# Toward a modern C program

The recompiler produces C that preserves the original program exactly (see
[recompiler-migration.md](recompiler-migration.md)). This work starts moving
that output away from the emulation layers it relied on: the PPU's register
protocol, the 6502 instruction helpers and the emulated return-address stack.
Each step is checked to preserve
the game state on every frame of the replay, and every generated routine is
compared against the previous output.

## Recompiling from the ROM

The game is generated from the ROM image itself (`--rom smb.nes --map
maps/smb.map`, used by `make codegen`), not from a disassembly. The map holds
what execution from the vectors cannot recover, and the names this project
gives to the program:

```
routine $8e04 dispatch                  ; jump through the address table that follows the call, indexed by A
table $8218 game_mode_tasks 8
var $0770 game_mode
var $0780 timers 36
io $4015 apu_status
```

- `routine`: an entry point, emitted as a C function of that name. Routines
  not in the map get generated names, as do internal labels (`loc_8e19`).
- `table`: data inside PRG ROM with its length, including the inline jump
  tables after each call to the dispatcher (nothing else marks where those
  instruction streams stop).
- `var`/`io`: RAM and hardware registers; a length makes an array.

Data names become upper-case address constants (`ram[GAME_MODE]`,
`ROM(VRAM_LISTS_LOW + y)`), so they never collide with functions, struct
fields or library names. The front end decodes the image with the same
exploration as assembly input and rebuilds the source view the lowering
consumes, choosing for each operand an array or a scalar at the same address
depending on whether the access is indexed. An indexed read just before a
table is written as that table minus an offset (`ROM((DEMO_BUTTONS - 1) + x)`).

The names and descriptions are this project's own. Code generated from the
ROM and from the disassembly were checked identical in behaviour (per-frame
trace, ROM framebuffer hash and the routine-level differential test pairing
routines by address). `tests/nrom128.map` names the independent NROM fixture,
which `tests/check_fixtures.py` also recompiles from an iNES image built from
its assembly.

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

`tests/check_routines.py` generates the program a second time from the
unoptimised IR (`--reference`: every instruction with all of its effects),
links both into one binary with the reference routines renamed `ref_*`, and runs
`tests/routines/diff.c`. For every generated routine it takes 50 states from
the replay, randomises A/X/Y, SP and the flags (and, in every other state, 48
RAM bytes), runs both versions under a trap and timeout guard, and compares
registers, RAM, the display state, traps, and the flags that the routine's
callers read. This exercises paths the replay never takes. While the
instruction-helper layer still existed it served as the reference: in a sample
of 24 random single-operator mutations of the generated code the test caught
19; the others were boundary changes (`>=` to `>`) or paths that these states
rarely reach. When the IR replaced the direct instruction printer, the new
output was also compared routine by routine with the previous generator's
(0 mismatches over 529 routines).

`tests/semantics.asm` checks the generated instruction C itself: each routine
ends with PHP so every flag is computed, and `tests/semantics.c` compares
ADC and SBC (all operands and carries), CMP, BIT, shifts and rotates (register
and memory), zero-page wrapping and bus read-modify-write with independent
models.

## The IR

The generator no longer prints C from the instructions directly. `src/ir`
builds an intermediate representation shared by the backends:

- one function per callable region, each a control-flow graph of blocks;
- statements over explicit machine variables (A, X, Y, SP and the six
  flags), single-assignment temporaries, and memory in three spaces: RAM
  (an address proven inside $0000-$07FF after masking mirrors), ROM (proven
  PRG ROM) and the bus (I/O, pointers); hardware writes with fixed targets
  are effects (`apu_write`, ...);
- terminators: goto, branch, return, tail call (fallthrough or jump into
  another routine), dispatch through a proven inline table, idle yield, trap.

Every instruction is built with its complete semantics, all flags included.
PHP and PLP pack and unpack the status byte explicitly, the dispatchers push
the return address and read their table explicitly, and pointers are two
RAM loads, so no backend needs runtime helpers for them.

`src/ir/analysis.mbt` computes interprocedural liveness over the IR with
matched call/return demands: a routine's returns demand what its callers read
after the call, and a call reads what the callee reads on entry (which
includes the caller's demand flowing through the callee). A tail call passes
the routine's demand on to its target. Roots return to the idle loop or to
the interrupted idle loop, which read nothing, so their demand is what the
roots read on entry; SP is live at every return. From the same summaries
(may-write, must-write, upward-exposed reads) each function is annotated
with its interface:

```c
// in: x, y; out: a, carry
void screen_position(void) {
```

Inputs are registers read before being written; outputs are registers and
flags the routine may change that its callers read after it returns.

`src/ir/passes.mbt` then optimises to a fixpoint:

- dead code: assignments nobody reads (most flags), unused temporaries; reads
  with side effects are kept;
- forwarding: a value with a single use later in its block moves into that
  use when nothing in between changes what it reads, keeping the
  truncation of its destination;
- local simplification (`!(a == b)` to `a != b`, nested byte truncations,
  constant offsets).

Forwarding subsumes the earlier load/store fusion and compare/branch folding,
and also folds flag computations into the branches that read them:

```c
if ((ram[(uint8_t)(ENEMY_STATE + x)] & 0x20) != 0) {
  move_defeated_enemy(); return;
}
ram[(uint8_t)(ENEMY_Y + x)] = (uint8_t)(ram[CANNON_Y + y] - 8);
```

## The C backend

`src/transpile/c.mbt` prints the IR. Blocks keep their original order;
forward branches over single-entry regions become if/else, conditional back
edges closing single-entry regions become do/while loops, and other edges
stay gotos. Blocks that only return, tail-call, yield or trap are printed
where they are reached. Casts to `uint8_t` and `uint16_t` appear only where
a value is not evidently in range, and the output compiles without warnings
under `-Wconversion`.

JSRs are plain C calls: SMB's replay was identical with and without the
emulated return-address stack, and the generator's static stack checks reject
programs that inspect return addresses outside a proven dispatcher.
NMI delivery restores the status flags in C instead of pushing PC and P.

## Hand-written routines

`--native FILE` (for SMB, `codegen/native/routines.txt`) lists routines whose
bodies are written by hand. The generator emits their prototypes and, in place
of the body, the contract the replacement must keep:

```c
// clear_screens: native implementation, not generated.
// It may read: none.
// It must produce: y.
```

## Display without a PPU register model

The routines that drove the PPU registers (`reset`,
`frame_interrupt`, `apply_vram_list`, `set_scroll`, `set_ppu_ctrl`,
`clear_screens`, `clear_screen`, `draw_title_screen`) are now C in
`codegen/native/display.c`. Instead of a shared write latch, an
auto-incrementing VRAM address, a buffered data port and vblank / sprite 0
polling loops, they update a `Video` struct (`codegen/lib/video.h`):
nametables, palette, sprites, control, mask, scroll and the status-bar split.
They keep the original RAM side effects and the registers their callers read.

The old PPU module is split in three:

- `video.c`: the display state and VRAM address decoding;
- `render.c`: the software renderer, a function of `Video` only;
- `tests/support/ppu_bus.c`: the register interface over `Video`, compiled
  only with `-DPPU_BUS` for generic programs (the test fixtures).

SMB's build does not include it: a register access left in its generated code
is a link error, and a computed access to $2000-$3FFF or $4014 traps.

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

## Remaining work

- Registers as C locals and parameters: the interfaces give each routine's
  inputs and outputs, but callers and callees still share global registers.
- Typed game state: a struct over RAM with named, sized fields, keeping the
  flat byte array underneath for SMB's out-of-bounds idioms.
- The status-bar split is decided after the frame's game logic, as before;
  hardware decides it at the sprite 0 hit, earlier in the frame.
- Audio still runs on an APU model driven by register writes.
