# SMB

[Play it online](https://nathsou.github.io/smb/)

Static recompilation of Super Mario Bros., Metroid and Super Mario Bros. 3
from the ROM into C and WebAssembly. The repository contains no game code or
data: the recompiler reads your own copy of the ROM, and `maps/smb.map`,
`maps/metroid.map` and `maps/smb3.map` give its routines, tables and
variables this project's own names. The page recompiles the ROM in the browser and shows the
generated C next to the game.

[![SMB C port running in the browser](res/smb-demo.png)](https://nathsou.github.io/smb)

## Controls

- D-Pad: WASD
- B: K
- A: L
- start: Enter
- select: Space
- z: Save state
- x: Load state

## Checkpoints

- [x] Static translation of the disassembly to low-level C
- [x] PPU & APU emulation layers
- [x] Convert subroutines to C functions
- [x] Convert most gotos to if statements
- [x] Remove unused flag updates
- [x] Replace PPU with direct draw calls (display state written directly, GPU renderer)
- [x] Emit instructions as C instead of instruction-emulation helpers
- [x] Infer routine interfaces (registers and flags read and produced)
- [x] Recompile from the ROM with our own names (no disassembly in the repository)
- [x] An IR shared by a C and a WebAssembly backend, with optimisation passes
- [x] Recompile in the browser, with a code viewer
- [x] Synthesise sound on the audio thread, band-limited, from per-frame register writes
- [x] A second game: Metroid (MMC1 banks, waits for the interrupt, return-address tricks)
- [x] A third game: Super Mario Bros. 3 (MMC3: two bank windows, CHR banks, frames split by the scanline IRQ)
- [ ] Registers as C locals and parameters
- [ ] Manually rewrite portions of the code to higher level C

See [toward a modern C program](docs/modern-runtime.md) for how these steps are
done and checked, [recompiling Metroid](docs/metroid.md) for the second
game and [recompiling Super Mario Bros. 3](docs/smb3.md) for the third.

## Building

### Linux & MacOS

1. Fetch the submodules:
```bash
$ git submodule update --init --recursive
```

2. Build raylib, follow the instructions [here](https://github.com/raylib-extras/raylib-quickstart)

3. Install MoonBit (see Codegen), place a legally obtained dump of SMB called
   `smb.nes` in the root folder, then recompile it and build:
```bash
$ make codegen
$ make build
```

4. Run `./smb` from the root folder (it loads `web/video.frag`, the GPU
   renderer's shader; `./smb --software` uses the software renderer;
   `--debug-chr` draws with generated placeholder tiles)

For Metroid, place a legally obtained dump called `metroid.nes` in the root
folder and add `GAME=metroid`: `make codegen GAME=metroid` recompiles it into
`codegen/metroid/` and `make build GAME=metroid` builds `./metroid` (it saves
states to `metroid.sav`). Likewise for Super Mario Bros. 3 with `smb3.nes`
and `GAME=smb3` (`codegen/smb3/`, `./smb3`).

## WebAssembly

The page recompiles your ROM in the browser: the recompiler (MoonBit compiled
to JavaScript) turns it into a WebAssembly module and C, which a code viewer
shows next to the game (SHOW CODE; the map tab is editable and recompiles).

1. Install a recent `clang` with the `wasm32` target and MoonBit (see Codegen)
2. Run `make web`
3. Serve the `web/` folder over HTTP and open `index.html`
4. Load a legally obtained dump of SMB, Metroid or SMB3, an iNES file (or
   for SMB a raw dump of the cartridge); it stays in your browser. An MMC1
   dump is recompiled as Metroid and an MMC3 dump as SMB3; loading another
   ROM switches games. An unrecognised dump (by SHA-256) still loads, with
   a warning

DEBUG TILES draws SMB with generated placeholder tiles instead of the ROM's,
and `?software` in the URL selects the software renderer instead of WebGL2.

### Themes

THEME draws the frames differently (WebGL2; ORIGINAL is the game's own
picture), in every game:

- 2D GLOBAL ILLUMINATION: fire, lava, coins and question blocks light the
  scene, with soft shadows and a bounce, by radiance cascades; daylight
  stays as in the game;
- 2.5D RAY TRACING: every pixel becomes voxels, seen from slightly above,
  with sun and fire shadows traced through them;
- 3D: the level and its characters as shapes made from the ROM's own tiles
  and level data (textured cubes, pipes as cylinders, hills, clouds and
  sprites inflated from their silhouettes), through a perspective camera.
  No model is drawn by hand: nothing of the game is in the repository.

![The original picture and the three themes, in 1-1 and 1-4](res/themes.png)

In SMB the themes know what is solid from the game's own level data (its
block buffer, by the map's names), not from the picture. In Metroid and
SMB3 the background is solid, the sprites stand in front of it, and the
light comes from open space. See [themes](docs/modern-runtime.md#themes).

## Codegen

1. Install [Moonbit](https://www.moonbitlang.com/):

```bash
$ curl -fsSL https://cli.moonbitlang.com/install/unix.sh | bash -s '0.10.17+d971670cd'
```

2. Place your `smb.nes` in the root folder and run `make codegen`; the C goes
   to `codegen/game/` (not in the repository). A raw dump of the cartridge
   (no iNES header) works too: `make codegen ROM=smb.bin`

The game is recompiled from the ROM image and `maps/smb.map`, which gives the
routines, tables and variables this project's own names (see
[recompiling from the ROM](docs/modern-runtime.md#recompiling-from-the-rom)).
The generator recovers reachable instructions and callable regions from the
vectors, builds an IR, optimises it and emits structured C and, with
`--wasm`, WebAssembly. The hand-written display routines
(`codegen/native/display.c`) use `codegen/native/smb.h`, the map's names as
a C header (`make header`, no ROM needed). See the
[analysis and migration notes](docs/recompiler-migration.md) and
[toward a modern C program](docs/modern-runtime.md).

Run `python3 tests/check_codegen.py` for the ROM-free suite (as CI does):
generator tests, and the independent NROM fixtures (from assembly and from an
iNES image) through both backends, natively and in WebAssembly. With your
`smb.nes`, append `--rom` to also recompile the game, replay the recorded
7,987 frames natively against `tests/golden/warpless.trace` (debug and ROM
tiles), run the differential test of every generated routine
(`tests/check_routines.py`) and replay the WebAssembly build
(`tests/web/replay_split.mjs`). With your `metroid.nes`, `--metroid`
recompiles Metroid and replays `rec/metroid.rec` (4,601 frames) natively and
in WebAssembly against `tests/golden/metroid.trace`, which an independent
6502 interpreter (`tests/interpreter.c`) wrote and is checked to reproduce;
with your `smb3.nes`, `--smb3` does the same for SMB3 over `rec/smb3.rec`
(3,201 frames, `tests/golden/smb3.trace`). Both also compare the page's
WebGL renderer with the software renderer and draw the themes
(`tests/web/cartridge_gpu.cjs`, Playwright). These checks do not require
Raylib, except SMB3's desktop GPU check.

The GPU renderers can be compared with the software renderer pixel for pixel:
`make gpu-check && xvfb-run ./gpu_check --debug-chr` (desktop; for Metroid
`make gpu-check GAME=metroid && xvfb-run ./gpu_check --every 25 --variations`,
for SMB3 `GAME=smb3`) and
`NODE_PATH=$(npm root -g) node tests/web/gpu_check.cjs` (WebGL2 in headless
Chromium, needs Playwright). `tests/web/themes.cjs` draws frames of the
replay with every theme and fails on WebGL errors; with a query and a
directory it saves them, e.g. `'frames=7800&rom=1' /tmp/themes`.

In SMB, calls are plain C calls. Return addresses are not kept on the
emulated stack, except by the inline-table dispatchers, which read their
table through it. Metroid reads and drops return addresses, so its calls
push them as the 6502 does (see [return addresses](docs/metroid.md#return-addresses)).

To compile another compatible assembly input into a separate directory:

```bash
moon run src/main -- --input tests/nrom128.asm --output /tmp/nrom-codegen
```

## References & Resources

- [doppelganger's disassembly](https://www.romhacking.net/documents/344/), which this project was originally built from
- [SuperMarioBros-C by MitchellSternke](https://github.com/MitchellSternke/SuperMarioBros-C)
- [Nesdev Wiki](https://www.nesdev.org/wiki/Nesdev_Wiki)
- [nessy](https://github.com/nathsou/nessy)
- [An Overview of NES Rendering by Austin Morlan](https://austinmorlan.com/posts/nes_rendering_overview/)
