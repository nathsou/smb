# Nintendo 3DS

The recompiled games on the Nintendo 3DS, in stereoscopic 3D, in one
program with a game selector, built with
[Citronnade](https://github.com/nathsou/citronnade): a Rust toolchain for
3DS homebrew that needs neither devkitPro nor a C library.

Each game is the same generated C as the desktop and web versions,
compiled for the 3DS's ARM11 by `build.rs` and driven through the same
functions (`cpu_init`, `next_frame`, ...). Each VBlank runs one frame of
the game being played; the APU's samples go to the DSP. The program holds
SMB, SMB3, Metroid, Zelda and Tetris, whichever you have recompiled: a game
left while playing keeps its place, and resumes when chosen again.

Every game links with its own copy of the runtime. `build.rs` links each
game's objects into one (`ld.lld -r`) and prefixes every symbol it defines
with the game's name (`llvm-objcopy --redefine-syms`), so their
`cpu_init`s, RAMs and pictures do not collide; `c/game.c` gives them one
interface, which the Rust side calls through a generated table.

## The 3D

`codegen/lib/layers.c` draws each frame as two images instead of one: the
background, each pixel on a plane, and the sprites. It reads the frame the
way render.c's general path does, scanline by scanline (frames split by an
interrupt handler, CHR banks, sprites two tiles high), so every game's
frames take this path. In SMB the plane comes from the game's own level
data (its block buffer, by the map's names, as the web themes read it); in
the others the background is on the screen, with SMB3's status bar fixed.
The GPU then draws the images as textured quads, back to front, each
plane shifted by its depth for each eye:

| plane | what | depth |
|---|---|---|
| behind | hills, clouds, bushes, castles (SMB) | 3 px behind the screen |
| playfield | ground, bricks, blocks, pipes, coins, and the characters on them | on the screen |
| fixed | the status bar (SMB, SMB3) | 1.5 px in front, an overlay |

(depths at full 3D slider, for the "normal" preset; the bottom screen
offers flat, subtle, normal and deep). With the slider down the picture is
render.c's, pixel for pixel, in every game. A frame takes 8 to 11 ms of the
16.7 ms budget, measured in the Azahar emulator; the recompiled game itself
takes about 0.1 ms of it.

## Building

1. Recompile the games: put your ROMs in the repository's root
   (`smb.nes`, `smb3.nes`, `metroid.nes`, `zelda.nes`, `tetris.nes`) and
   run `make codegen GAME=...` for each (`make codegen` for SMB; see
   [Codegen](../../README.md#codegen)). `build.rs` builds in every game
   whose code and ROM are there, and warns about the others. It takes
   SMB's CHR from its ROM, and a cartridge's header and CHR (the PRG is in
   the recompiled code); `SMB_ROM`, `SMB3_ROM`, ... give other paths.
2. Install clang (version 18 or later, for the ARM target and `llvm-ar`)
   and Citronnade's tool:

```bash
cargo install --git https://github.com/nathsou/citronnade --branch dev cargo-citronnade
```

3. Build, in this directory:

```bash
cargo citronnade build --release
```

   This makes `target/armv6k-nintendo-3ds/release/smb-3ds.3dsx`. It
   contains the games' code and tiles: keep it to yourself.

4. Copy it to `/3ds/` on the SD card and start it from the Homebrew
   Launcher, or send it over Wi-Fi with `cargo citronnade run --release`
   (the launcher's netloader: press Y).

   To install it on the HOME Menu instead, build with `--cia` and install
   `smb-3ds.cia` with FBI (custom firmware such as Luma3DS is needed: the
   title is not signed). It installs as title `000400000F5B3000`. Like the
   `.3dsx`, it contains your games: keep it to yourself.

Sound needs the console's DSP firmware at `/3ds/dspfirm.cdc` (dump it with
the DSP1 homebrew).

## Controls

In the selector: Up and Down or the stylus choose a game, A starts or
resumes it, START quits.

In a game:

- Circle Pad or D-pad, A, B (or Y), START, SELECT: the NES controller
- R: layers on or off (the flat picture, drawn by the CPU)
- START + SELECT + L or R: quit

The bottom screen's buttons save the game's state to
`/3ds/smb-3ds/GAME.state` on the SD card and load it back, choose the 3D
depth, turn the layers on or off, and go back to the selector (Games). On
a New 3DS the program asks for the faster CPU mode.

The icon (`icon.png`, three stacked planes) is this project's own.
