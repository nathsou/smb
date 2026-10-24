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
enum game_mode title=0 playing=1 victory=2 game_over=3
flags button a=$80 b=$40 select=$20 start=$10 up=$08 down=$04 left=$02 right=$01
var $0770 game_mode : game_mode
var $06fc joypad1 : button
var $0780 timers 36
io $4015 apu_status
```

- `routine`: an entry point, emitted as a C function of that name. Routines
  not in the map get generated names, as do internal labels (`loc_8e19`).
- `table`: data inside PRG ROM with its length, including the inline jump
  tables after each call to the dispatcher (nothing else marks where those
  instruction streams stop).
- `var`/`io`: RAM and hardware registers; a length makes an array, and a
  type (`: game_mode`) names the variable's values.
- `enum`/`flags`: the named values of variables, or bits that combine. Each
  is a C constant named after the type and the value (`GAME_MODE_PLAYING`,
  `BUTTON_A`); the names share the map's namespace, so a value cannot shadow
  a variable or a table.
- `rom`: a dump the map was written for, by the SHA-256 of its PRG then CHR
  ROM (without a header) and their sizes.

The recompiler accepts an iNES file or a raw dump of the cartridge (PRG then
CHR, no header; split by the sizes of a known dump of the same length, or as
16 or 32 KiB of PRG and 8 KiB of CHR). Both hash the same. A dump the map
does not name still recompiles, with a warning (on the command line, and
under the game on the page), since its code may differ from what the names
describe.

Data names become upper-case address constants (`GAME_MODE`,
`VRAM_LISTS_LOW`), so they never collide with functions, struct fields or
library names; the C reads and writes memory through lower-case views of
them (see [The C backend](#the-c-backend)). The front end decodes the image with the same
exploration as assembly input and rebuilds the source view the lowering
consumes, choosing for each operand an array or a scalar at the same address
depending on whether the access is indexed. An indexed read just before a
table is written as that table minus an offset (`demo_buttons[x - 1]`).

The names and descriptions are this project's own; the repository holds no
game code or data, and the generated C goes to `codegen/game/` (`make
codegen` with your ROM). The hand-written routines include
`codegen/native/smb.h`, the map's names as a header generated from the map
alone (`make header`). Code generated from the ROM and from the disassembly
(before it was removed) were checked identical in behaviour (per-frame
trace, ROM framebuffer hash and the routine-level differential test pairing
routines by address). `tests/nrom128.map` names the independent NROM fixture,
which `tests/check_fixtures.py` also recompiles from an iNES image built from
its assembly.

## Oracles

### Replay with debug tiles and per-frame state hash

`codegen/lib/debug_chr.c` generates placeholder pattern tables: SMB's font
layout for background tiles $00-$23, a blank $24, and every other tile
showing its index in hex, with transparent pixels so sprite priority stays
visible. SMB reads its title screen from CHR ROM ($1EC0) as a VRAM update
list, so the debug bank carries a small replacement list ("DEBUG TILES").
The desktop and web builds can draw with these tiles instead of the ROM's
(`--debug-chr`, DEBUG TILES); the program itself always comes from the ROM.

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

### Audio

`tests/golden/warpless.audio` holds two hashes per frame of the replay: the
APU register writes the game made (address and value, in order) and the
samples rendered from them at 48 kHz, one frame's worth. `./hash
--audio-trace tests/golden/warpless.audio` reports the first frame where
either differs; `tests/web/replay_split.mjs` checks the WebAssembly build the
same way. The game never reads the APU, so the write hash pins down
everything the game asks of the sound hardware, independently of how it is
synthesised.

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

- constant propagation across blocks: registers and flags with a known
  value on every path become constants (a call makes what the callee may
  write unknown); reads of PRG ROM at known addresses become the byte read,
  at recompile time; branches on known conditions become jumps and blocks
  no longer reached are dropped;
- dead stores: RAM stores overwritten in the same block before anything
  could read them.

Forwarding subsumes the earlier load/store fusion and compare/branch folding,
and also folds flag computations into the branches that read them:

```c
if ((enemy_state(x) & 0x20) != 0) {
  move_defeated_enemy(); return;
}
enemy_y(x) = (uint8_t)(cannon_y[y] - 8);
```

## The C backend

`src/transpile/c.mbt` prints the IR. Blocks keep their original order;
forward branches over single-entry regions become if/else, conditional back
edges closing single-entry regions become do/while loops, and other edges
stay gotos. Blocks that only return, tail-call, yield or trap are printed
where they are reached. Casts to `uint8_t` and `uint16_t` appear only where
a value is not evidently in range, and the output compiles without warnings
under `-Wconversion`.

With a ROM map, memory is read and written by name. `vars.h` defines each
variable and table of the map as a view of `ram` or `data`, and the backend
writes an access through the view of the name the front end gave its
address:

```c
ram[WORLD_NUMBER]++;              ->  world_number++;
ram[VRAM_LIST1 + 1 + y] = a;      ->  vram_list1[1 + y] = a;
ram[(uint8_t)(ENEMY_Y + x)] = a;  ->  enemy_y(x) = a;
a = ROM(POPUP_TILES + 1 + x);     ->  a = popup_tiles[1 + x];
ram[0x100 + sp] = a;              ->  stack[sp] = a;
```

The views keep the machine's memory: SMB indexes across neighbouring arrays
(one object index runs over the player's, the enemies' and the fireballs'
entries) and clears ranges of them, so the bytes stay where they are and the
names only read them. A variable is the byte at its address and an array or
table the bytes from it. Arrays an index can carry out of their page are
function-like, `enemy_y(x)`: in page zero the address wraps within the page,
as indexed zero-page addressing does, and near the end of RAM to its start,
as RAM's mirrors do. A scalar read with an index is the array that holds it.
A name the generated code already uses as an identifier (a routine, a
temporary, a register) gets no view, and its accesses keep their address;
so do addresses the map does not name, such as the scratch bytes `$00-$07`.
The compiled code behaves identically (per-frame traces, routine diff) and
runs at the same speed. Assembly input has no map, and its C keeps
addresses.

A constant beside a typed variable is written as the type's value: one it
is compared with, stored to it, or used to mask or combine it (also when
the variable is combined with others of its type first). Bits are the names
of those set; a mask with most of them set names those it clears.

```c
game_mode = 1;                             ->  game_mode = GAME_MODE_PLAYING;
if (((joypad1 | joypad2) & 0x40) == 0)     ->  if (((joypad1 | joypad2) & BUTTON_B) == 0)
a = joypad1 & 0x7f;                        ->  a = joypad1 & ~BUTTON_A;
if (area_kind == 0)                        ->  if (area_kind == AREA_KIND_WATER)
```

The types are anonymous C enums in `vars.h` and in `codegen/native/smb.h`,
and the variables stay bytes.

JSRs are plain C calls: SMB's replay was identical with and without the
emulated return-address stack, and the generator's static stack checks reject
programs that inspect return addresses outside a proven dispatcher.
NMI delivery restores the status flags in C instead of pushing PC and P.

## The WebAssembly backend

`src/wasm` emits a WebAssembly module from the same optimised IR
(`--wasm FILE`). The page no longer needs a C compiler for the game: the C
runtime (video, audio, the native display routines, a PRG buffer) is
compiled once into `web/runtime.wasm` (`make runtime-wasm`), and the game
module is recompiled from the ROM. `web/link.js` links the two at load
time in one memory:

- the game module imports the memory, the runtime's machine state as i32
  globals holding addresses (`ram`, `data`, `a`, `carry_flag`, ...), and the
  runtime functions it calls (bus access, APU and joypad writes, the idle
  yield, traps, and the native routines);
- the runtime imports the game routines it calls (the entry points and the
  routines the natives call); these are JavaScript trampolines to the game
  module, instantiated after it;
- the loader copies the ROM's PRG banks into the runtime's buffer.

Constant parts of addresses go into the memory instructions' offsets, so
`ram[ENEMY_X + x]` is the `ram` global plus X, loaded at offset `ENEMY_X`.

Control flow is structured with Ramsey's algorithm ("Beyond Relooper",
ICFP 2022) on reducible graphs: blocks are emitted under their immediate
dominator, merge nodes after a `block` that forward branches leave, loop
headers as a `loop` that back edges continue. One SMB routine, the music
engine's `update_music`, has irreducible control flow; it runs as a loop over
a `br_table` on a block number. `--wasm-dispatch` builds every function that
way, to check the fallback.

Registers and flags live in WebAssembly locals: a function loads those live
on entry, writes back the ones it changed only where a callee, its caller or
the idle loop reads them (from the IR's liveness), and reloads after a call
what the callee may change. When the runtime's addresses are known before
the game module is built (as in the page, which instantiates the runtime
first; `--wasm-layout` from the command line), they are constants in memory
offsets rather than imported globals. Bus reads test for ROM and RAM inline
and call the runtime only for I/O. The WebAssembly build also inlines small
leaf routines (one block, no calls); the C keeps one function per routine.

`node tests/web/replay_split.mjs` replays the recording with the two modules
and matches the golden trace on every frame, with either structuring and
with registers in locals or in memory. Game logic for the 7,987 frames
takes about 28 ms in Node (V8), against 37 ms with registers in memory and
imported addresses, and 14 ms for the clang-compiled C (which inlines
across routines and has no calls between modules).

## In the browser

The page recompiles the player's ROM itself. `src/pipeline` is the
recompiler as a library (no file system); `src/web` exports it to
JavaScript (`moon build --target js`, copied to `web/recompiler.js` by
`make web`). On a ROM upload (kept in IndexedDB, never sent anywhere) the
page:

1. instantiates `web/runtime.wasm` and reads its machine-state addresses;
2. runs the recompiler in a Web Worker (`web/recompiler-worker.js`) on the
   ROM, `smb.map`, the native routine list and that layout, which takes
   2-3 s and yields the game module and the C program;
3. caches both in IndexedDB under a SHA-256 of all the inputs and of the
   recompiler itself, so later visits start at once;
4. links the modules (`web/link.js`) and runs the frame loop.

`web/viewer.js` is a small hand-written code viewer with tabs: the
generated `code.c`, headers and `data.c` (read-only, virtualised so only the
visible lines are in the DOM, syntax-highlighted, searchable with Ctrl+F,
routine names link to their definitions), the hand-written `display.c`, and
the map, which is editable: Recompile regenerates the game and the C with the
edited names and restarts the game.

## Sound

The game drives the sound hardware by writing the APU registers and never
reads them back, so sound is one-way: the game side (`codegen/lib/sound.c`)
collects each frame's writes, and the synthesizer (`codegen/lib/apu.c`)
takes them through a single-producer, single-consumer queue of frames.
`apu_render` applies the next frame's writes at each frame boundary in
sample time (every sample rate / 60 samples) and resumes rendering wherever
the previous call stopped, so it serves any callback size. When the game is
late, the channels keep sounding; when it is ahead, older frames are applied
at once to keep latency within a few frames, and writes that do not fit the
queue join the next frame rather than being dropped.

- Desktop: the game pushes a frame per frame on the main thread; raylib's
  audio callback renders on its own thread. (The previous code ran the APU
  from both threads at once.)
- Web: the synthesizer is its own small WebAssembly module (`web/apu.wasm`)
  inside the AudioWorklet (`web/audio-worklet.js`). The page posts each
  frame's writes, so audio no longer depends on the main thread pushing
  samples in time. Audio can only start after a key press; the page keeps
  the last value of every register and the worklet starts from them, so the
  channels the game enabled at power-on are not lost.

The synthesizer (`codegen/lib/apu.c`) follows the NESdev descriptions of
the channels, the frame sequencer and the mixer:

- channels are event-driven: each timer is scheduled at its next step in
  CPU cycles, so the cost follows the notes (about 0.4 ms per second of
  audio, against 6.9 ms when every CPU cycle was stepped);
- whenever the mixed output changes, a band-limited step (a
  Blackman-windowed sinc, 64 phases of 16 taps) is added at that exact time;
  integrating gives samples without the aliasing of point sampling. On
  sustained notes the energy outside the harmonics is 50-56 dB below them,
  against 10-37 dB before (the higher the note, the worse it was);
- the NES's output filters (high passes at 90 and 440 Hz, a low pass at
  14 kHz) follow, and samples are floats in [-1, 1];
- the frame sequencer is the hardware's, with 4- and 5-step modes and the
  reset on $4017 writes (SMB writes $FF there at the start of every sound
  update, which re-synchronises it with each frame).

Rewriting it fixed several faults of the previous model: the pulse channels,
SMB's melody and harmony, played an octave too high (their timers count APU
cycles, two CPU cycles each); the noise length counter loaded `value >> 3`
instead of the length table, and halting it cleared it; the triangle dropped
to 0 when stopped instead of holding its level (a pop); and the mixer's
lookup table could be read past its end for DMC levels above 2. Only the
DMC's output level is kept, as SMB plays no samples.

The new output was checked against the previous one over the recording:
per-frame loudness correlates at 0.965, and pitch classes (octave-invariant,
since the pulses moved down an octave) agree with a median cosine similarity
of 0.96. The register writes in the audio oracle are unchanged on every
frame; the sample hashes were recorded again from the new synthesizer.

`tests/web/worklet.mjs` runs the worklet processor in Node on the recorded
game and matches the golden samples at the browsers' 128-sample quantum;
`./hash --audio-chunk N` checks other chunk sizes natively, and
`tests/audio_queue.c` the queue's edges.

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
- `codegen/lib/ppu_bus.c`: the register interface over `Video`, compiled
  only with `-DPPU_BUS` for programs that drive the registers (the test
  fixtures and Metroid, see [recompiling Metroid](metroid.md)).

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
previous frame's pixels, never occurs in SMB. Metroid's frames, with
vertical scrolling and its own mirroring, are compared the same way
(`make gpu-check GAME=metroid`, see [oracles](metroid.md#oracles)).

## Themes

A theme (`web/themes`) draws the `Video` state its own way; the page's THEME
menu switches between them while the game runs. Theme shaders include
`video.frag` as a library (`#define NES_LIBRARY` leaves out its `main`), so
they decode tiles, attributes and sprites exactly as the original picture
does.

Both themes start from the scene (`scene.frag`): the frame as a background
and a sprite layer, 16 pixels wider on each side, each pixel with a
material. The materials come from the game, not from the picture: the block
buffer (`block_buffer1`, `block_buffer2`) holds the metatiles of the two
screens around the player that the player can touch, because the level parser
leaves scenery out (`block_buffer_limits`). A metatile there is solid unless
`climb_metatile_limits` makes it climbable, or it is a coin or an invisible
block; the rest of the opaque background is scenery. Lava is the castle's
water tiles, and fire is the fireball and flame sprite tiles. The scene reads
these by the map's names, so a map without them leaves everything scenery.

- 2D global illumination (`light.js`): a jump flood (`jfa.frag`) gives every
  pixel its nearest occluder and nearest free pixel; radiance cascades
  (`cascade.frag`) march rays through that distance field, from the top
  cascade down, each merging the one above; rays that leave the scene see the
  sky, which dark areas (a black backdrop) do not have. Occluders return
  their own light and last frame's light on them, reflected once (shifted by
  the scroll). An occluder pixel takes the light of its nearest free pixel.
  The backdrop is the sky and is not lit.
- 2.5D ray tracing (`voxel.js`): each opaque pixel is a column of voxels
  over the depths its material gives, seen through a cabinet projection, so
  the play plane is where the game draws it. Rays walk the pixel grid
  checking the depths they cross each cell at. The sun and fire sprites
  (point lights, merged per flame) cast shadow rays, five short rays give
  ambient occlusion, and the 2D global illumination is the light from
  everywhere else.

The status bar is drawn as it is. `tests/web/themes.cjs` draws replay frames
with every theme and fails on WebGL errors. The themes are a presentation
layer only: the game, the Video state and the pixel-exact checks of the
original renderer are unchanged.

## Remaining work

- Registers as C locals and parameters: the interfaces give each routine's
  inputs and outputs, but callers and callees still share global registers.
- Scratch bytes as C locals, parameters and return values: `$00-$07` are
  most of the remaining `ram[...]` accesses.
- Records: the sprite table and the object slots as structures over the
  same bytes (`sprite_x[4 + y]` is the X of the sprite after `y / 4`).
- The status-bar split is decided after the frame's game logic, as before;
  hardware decides it at the sprite 0 hit, earlier in the frame.
