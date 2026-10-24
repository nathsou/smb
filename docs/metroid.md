# Recompiling Metroid

Metroid is the second game the recompiler handles, and the first that is not
NROM: an MMC1 board with eight 16 KiB banks of PRG ROM, the last fixed at
$C000 and any other switched in at $8000, 8 KiB of RAM at $6000 and CHR RAM
(the game writes its own tiles). As for SMB, the repository holds no game
code or data: `maps/metroid.map` gives the routines, tables and variables
this project's names, and the recompiler reads your own dump.

```bash
make codegen GAME=metroid   # metroid.nes -> codegen/metroid/
make build GAME=metroid     # ./metroid (raylib)
python3 tests/check_codegen.py --metroid
```

The page recompiles it too: it recognises an MMC1 dump and uses the map and
runtime module for such cartridges.

From the 128 KiB image the recompiler decodes 22,228 instructions and emits
1,819 C functions (about 56,000 lines) or, for the page, about 730 KB of
WebAssembly.

## Banks in the map

Window entries name their banks:

```
bank 0 title    ; title screen, password, ending
bank 1 brinstar
routine $c4e8 switch_bank            ; the fixed bank
routine 1-5:$80b0 area_80b0          ; the same address in banks 1 to 5
table 0:$802a routines 62
code $c51f bank_inits 9              ; code addresses, a word each
code 1-5:$8048 enemy_moves 8 2 rts   ; addresses minus one, returned through
```

A banked entry becomes one per bank, named after the bank
(`brinstar_area_80b0`), and one for the window as the fixed bank sees it
(`area_80b0`). A code table lists where computed jumps go. The syntax is in
`src/image/rom.mbt`.

Inside the recompiler a location is a bank and an address
(`((bank + 1) << 16) | address`). A plain window address means "the window
as mapped at run time", and fixed-bank addresses stay as they are, so NROM
programs are unchanged.

## Discovery

Exploration starts from the vectors in the fixed bank, as for NROM:

- **Calls into the window.** Code in a bank that calls an address in the
  window stays in its own bank, since it only runs while its bank is mapped.
  When fixed-bank code calls into the window, the call can reach every bank
  the map names for that address. It becomes a window dispatcher, a
  function that switches on the bank mapped (`prg_bank`) and calls that
  bank's routine. A bank the map does not name traps.
- **Computed jumps.** `JMP (pointer)`, and an `RTS` that returns through two
  bytes the routine pushed (the stack analysis sees two bytes above the
  return token), go to the addresses of the code tables in the jump's own
  bank (fixed-bank jumps use the fixed bank's tables). The 6502 code reads
  the pointer and jumps through it, and the IR's `Jump` terminator lists
  the possible targets. Any other target traps.
- **Map tables are data.** A transfer into a table traps instead of decoding
  data as instructions.
- **Mapper writes.** MMC1's serial port (five writes to $8000-$FFFF) becomes
  a `mapper_write` effect, and the runtime (`codegen/lib/mapper.c`) keeps
  the shift register, the control and PRG registers and the mapped window.
  Only PRG mode 3 (last bank fixed) is supported; any other mode traps.

`moon run src/inspect -- --rom metroid.nes --map maps/metroid.map` reports
the windows, computed jumps and wait loops found. `--bank N` and
`--symbols` show more.

## Waiting for the interrupt

SMB's main loop idles at `jmp *`, where the runtime ends the frame. Metroid
waits for the interrupt in loops of the form `lda frame_ready / beq wait`,
some inside subroutines several calls deep.

`src/image/loops.mbt` recognises a **wait loop**: a loop entered only at its
start, whose every decision depends on memory it read since the start.
Memory does not change during the loop, so once an iteration from the start
comes back to it, every later one would too, and only an interrupt could
end it. The transfer back to the start becomes the IR's `Suspend`. Loops
that only an interrupt could end, but that aren't wait loops, are still
rejected instead of compiling into a hang. Metroid has three wait loops:
in `main_loop`, `wait_frame` and `screen_off`.

The runtime only gets control back when the reset routine returns. So
`make_resumable` (`src/ir/escape.mbt`) rewrites every function that can
reach a wait loop:

- **Suspending.** At a wait, the function records a resume point and
  returns. After each call that can suspend, a function checks
  `cpu_suspended`, records its own point and returns too, all the way out
  of the reset routine.
- **Resuming.** On the next frame the runtime delivers the interrupt and
  calls the reset routine again. Each function on the way pops its point
  and goes straight back to the call it suspended in, until the innermost
  one restarts its wait loop and reads memory again.

Registers, flags and the guest stack are machine state, so they survive.
Tail calls and dispatches to suspending routines become calls followed by
returns, so that they can resume. The interrupt handler must not reach a
wait: the build fails with the chain of calls if it does.

## Return addresses

Metroid reads and drops return addresses: `pla; pla; rts` returns to its
caller's caller, `pla; pla; jmp` abandons a call, and one routine pulls half
an address and jumps into its caller. When a program does this (the stack
analysis finds pulls below the return token), its returns are **exact**:

- JSR pushes the return address on the guest stack, as the 6502 does.
- RTS pulls it, and `cpu_returned` records where it goes.
- After each call, the caller compares that with its own return address.
  If they differ, the callee returned past it, so the caller returns too,
  until the function that the address belongs to resumes after its call.
- The interrupt handler's entry pushes a return address and the status, as
  the hardware does, and RTI pulls them.

So the guest stack holds what the machine's would, and code that reads it
sees the same bytes. A callee that always unwinds (`pla; pla; rts`, after
any `jmp`) never returns to the instruction after its call, so discovery
doesn't decode the bytes there, which are often data. SMB doesn't touch its
return addresses, and its calls stay plain C calls.

## Runtime

The game drives the PPU through its registers, so it is built with
`-DPPU_BUS -DMAPPER`:

- `codegen/lib/ppu_bus.c` implements the register protocol (write toggle,
  VRAM address, read buffer) on top of `Video`.
- Writes to CHR RAM bump `video.chr_version`, so the GPU renderers upload
  the tiles again.
- `video.mirroring` and the vertical scroll are used by the software
  renderer (a general path next to SMB's fast one), by `web/video.frag`
  and by both GPU hosts.
- `codegen/lib/cartridge.c` loads the iNES file and the CHR, `prg.c` holds
  every bank, and `wram` is the cartridge's RAM.
- Save states (`codegen/lib/state.c`) also hold the CHR RAM, the
  cartridge's RAM, the mapper, the PPU's latches, the CPU's registers and
  the resume points. A state is saved between frames, while the foreground
  is suspended in a wait.

## Oracles

SMB's oracles compare the recompiled program with earlier versions of
itself. Metroid has no earlier version, so its reference is an independent
interpreter, `tests/interpreter.c`. It is a plain 6502 interpreter over the
same runtime (memory map, PPU registers, mapper, APU) and the same frame
model: it runs the foreground until a wait loop jumps back to its start,
then each frame runs the interrupt handler and the foreground again. The
wait loops come from the recompiler. Both programs write per-frame state
hashes (`codegen/lib/state_hash.c`: RAM, the cartridge's RAM, the bank
mapped, CHR, nametables, palette, sprites, control, mask, scroll and
mirroring).

- `rec/metroid.rec` (4,601 frames) goes from the title to Brinstar, takes
  the morph ball, goes through doors and pauses. Its golden trace
  `tests/golden/metroid.trace` comes from the interpreter.
- `tests/check_codegen.py --metroid` checks the interpreter against the
  golden trace. It then checks the recompiled C natively, loading save
  states back every 500 frames (`./replay --check-states 500`), and both
  WebAssembly variants (`tests/web/replay_cartridge.mjs`) against it.
- `make gpu-check GAME=metroid` compares the GPU renderer with the software
  renderer pixel for pixel over the recording. `--variations` also draws
  each compared frame with every mirroring, base nametable and a few
  vertical scrolls, which catches mistakes the recording's own frames don't
  show.
- `tests/mmc1/*.asm` is a small independent MMC1 program. It switches
  banks, calls the window from the fixed bank, makes computed jumps, waits
  in a subroutine and returns past its caller. `tests/check_fixtures.py`
  recompiles it in CI and compares it with the interpreter frame by frame,
  natively and in WebAssembly.

`./replay metroid.nes --rec rec/metroid.rec --dump 1200,4000` writes frames
as PPM images. `--trace FILE` reports the first frame that differs from a
trace.

## Limits

- **Boards.** MMC1 only in PRG mode 3, with CHR RAM or 8 KiB of CHR ROM
  (no MMC1 CHR banking). [SMB3](smb3.md) added MMC3.
- **Timing.** The frame model is SMB's: the interrupt runs once per frame,
  at a wait or the idle loop, and the frame is drawn from the state at the
  end. Metroid changes nothing mid-frame (SMB3's IRQ does, see
  [frames in parts](smb3.md#frames-in-parts)).
- **Names.** The map names the frame, bank switching, the title bank's
  entry points, the sound engine and the areas' shared routines. Most other
  routines keep generated names (`loc_brinstar_8024`).
