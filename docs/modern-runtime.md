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
alone (`make header`), and `codegen/native/routines.h`, the signatures they
have and call with, generated with the game (`make native-interface`). Code generated from the ROM and from the disassembly
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

Arrays indexed alike are the fields of a record (the map's `record` lines):
a structure laid out over the same bytes, with each field at its array's
address and gaps between them, so `enemy(x)` is the structure at `$0f + x`:

```c
struct enemy_record {
  uint8_t active; // $000f
  uint8_t gap0[6];
  uint8_t kind; // $0016
  ...
};
#define enemy(i) (*(struct enemy_record *)&ram[0xf + (i)])

enemy_kind(x) = 0xd;    ->  enemy(x).kind = ENEMY_KIND_PIRANHA;
sprite_tile[4 + y] = a; ->  sprite(4 + y).tile = a;
```

SMB's records are the object slots (`obj`, which runs over the player,
the enemies, the fireballs, the blocks, the misc objects and the bubbles in
turn, and `enemy`, `fireball`, `block`, `misc`, `bubble`), the floating
scores (`popup`), the cannons and the OAM copy's sprites (`sprite`, four
bytes each, indexed by byte offset). In a record whose fields are
consecutive bytes, as the sprites', a byte reached past a field is the field
of a later element: `$0203 + 2` is the next sprite's tile, `sprite(4 +
y).tile`. The arrays of a record lose their own views. The index stays
within the record (it does not wrap around page zero or RAM), which
`--check-scratch` checks with the other indexed accesses (see Scratch
values). The generated C reads 1,146 fields this way, 689 of them the
enemies'.

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
and the variables stay bytes. A register loaded from a typed variable holds
its type until written (a forward pass per function), so constants beside it
are named too:

```c
a = enemy_kind(x);
if (a != ENEMY_KIND_SPINY) {
  if (a != ENEMY_KIND_PIRANHA) {
```

SMB's `enemy_kind` names the kinds the code establishes: the routines that
`enemy_initializers`, `enemy_runners` and the large platforms' dispatch give
each kind, and the code that sets or tests one (`draw_vertical_pipe` sets the
piranha, `draw_flagpole` the flag, `fireball_hit_enemy` spares the buzzy
beetle). Every routine of `maps/smb.map` has a one-line description, the
comment above its function.

With `--locals`, a 16-bit addition or subtraction, done a byte at a time
with the carry going from the low byte into the high one, is one sum
(`src/transpile/words.mbt`). The low sum takes the high bytes as `WORD(hi,
lo)`, and the high byte is the sum's:

```c
uint16_t sum2 = (uint16_t)(obj(x).x + pixel_speed + carry);
obj(x).x = (uint8_t)sum2;
a = (uint8_t)(obj(x).page + page_carry + (sum2 >= 0x100));
                 ->
uint16_t sum2 = (uint16_t)(WORD(obj(x).page, obj(x).x) + WORD(page_carry, pixel_speed) + carry);
obj(x).x = (uint8_t)sum2;
a = (uint8_t)(sum2 >> 8);
```

This is exact when the low sum carries (or borrows) at most once, so the
carry test is the sum's bit 8; when the sum is otherwise used only for its
low byte; and when nothing between the two statements writes what the high
bytes read, as they are now read first. A comparison of the low bytes
followed by a subtraction of the high ones that keeps only the carry is a
16-bit comparison, `carry = WORD(obj(x).page, obj(x).x) >= WORD(middle_page,
middle_x);`. SMB has 49 such sums and 2 comparisons. A chain whose high
byte's carry goes on (three-byte positions, the score's digits) stays as it
is.

JSRs are plain C calls: SMB's replay was identical with and without the
emulated return-address stack, and the generator's static stack checks reject
programs that inspect return addresses outside a proven dispatcher.
NMI delivery restores the status flags in C instead of pushing PC and P.

## Registers as locals and parameters

Without options, routines share the machine's registers and flags as
globals, as the 6502 does. With `--locals` (SMB's `make codegen` and the
page), each routine has its own (`src/transpile/locals.mbt`). What it reads
of its caller's registers and flags are its parameters, what its callers
read after it returns are its results, and the rest are locals:

```c
// in: x; out: a                     uint8_t lakitu_chase(uint8_t x) {
void lakitu_chase(void) {      ->      uint8_t a, y;
  y = 0;                               bool negative;
  player_enemy_distance();             y = 0;
  if (neg_flag) {                      player_enemy_distance(x, &negative);
                                       if (negative) {
```

- Results are the interface's outputs: what the routine may change that a
  caller reads afterwards. The first, in the order A, X, Y, carry, zero,
  negative, overflow, is returned. The others are written through pointers,
  `out_y`, or `inout_y` when the routine also reads Y.
- Parameters are what is live on the routine's entry when its returns read
  its results. That is what it reads before writing, plus the results it
  passes through unchanged on some path (a routine that only sometimes sets
  Y takes Y and returns it). They are a least fixpoint over the program: a
  call reads its callee's parameters and sets its results.
- A call assigns only the results live after it, and a tail call whose
  results are the routine's is `return f(x);`. An assignment of a pure value
  to a local nothing reads is left out. Some were live while flags were
  globals, because a routine called later could read them.
- The interrupt and decimal flags and SP stay global. The stack is memory.

Some routines keep the machine's registers: the roots (`reset` and
`frame_interrupt`, which the runtime enters), routines that wait for an
interrupt, and `clear_memory`, which clears RAM through a pointer, scratch
bytes and stack page included (a routine the hand-written code calls that
stores through a pointer). They are printed as without `--locals`. A call
from them to a routine with locals passes the globals (`x =
move_lakitu(x);`). A call the other way goes through `machine`, the
machine's registers by pointer (`*machine.y = y; clear_memory(); a =
*machine.a;`), twice in SMB.

The hand-written routines have locals too, with the signatures their
original code gives them; see Hand-written routines.

The runtime's `indirect_y_addr` and `indirect_x_addr` read the machine's X
and Y. With locals the pointer is read by `zero_page_pointer` and the index
added in the generated code. `cpu_unresolved_jump` is declared
`noreturn`, so a routine that returns a value needs no return after a trap.

In SMB, 211 routines return a value, 40 also write results through pointers,
and 223 take parameters and return nothing. The C compiles without warnings
under the Makefile's flags. Coroutines, restarts and preemption keep the
machine's registers: `--locals` refuses programs that use them. The other
games are recompiled as before.

### Results given back as they were

Callers read X after most of SMB's object routines, which walk other arrays
with it and reload it from `slot` (the object slot being processed) before
returning. X is then no result: the routine gives it back as it was, and
the caller keeps its own.

```c
uint8_t move_enemy_x(uint8_t *inout_x, uint8_t *out_y) {      // before
uint8_t move_enemy_x(uint8_t x, uint8_t *out_y) {             // after
  ...
  x = slot;
```

A forward analysis tracks which locations are equal, as a partition:
registers and flags, their entry values, scratch bytes, pushed stack slots,
constants and the RAM bytes that code with locals loads into X or Y. A copy
joins the copied location's class, a push and a pull move a value through
the stack, and a call separates what the callee may change. Three facts
come out of it, each a greatest fixpoint over the program:

- what a routine is entered with equal, at every call (X and `slot`);
- what it leaves equal at every return (X reloaded from `slot`), which a
  call then joins in the caller;
- what it gives back as it was: a register every return holds the entry
  value of, which comes off its results.

RAM bytes change only by their own stores, by indexed stores that may reach
them without wrapping around page zero or RAM (an index from a scratch byte
stays within them), by pointer stores above the stack page (anywhere in the
routines that keep the machine's state: `clear_memory` clears all of RAM),
and by calls that do one of these. A hand-written routine's original code is
not taken for what its C does: it may change any of them and gives nothing
back as it was, and the routines it calls are entered with no equality. In
SMB, 41 routines that would return a value without this analysis no longer
do (211 against 252), and 125 return X, as a result or through `out_x` (174
without).

The analyses are worklists: the parameters' fixpoint follows a routine again
only when one it calls takes more, the equalities' flow runs blocks in
reverse postorder and again only when what comes into them changes, and a
routine is followed again only when what it is entered with or what a callee
gives back changes. With `--locals` (and the naming below) the recompiler
takes about 3.7 s for SMB from the command line (`moon run`, 1.9 s
without), and the page's recompile about 2.1 s (1 s without, measured in
Node), which the page then caches.

### Scratch values

The scratch bytes `$00-$07` stay in RAM between routines, which is how SMB
uses them: it leaves values there for the routine it calls next, or for its
caller. Within a routine, a value in one of them that never leaves it is a
local, `t0` to `t7`:

```c
uint8_t render_column(void) {
  uint8_t a, x, y, t0, t3;
  ...
    t3 = ram[3] >> 1;     // values of $03 only this routine reads
    t3 = t3 >> 1;
    t3 = t3 >> 1;
    ram[3] = t3 >> 1;     // one that reaches its return, in RAM
```

So `ram[0]` in the C is a value that crosses a routine's edge, and `t0` one
that does not. For each scratch byte, the writes of a routine are joined
with the reads they reach (its webs). A web that only the routine's own
accesses of the byte take part in is a local. A web stays in RAM when it is
reached from the routine's entry, read at a return the caller reads it
after, or read or written by a call. A call that must write the byte (a
greatest fixpoint over the program) ends the web that came before. An index
into the scratch bytes (`ram[1 + y]`) also keeps the web in RAM. What callers
read of a scratch byte after a call, and what interrupt handlers read on
entry, are found as for registers, over the calls. In SMB, 18 routines have
scratch locals (100 uses). Most scratch values cross a call, and stay in RAM.

The map names what a scratch byte holds in a routine (`scratch ROUTINE $0K
NAME ; description`). Where the byte is a local throughout the routine, the
local takes the name; otherwise the routine is printed between `#define NAME
ram[K] // description` and `#undef NAME`:

```c
#define pixel_speed ram[0] // the whole pixels of the speed
#define fraction_speed ram[1] // the sub-pixel part of the speed
#define page_carry ram[2] // the page step: $ff when moving left
uint8_t move_object_x(uint8_t x, uint8_t *out_page_step) {
  ...
  uint16_t sum = (uint16_t)(obj(x).x_force + fraction_speed);
```

SMB's map names 42 bytes over 24 routines (gravity, horizontal movement,
sprite rows, collision boxes, the VRAM queue, bubbles, platforms).

A scratch byte a routine reads on entry is a parameter when only its own
accesses take part in the value it is entered with, and calls that take
the byte the same way: no return its callers read the byte after, no call
that reads it in RAM, no index into the scratch bytes, no wait. Its
callers then pass it, from a local of theirs or from RAM (`ram[2]`, where
the byte has to stay there for another reader). This is a greatest fixpoint:
a byte one routine no longer takes by value may stay in RAM in the
routines that pass it to it. The hand-written routines and those they
call, the targets of computed jumps and dispatches, the roots and the
dispatchers keep the scratch bytes in RAM.

```c
void apply_gravity(uint8_t can_rise, uint8_t x, uint8_t down_force, uint8_t up_force, uint8_t max_speed);

void apply_gravity_object(uint8_t max_speed, uint8_t x, uint8_t down_force, uint8_t up_force) {
  uint8_t can_rise;
  ram[2] = max_speed;
  can_rise = 0;
  apply_gravity(can_rise, x, down_force, up_force, ram[2]); return;
}
```

The parameter takes the name its local has. A value the map leaves
unnamed takes the name of the callee's parameter it is passed to, up the
calls: `apply_gravity_object` passes its caller's `$00` and `$01` on as
`down_force` and `up_force`. 41 routines take 71 scratch values by value.
Of the 509 `ram[k]` in the C, 93 are such arguments; 125 accesses read a
scratch byte by its `#define`d name. A name that an identifier, a
view, a record or a record's field already has is left out (a `#define`
would rewrite `.kind`), and `tests/check_codegen.py` fails if a map name
never reaches the code.

In routines with locals, accesses through pointers and indexed accesses
from other bases are taken to stay out of the scratch bytes. SMB's pointers
lead to ROM, the block buffers and the VRAM lists, and its indices stay
within their arrays. The routines that keep the machine's state are taken
to read and write any scratch byte through a computed address, unless the
address's bounds rule it out (the stack). `clear_memory` clears them
through a pointer.

What RAM keeps of the scratch bytes between frames is then dead, and
differs from before. `./hash --mask-scratch` leaves `$00-$07` out of the
state hash. `tests/golden/warpless.scratch.trace` holds those hashes,
recorded from the build without `--locals`, which still matches
`warpless.trace`; `tests/check_codegen.py --rom` checks both.

Checks:

- The replay matches `warpless.scratch.trace` and `warpless.audio` on every
  frame, and both framebuffer hashes are unchanged.
- `--check-scratch` generates the C with what the analyses take of computed
  addresses checked: an access that reaches a scratch byte that is a local
  where it is made (`scratch_guard`), an index in page zero or RAM that wraps
  (`page_guard`, `ram_end_guard`), an index from a scratch byte that leaves
  them (`scratch_index_guard`), a pointer store in a routine with locals
  below $0200 (`pointer_guard`). One that fails sets `guard_failed`, and
  `./hash --check-scratch` fails on it. None does over the replay.
- The routine-level differential test runs every routine through its
  adapter in `machine.c`, which is generated with `--locals`. The adapter
  takes the routine's parameters from the machine's registers and
  overwrites them before the call, so a routine that reads them instead of
  its own fails. It then writes the results back, and the registers it
  gives back as they were, so those are compared too. Scratch bytes are
  compared where they are results, as registers are. A state first takes on
  what every call enters the routine with (`enter_NAME`: `slot` set to X). A
  run where the guards fail is tried again with X and Y below $20, as the
  game's object and sprite indices are, and left out if they fail again: the
  game's indices never go there, and the program relies on that. 0
  mismatches over the same 530 routines, 155,519 runs; 1,020 are left out.
- The build without `--locals`, which reaches the hand-written routines
  through `machine_calls.c`, still matches `warpless.trace`, and so does the
  split WebAssembly build, whose runtime has the adapters, on all 7,987
  frames.
- The WebAssembly module is byte-identical with and without `--locals`
  (only the C is printed differently).
- The NROM-128 fixture runs with `--locals` too (ROM-free, in CI), from its
  assembly and from its iNES image.
- Mutations of the generated C:
  - every sync to `machine` dropped: diverges at frame 0;
  - every write-back through a pointer dropped: the replay crashes;
  - one carry write-back inverted: the routine diff reports 3 routines;
  - the runtime's `indirect_y_addr` printed in routines with locals: the
    replay crashes;
  - every read of a scratch local made from RAM instead: diverges at frame
    7;
  - the 16-bit comparisons printed with `>` for `>=`: diverges at frame
    282, and the high bytes taken as `sum >> 7`: the game hangs.

  Two single-site mutations were not caught. One was equivalent: the
  routine saves Y in `$01` and reloads it before the write-back. The other
  drops the sync before `select_vram_list_b` in `queue_mushroom_palette`, on
  a path that only levels with `area_style == 1` take, and neither the
  replay nor the random states reach it. The generator makes the same
  choice at every site, so a mistake in it shows up at sites the replay
  does reach. A proof per routine would cover the paths the tests miss (see
  Remaining work).

### Each value its own variable

A routine reuses its registers for one value after another: `apply_gravity`
first holds in A whether the object may rise, then its speed, then the
speed with the force added. With the registers as locals, a register is one
C variable, which a parameter's name could not stay on. `src/transpile/names.mbt`
joins each register's writes with the reads they reach, where the register
is live (its webs, as for the scratch bytes): a web is one value, and can be
a variable of its own. A web is named, in this order, for:

1. the routine's parameter, if the routine's entry is one of its writes:
   the map's `param ROUTINE REG NAME`, or `param * x object slot`, in a
   routine that every call enters with X equal to `slot` (and a web of X
   that only loads of `slot` write);
2. the routine's result, if a return reads it (`result ROUTINE REG NAME`);
3. the result of the calls that write it, if they agree;
4. the map parameter of the calls it is passed to, if they agree.

The webs left unnamed share their register's variable, as before, and so do
those whose name a view, a record, a routine, a temporary, one of the
routine's scratch names or another register's variable already has. Webs of
one register may share a variable however they are named: no read takes a
value from another web. A read prints the web that reaches it, and a write
the web it starts. At a call, a result written through a pointer the callee
also reads stays the variable it came from. At a tail call, or the cases of a
dispatch, what the call writes goes to the registers' own variables, and the
return reads it there.

```c
uint8_t move_enemy(uint8_t object) {        void update_balance_platform(uint8_t object) {
  uint8_t a, x, y;                            ...
  bool carry;                                 object = enemy_offscreen_bits(x, &y);
  a = enemy(object).kind;                     ...
  switch (dispatch(0xc909, a, &y, &carry)) {
    case 0xca77: move_walker(object); return object;

bool player_enemy_distance(uint8_t x, bool *out_left_of_player) {
  ...
  right_of_player = diff2 >= 0;
  left_of_player = ((uint8_t)diff2 & 0x80) != 0;
  *out_left_of_player = left_of_player; return right_of_player;
}
```

Names also come from the callees, up the calls until none changes: a
parameter whose entry value is only passed to callees' named parameters
(and given back) takes their name; a result that is only callees' named
results takes theirs, and one reloaded from `slot` is `object`; a tail
call's result takes the callee's result name (`object = queue_counters(a,
x); return object;`). A scratch byte the map leaves unnamed that a routine
passes to a callee taking it by value is named for the callee. A byte
with values both local and in RAM in a routine names the local, and
`NAME_ram` the RAM.

The map's description of a named parameter or result goes above the
function (`// can_rise: nonzero: the up force applies too`). SMB's map
names every routine's parameters and results (308 `param` and 173 `result`
lines) and its scratch values (196 `scratch` lines): no prototype has a
register's name left, and no routine body a `tK` or a `ram[k]`. X that a
routine passes along without reading it as anything, left by the frame
interrupt or a loop, is `leftover_x`. A map name that never reaches its
routine fails `tests/check_codegen.py`. Without any of these lines the C is
the same as before, byte for byte.

`label $ADDR NAME ; description` names a place inside a routine, where the C
had `loc_ADDR`: all 247 of SMB's are named (`goto next_axis;`,
`start_bump_sfx:`). Enums from the dispatch tables (`player_task`,
`screen_task`, `star_flag_task`), the VRAM list selector by the list each
value draws, and flags for the sprite attribute bits and the sound engine's
request bits (established by the code that sets each one: `pulse1_sfx`'s
bump is set by the block, enemy and fireball bumps) name the constants
beside those variables: `if (player_task != PLAYER_TASK_DYING)`.

Checks: the replays, the checked build and the routine diff as above, with
0 mismatches. While the naming was written, the routine diff caught a call
after which X stays what it was (`same_x`): the analysis took the call to
write X, so the value read after it lost its variable
(`update_balance_platform`, `vram_list1[5 + x]`). The analysis now treats such
a call as not writing X, as the printer does. Taking every call to read a
scratch byte by value (a mutation of the condition for parameters) changes
88 lines of the C and is caught by neither the replay nor the routine diff:
its wrong parameters are passed what RAM holds anyway, except on paths the
tests do not reach. The condition is the same reaching-writes rule as the
scratch locals', but the tests do not guard it well. Once the map named a Y
parameter that indexes a pointer, the replay and the routine diff caught
that the pointer read named the index for its register (`zero_page_pointer(p)
+ y`) and not for its value.

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
  module, instantiated after it. The game module keeps the machine's
  registers, so the runtime is built with `machine_calls.c` (see
  Hand-written routines);
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

With `--locals`, they take and return values like the other routines, with
the signatures their original code gives them. `codegen/native/routines.h`
declares them for the hand-written C, with the generated routines it calls:

```c
uint8_t clear_screens(void); // returns y
void select_vram_list_b(uint8_t a);
void run_game_mode(uint8_t x);
```

so `display.c` reads `y = clear_screens();` and `select_vram_list_b(5);`.
`make native-interface` writes `routines.h` from the generated game, and
`tests/check_routines.py` checks that it is the one generated and agrees
with `code.h`. `frame_interrupt` passes `run_game_mode` and `update_sound`
the X their original code passes them (7, where the loop over the random
bytes leaves it, or the sprite shuffler's).

A game that keeps the machine's registers (built without `--locals`, and the
WebAssembly game module) calls them through `codegen/native/machine_calls.c`,
generated alongside: built with `MACHINE_CALLS`, `routines.h` names the
hand-written C's routines `locals_*`, and the adapters pass their values
through the registers, both ways:

```c
void clear_screens(void) {
  y = locals_clear_screens();
}
void locals_select_vram_list_b(uint8_t v_a) {
  a = v_a;
  select_vram_list_b();
}
```

`codegen/native/smb.h`, which comes from the map alone, no longer declares
the routines.

## Display without a PPU register model

The routines that drove the PPU registers (`reset`,
`frame_interrupt`, `apply_vram_list`, `set_scroll`, `set_ppu_ctrl`,
`clear_screens`, `clear_screen`, `draw_title_screen`) are now C in
`codegen/native/display.c`. Instead of a shared write latch, an
auto-incrementing VRAM address, a buffered data port and vblank / sprite 0
polling loops, they update a `Video` struct (`codegen/lib/video.h`):
nametables, palette, sprites, control, mask, scroll and the status-bar split.
They keep the original RAM side effects, and take and return the registers
their original code does.

The old PPU module is split in three:

- `video.c`: the display state and VRAM address decoding;
- `render.c`: the software renderer, a function of `Video` only;
- `codegen/lib/ppu_bus.c`: the register interface over `Video`, compiled
  only with `-DPPU_BUS` for programs that drive the registers (the test
  fixtures and Metroid, see [recompiling Metroid](metroid.md)).

SMB's build does not include it: a register access left in its generated code
is a link error, and a computed access to $2000-$3FFF or $4014 traps.

## GPU rendering

`web/video.frag` draws a frame from `Video`: the CHR (decoded, a 1 KiB bank
per row of tiles), the nametables and each scanline's settings are textures,
and the palette, sprites and master palette are uniforms. The scanline
settings (`render.c`'s `video_lines`: control and mask, the scanline's
position in the plane of the nametables, the CHR banks, whether it is part
of a status bar) are how a frame drawn in parts reaches the shader: SMB's
fixed status bar, and the parts SMB3's interrupt handler sets mid-frame
with MMC3's banks (see [SMB3](smb3.md#frames-in-parts)). The
desktop build (`codegen/gpu.c`, raylib) and the web page (`web/gpu.js`,
WebGL2) share the shader and use it by default; `--software` (desktop) and
`?software` (web) select the software renderer, which remains the reference.

Both were compared with the software renderer pixel for pixel on all 7,987
frames: natively with Mesa under Xvfb (`make gpu-check`, then
`xvfb-run ./gpu_check --debug-chr`) and in headless Chromium with SwiftShader
(`NODE_PATH=$(npm root -g) node tests/web/gpu_check.cjs`). The only intended
difference, a masked left column showing the backdrop rather than the
previous frame's pixels, never occurs in SMB. Metroid's frames, with
vertical scrolling and its own mirroring, and SMB3's, in parts with CHR
banks and 8x16 sprites, are compared the same way (`make gpu-check
GAME=metroid`, `GAME=smb3`), and in headless Chromium through the page's
two modules (`tests/web/cartridge_gpu.cjs`).

## Themes

A theme (`web/themes`) draws the `Video` state its own way; the page's THEME
menu switches between them while the game runs. Theme shaders include
`video.frag` as a library (`#define NES_LIBRARY` leaves out its `main`), so
they decode tiles, attributes and sprites exactly as the original picture
does.

Both themes start from the scene (`scene.frag`): the frame as a background
and a sprite layer, at least 16 pixels wider on each side, each pixel with
a material. In SMB the materials come from the game, not from the picture: the block
buffer (`block_buffer1`, `block_buffer2`) holds the metatiles of the two
screens around the player that the player can touch, because the level parser
leaves scenery out (`block_buffer_limits`). A metatile there is solid unless
`climb_metatile_limits` makes it climbable, or it is a coin or an invisible
block; the rest of the opaque background is scenery. Lava is the castle's
water tiles, and fire is the fireball and flame sprite tiles. The scene reads
these by the map's names. Other games' maps (Metroid, SMB3) do not name
such level data: there every opaque background pixel is solid, and the
themes know no lights but the sky.
The scene is anchored to the level, not to the screen: its corner moves
in steps of 32 pixels, horizontally and vertically, so that the light's
probes (2 to 32 pixels apart) stay on the same spots of the level as it
scrolls. Anchored to the screen, the level slid across them and the light
shimmered while scrolling. The level's position comes from the scanline
settings of the first scanline that is not a status bar.

- 2D global illumination (`light.js`): a jump flood (`jfa.frag`) gives every
  pixel its nearest occluder and nearest free pixel; radiance cascades
  (`cascade.frag`) march rays through that distance field, from the top
  cascade down, each merging the one above. Fire, lava, coins and question
  blocks emit light; occluders also return last frame's light on them,
  reflected once (shifted by the scroll). An occluder pixel takes the light of
  its nearest free pixel. Light that reaches everywhere is added after, and
  is not reflected: daylight is even, as in the game, and dark areas (a black
  backdrop) are dim. The backdrop is not lit. Without SMB's level data, half
  the light comes from the sky beyond the scene and half from everywhere, so
  that what faces open space is lit, what is enclosed shaded, and enclosed
  rooms (Metroid's caverns, a screen-filling world map) stay lit; dark
  backdrops do not dim them, since the theme knows none of their lights.
- 2.5D ray tracing (`voxel.js`): each opaque pixel is a column of voxels
  over the depths its material gives, seen through a cabinet projection, so
  the play plane is where the game draws it. Rays walk the pixel grid
  checking the depths they cross each cell at. The sun and fire sprites
  (point lights, merged per flame) cast shadow rays, five short rays give
  ambient occlusion, and the 2D global illumination is the light from
  everywhere else; here rays that leave the scene see the sky (in daylight),
  so what is under something is shaded. Without SMB's level data the solid
  background is set behind the sprites, so that characters stay in front of
  the walls they overlap.
- 3D (`model.js`): shapes made at runtime from the tiles and the level data,
  never designed by hand, so that the repository holds nothing of the game.
  `shape.frag` gives each scene pixel, for its background and its sprite, the
  depths it covers (a centre and a half thickness). Solid metatiles are
  16-deep cubes with bevelled edges, the floor's (the bottom two rows)
  reaching back under the scenery as a stage. Pipes are cylinders around the
  axis between their halves (metatiles $10-$15 upright, $1c-$21 sideways).
  Coins and climbables are thin. Scenery and sprites are inflated: a jump
  flood (`edges.frag`) gives each pixel its distance to the edge of its
  silhouette, and the thickness rises from it as a quarter circle.
  `smooth.frag` then blurs their thickness (outside the shape counting as 0),
  which rounds off the creases a distance field leaves along the middle.
  `model.frag` traces those columns as `voxel.frag` does, but from a
  perspective camera tilted down. Where a ray reaches an inflated shape it
  meets the thickness interpolated between pixels, for the depth and the
  normal, so the shape shades smoothly while its outline stays pixel art;
  its own column does not shadow it. A block's top and sides show its own
  tile, as a cube's faces. The lighting is the 2.5D theme's, with more
  ambient light from above than below, a highlight where the sun glints and
  a little haze far back. The picture is drawn at up to 4 times the NES
  resolution and scaled to the canvas (2x2 samples per pixel at the usual
  2x), each sample seeing the sun from its own point, which softens shadow
  edges.

Status bars (any band the scanline settings mark) are drawn as they are.
`tests/web/themes.cjs` (SMB) and `tests/web/cartridge_gpu.cjs` (Metroid,
SMB3) draw replay frames with every theme and fail on WebGL errors. The themes are a presentation
layer only: the game, the Video state and the pixel-exact checks of the
original renderer are unchanged.

## Remaining work

- Registers as locals in the other games, and in the WebAssembly game
  module (which would let the runtime drop `machine_calls.c`).
- The routines that still return X, and those whose X is not `object`
  (159 take an `x`): many are entered with X something other than `slot`
  from some call, or through a jump table, which the analysis takes no
  equality from. A context-sensitive analysis (per call) would find more.
- Names for the values that stay in a register's own variable inside a
  routine (about 4,000 uses of `a`, `x`, `y` and the flags: the accumulator
  between a load and a store, loop counters) and for the arithmetic
  temporaries (`sum`, `diff`, `carry_out`). The map would need a line that
  names a value by the instruction that writes it.
- A proof per routine that the C with locals computes what the reference
  computes, for every input (as acnl does with an SMT solver), for the paths
  that neither the replay nor the differential test's states take.
- Carry chains of three bytes and more (sub-pixel, pixel and page; the
  score's digits), whose middle sum's carry goes on.
- The status-bar split is decided after the frame's game logic, as before;
  hardware decides it at the sprite 0 hit, earlier in the frame.
