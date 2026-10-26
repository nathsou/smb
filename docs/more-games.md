# Five more games

After Super Mario Bros., Metroid and Super Mario Bros. 3, the recompiler
handles Bomberman, Tetris, The Legend of Zelda, The Chessmaster and Kirby's
Adventure. As for the others, the repository holds no game code or data:
each map in `maps/` names routines and tables and records the facts discovery
cannot recover on its own. The recompiler reads your own dump.

```bash
make codegen GAME=kirby          # kirby.nes -> codegen/kirby/ (likewise bomberman,
make build GAME=kirby            # tetris, zelda, chessmaster): ./kirby (raylib)
python3 tests/check_codegen.py --kirby
```

| Game | Board | Decoded instructions | C functions (lines) | WebAssembly | Recording |
| --- | --- | --- | --- | --- | --- |
| Bomberman | NROM, 16 KiB | 3,861 | 159 (8,500) | 110 KB | 900 frames |
| Tetris | MMC1, 32 KiB, CHR banks | 4,754 | 194 (10,100) | 130 KB | 3,000 frames |
| The Legend of Zelda | MMC1, 128 KiB, CHR RAM | 19,016 | 1,125 (47,100) | 570 KB | 2,400 frames |
| The Chessmaster | MMC1, 128 KiB | 15,445 | 561 (48,700) | 660 KB | 6,019 frames |
| Kirby's Adventure | MMC3, 512 KiB | 18,470 | 1,128 (47,100) | 640 KB | 3,200 frames |

All five drive the PPU through its registers and run on the runtime module
for cartridges, NROM included (the header's mirroring is applied). Each is
checked like Metroid: the reference interpreter (`tests/interpreter.c`)
writes `tests/golden/NAME.trace` over `rec/NAME.rec`, and the recompiled
game matches it frame by frame natively, with save states loaded back every
500 frames, and in WebAssembly (both layouts), and the page's WebGL renderer
matches the software one. The page tells the games apart by the SHA-256 of
PRG and CHR (the maps' `rom` lines) and recompiles each in one to five
seconds.

## Bomberman: restarts

Bomberman restarts from deep in its calls: on some paths it loads the stack
pointer (`ldx #$ff / txs`) and goes on with the reset routine's code. The
stack analysis finds a TXS outside the reset routine, checks that the reset
routine reaches the same instruction, and records a restart there. The IR
(`src/ir/restart.mbt`) makes the code from that TXS a routine of its own:
the call that leads there unwinds every call on the way (each returns while
`cpu_restarting` is set) down to the reset routine, which calls the restart
routine. Its sound engine jumps through tables of addresses minus one
(`code $e6ad sound_routines 7 2 rts`), pushed and returned through: an RTS
two bytes above the return address is a computed jump.

## Tetris: CHR banks and bytes after calls

Tetris is a 32 KiB MMC1 game: its PRG ROM is one image (no window to follow),
while MMC1's registers still switch its CHR in 4 KiB banks, which the
runtime now does. Its routines read the bytes after their call: a jump table
after `jsr jump_by_index`, the address of what to draw after `jsr $aa98`.
`inline $aa98 2` says how many bytes a routine reads; its calls then return
past them, and keep their return addresses on the guest stack, where the
routine reads them (`exact` returns). A computed jump can go where the map
says besides its tables (`jump $e1cb $e091`, a default set with immediates)
or read a table through a pointer (`jump $e1cb sound_inits`). The check for
loops only an interrupt could end follows what each path knows of the
registers, so that a loop that ends on some path is not taken for a wait.

## The Legend of Zelda: code in RAM, banks followed

Zelda copies code from bank 1 to the cartridge's RAM and runs it there, and
its tables point into it: `copied $6c90 1:$a500 $1270` gives the bytes, which
the recompiler decodes from the ROM, and the generated code traps if RAM
holds others. Its banks are MMC1's 16 KiB at $8000: the game switches with
five serial writes of a bank number, and `banks follow` has discovery
follow them as it follows MMC3's, so that `lda #5 / jsr switch / jsr $8000`
calls bank 5's routine. Its many jump tables follow `jsr jump_by_index`, as
in Tetris.

## The Chessmaster: coroutines and preemption

The chess engine thinks for seconds without waiting for the interrupt, which
the hardware delivers wherever the foreground is. `preempt 500` has the
foreground yield there instead: at a transfer back (a taken branch or a jump
to an earlier address), once it went back 500 times in the frame, it
suspends as at a wait loop (`src/ir/preempt.mbt`), and the interpreter ends
its frames at the same transfers (`--preempt 500 --polls FILE`, the list the
recompiler writes).

The engine also runs as a coroutine, on a stack of its own: the game
switches to it by saving the stack pointer and loading the engine's
(`tsx / stx $6494 / ldx $6495 / txs`), pulls what the engine pushed before
it switched away, and returns where the engine was; the engine switches back
the same way from where it polls. `thread $d11a` gives where the engine
starts (the first switch jumps there, on a new stack). The stack analysis
takes a TXS of an unknown value as a switch, and the IR (`src/ir/switch.mbt`)
suspends the coroutine that switches away: with exact returns, the switch's
RTS goes to an address that no call of this coroutine waits for. The first
call that sees it is the one whose return address the switch left on its own
stack: it suspends there, waiting for that address, and the calls down to
the reset routine suspend too, as at a wait loop. The runtime keeps that
coroutine's resume points and resumes the one waiting for the address the
switch returned to, or starts one, from the reset routine, which runs the
coroutine `cpu_thread`. Over the recording the game switches 3,905 times
while the engine answers three moves.

## Kirby's Adventure: soft resets, pushed returns, operands in RAM

Kirby's NMI handler can restart the game, by loading the stack pointer or
jumping to the reset routine's entry (a soft reset): the runtime then drops
the suspended foreground and the reset routine runs the restart. Its IRQ
vector points to RAM, where the game writes the IRQ's code with operands it
changes (`ram $0029 48 a9 .. 8d 00 e0 8d .. .. 4c .. ..`: an `lda` of a byte
written there, a `jmp` to an address written there, a computed jump). Calls
into other banks go through `jsr far_call` followed by the routine's address
and bank (`inline $d805 3`).

A routine can push an address and go on (`lda #>back-1 / pha / lda #<back-1
/ pha`, then code that returns): discovery takes the second push as a call
of the code after it, which returns to `back`, leaving the same bytes on the
guest stack. Its slopes use tables of routines split in two, low and high
bytes apart (`code $f261 slope_moves_f261 12 rts high $f26d`).

With 64 banks in two windows, most of the map is the routines in the windows
that code outside them reaches, in the banks the game maps there, and the
tables the pointers of computed jumps are loaded from: generated from the
code, the far calls and a recording of the interpreter (`--profile`), then
checked by discovery, which rejects guesses that lead to bytes no code has.
