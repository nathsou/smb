# Recompiling Super Mario Bros. 3

Super Mario Bros. 3 is the third game the recompiler handles, on an MMC3
board: 32 banks of 8 KiB of PRG ROM, two of them fixed (in PRG mode 1, the
game's, bank 30 at $8000 and bank 31 at $E000) and any others switched in at
$A000 and $C000, 8 KiB of RAM at $6000, 128 KiB of CHR ROM switched in 1 and
2 KiB banks, and a scanline counter that raises the IRQ. As for the other
games, the repository holds no game code or data: `maps/smb3.map` names the
routines, tables and variables, and the recompiler reads your own dump.

```bash
make codegen GAME=smb3   # smb3.nes -> codegen/smb3/
make build GAME=smb3     # ./smb3 (raylib)
python3 tests/check_codegen.py --smb3
```

The page recognises an MMC3 dump and recompiles it with `smb3.map` and the
runtime module for cartridges, in about 13 seconds. From the 256 KiB image
the recompiler decodes 53,753 instructions and emits 2,217 C functions
(about 96,000 lines) or about 1.3 MB of WebAssembly.

## Two windows

Metroid has one window: code in a bank only runs while that bank is mapped,
and fixed-bank code calling into the window can reach any bank the map names
there. With two windows that is no longer enough. Code in a bank at $A000
calls routines at $C000 that run with whatever bank is mapped there, and the
fixed banks call into both. Discovery therefore follows what is known of the
banks (`Banking` in `src/image/values.mbt`):

- **The banks mapped.** Each window holds a set of banks (up to 32, empty
  when unknown). Code of a bank runs while its bank is mapped. A write to
  MMC3's bank registers ($8000 select, $8001 data) maps what was written.
- **Where the game keeps them.** The game switches banks by writing the bank
  it wants to RAM and calling a routine that writes it to the mapper
  (`lda #bank / sta $0720 / jsr $ffc2`). Discovery follows the registers
  and up to 32 RAM bytes as sets of values, and a load indexed into a table
  of the map can be any of its bytes (the banks of each tileset, say). Short
  callees are evaluated in place (up to 48 instructions, three deep), so the
  switch routine maps what its caller asked for. These values never decide
  a branch: the interrupt changes RAM where the foreground waits.
- **Shadows.** The interrupt handlers switch banks too, and restore them
  from RAM. `shadow $a000 $0720` tells discovery that, where nothing else
  is known, a window holds the bank in that byte. Waits and calls keep the
  shadows. A call also keeps the banks of the windows the callee never
  switches, which an analysis of the whole program finds (routines that
  write the mapper or a shadow, and their callers). Exploration runs again
  until the wait loops and the switching routines settle (three times for
  SMB3).

A call into a window then goes to the banks known to be there. A single bank
that is known, with code at the address, is a direct call to that bank's
routine. Otherwise it is a window dispatcher (on `prg_bank` for $A000,
`prg_bank1` for $C000) over the banks that can be there, among those the
map names at that address (`routine 0:$c49b objects_c49b`). Where the map
names none, it is the banks whose bytes there look like code: a bounded
walk that stops at undecodable bytes, `BRK` or the map's data. When none do,
discovery reports the problem with the candidates, and the map names the
right one. `moon run src/inspect -- --rom smb3.nes --map maps/smb3.map
--collect` lists every problem at once, and `--facts BANK:ADDR`, `--calls
ADDR` and `--switching BANK:ADDR` show what discovery knows at an address,
who calls a routine and why a routine switches banks.

The map also says what the game guarantees and discovery cannot see:

```
prg_mode 1                       ; banks 30 and 31 fixed at $8000 and $E000
bank 13 tiles13 $a000 with 14    ; at $A000, and bank 14 at $C000 while it runs
shadow $a000 $0720               ; the bank at $A000 where nothing else tells
trap 2:$a7f5                     ; a branch always taken: the fall-through traps
code 8:$a965 code_a965 15        ; where a computed jump in bank 8 goes
```

A tileset's bank always runs with the level loader (bank 14) at $C000,
which the tables of banks per tileset do not say together. The runtime
traps if the game selects the other PRG mode (`mapper_prg_mode`, which the
reset entry calls).

Computed jumps go to the code tables of the jump's bank or, for code that
dispatches into another window (the objects' common code in bank 0 jumps to
each object's routine in banks 1 to 5), to that window's. When the bytes
just before the jump load the pointer from a code table
(`lda table,y / sta $00 / lda table+1,y / sta $01 / jmp ($0000)`), only that
table's entries.

SMB3's dispatcher, `jump_by_index` ($FE99), is followed by a table of
routine addresses. Most of its hundred tables lead to the same bank or the
fixed banks, and some to the other window. A target there becomes a window
dispatcher too.

## Frames in parts

The interrupt handlers also change the picture mid-frame. The NMI handler
sets the scroll and enables interrupts, and the IRQ handler, raised by MMC3
after a number of scanlines, switches the CHR banks and the scroll for the
status bar at the bottom of the screen. The IRQ handler is a root of the
program once code enables interrupts. The frame model stays SMB's, with the scanlines added:

- After the NMI (and the foreground's frame) the runtime clocks MMC3's
  counter once for the pre-render line and once per visible scanline while
  rendering is on (`mapper_frame` in `codegen/lib/mapper.c`). Where it
  raises the IRQ, and interrupts are enabled (the NMI handler executed
  `CLI`, or the foreground runs with interrupts on), the IRQ handler runs.
- What the handler changes (control and mask, CHR banks, the horizontal
  scroll from the PPU's temporary address, the vertical position when it
  writes $2006) applies from the next scanline on: the frame is drawn in
  parts (`video.splits`, up to 8).
- The software renderer has a general path, scanline by scanline, for
  frames in parts, CHR banks and 8x16 sprites, with the hardware's sprite
  priority. The GPU renderers read each scanline's settings from a texture
  (`video_lines`) and the whole CHR from an atlas.

The NMI handler of one mode jumps into the window ($CECF, bank 22) with the
registers still pushed: the stack analysis follows the routines there as
part of the handler, with the data it left. A routine of bank 14 can return
with a byte still pushed on a path the game never takes, which exact returns
(see [Metroid](metroid.md#return-addresses)) handle as the machine would.

## Oracles

As for Metroid, the reference is the interpreter (`tests/interpreter.c`),
which now also runs the IRQ through the same frame model and takes waits in
a bank (`BANK:FROM:TO`).

- `rec/smb3.rec` (3,201 frames) goes from the title to the world map and
  through 1-1. Its golden trace `tests/golden/smb3.trace` comes from the
  interpreter.
- `tests/check_codegen.py --smb3` checks the interpreter against the golden
  trace, then the recompiled C natively (loading save states back every 500
  frames) and both WebAssembly variants. It compares the GPU renderers with
  the software renderer: the desktop one under Xvfb (`make gpu-check
  GAME=smb3`) and the page's WebGL one in headless Chromium, which also
  draws frames with every theme (`tests/web/cartridge_gpu.cjs`).
- `replay` and the interpreter take `--poke FRAME:ADDR=VALUE`, to start
  elsewhere than the recording would (`727=3` before the world map loads
  starts in world 4). Recordings that enter the first levels of each world
  (worlds 1 to 8, eight paths each, 6,000 frames) matched the interpreter
  frame for frame; they are not in the repository.
- `replay --save-state FRAME:FILE` writes a save state in the page's
  format, to look at a moment of the game in the page (with its themes).

`--profile FILE` makes the interpreter write what ran: the instructions per
bank, calls into a window with the bank mapped, and the largest index of
each jump table. It is how the map's table lengths and window targets were
checked.

## Limits

- **Timing.** The IRQ runs between frames' worth of foreground, after the
  NMI, and its changes apply from the next scanline: nothing changes within
  a scanline, and mirroring changes mid-frame are not drawn. The 8 sprites
  per scanline limit is not modelled.
- **Coverage.** Discovery is static, and the recordings cover only part of
  the game. A computed jump or a window call that reaches a target the map
  does not list traps (`cpu_unresolved_jump`) rather than run something
  else.
- **Names.** The map names the banks, the dispatcher and its tables, the
  bank switching, the interrupt handlers' modes, the tileset tables and the
  object tables. Most routines keep generated names (`loc_levels_cadd`).
