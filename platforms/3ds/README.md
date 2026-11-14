# Nintendo 3DS

The recompiled SMB on the Nintendo 3DS, in stereoscopic 3D, built with
[Citronnade](https://github.com/nathsou/citronnade): a Rust toolchain for
3DS homebrew that needs neither devkitPro nor a C library.

The game is the same generated C as the desktop and web versions, compiled
for the 3DS's ARM11 by `build.rs` and driven through the same functions
(`cpu_init`, `next_frame`, ...). Each VBlank runs one frame; the APU's
samples go to the DSP.

## The 3D

`codegen/lib/layers.c` draws each frame as two images instead of one: the
background, each pixel on a plane chosen from the game's own level data
(its block buffer, by the map's names, as the web themes read it), and the
sprites. The GPU then draws them as textured quads, back to front, each
plane shifted by its depth for each eye:

| plane | what | depth |
|---|---|---|
| behind | hills, clouds, bushes, castles | 3 px behind the screen |
| on | ground, bricks, blocks, pipes, coins | on the screen |
| fixed | the status bar | on the screen |
| sprites | Mario, enemies, items | 1.5 px in front |

(depths at full 3D slider, for the "normal" preset; the bottom screen
offers flat, subtle, normal and deep). With the slider down the picture is
the flat picture, pixel for pixel. Frames that change the picture
mid-screen (none in SMB's levels) fall back to the flat picture. A frame
takes 7.5 to 9.5 ms of the 16.7 ms budget, measured in the Azahar emulator.

## Building

1. Recompile the game: put your `smb.nes` in the repository's root and run
   `make codegen` (see [Codegen](../../README.md#codegen)). `build.rs`
   compiles `codegen/game/` and reads the ROM's CHR from `../../smb.nes`
   (or the path in `SMB_ROM`).
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
   contains the game's code and tiles: keep it to yourself.

4. Copy it to `/3ds/` on the SD card and start it from the Homebrew
   Launcher, or send it over Wi-Fi with `cargo citronnade run --release`
   (the launcher's netloader: press Y).

Sound needs the console's DSP firmware at `/3ds/dspfirm.cdc` (dump it with
the DSP1 homebrew).

## Controls

- Circle Pad or D-pad: move
- A: jump; B or Y: run, fire
- START, SELECT
- R: layers on or off (the flat picture)
- START + SELECT + L or R: quit

The bottom screen's buttons save the game's state to
`/3ds/smb-3ds/state.bin` on the SD card and load it back, choose the 3D
depth, and turn the layers on or off. On a New 3DS the game asks for the
faster CPU mode.

The icon (`icon.png`, three stacked planes) is this project's own.
