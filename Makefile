CC = clang

CFLAGS = -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-unused-value -Wconversion -Wsign-conversion -Wno-missing-braces
CFLAGS += -I./raylib-quickstart/build/external/raylib-master/src
CFLAGS += -O3 -std=c99
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -ferror-limit=0
# The game: smb (Super Mario Bros.), metroid or smb3, recompiled from your
# ROM.
GAME ?= smb
ifeq ($(GAME),metroid)
ROM ?= metroid.nes
MAP = maps/metroid.map
GAME_DIR = codegen/metroid
NATIVE_ROUTINES =
# it drives the PPU through its registers and switches banks (MMC1)
GAME_FLAGS = -DPPU_BUS -DMAPPER -DGAME_TITLE='"Metroid"' -DROM_PATH='"$(ROM)"' -DSAVE_FILE='"metroid.sav"'
GAME_SOURCES = codegen/lib/ppu_bus.c codegen/lib/mapper.c codegen/lib/cartridge.c
else ifeq ($(GAME),smb3)
ROM ?= smb3.nes
MAP = maps/smb3.map
GAME_DIR = codegen/smb3
NATIVE_ROUTINES =
# the PPU registers too, and MMC3's banks and scanline IRQ
GAME_FLAGS = -DPPU_BUS -DMAPPER -DGAME_TITLE='"Super Mario Bros. 3"' -DROM_PATH='"$(ROM)"' -DSAVE_FILE='"smb3.sav"'
GAME_SOURCES = codegen/lib/ppu_bus.c codegen/lib/mapper.c codegen/lib/cartridge.c
else
ROM ?= smb.nes
MAP = maps/smb.map
GAME_DIR = codegen/game
NATIVE_ROUTINES = --native codegen/native/routines.txt
GAME_FLAGS =
GAME_SOURCES = codegen/native/display.c
endif

# the generated program (make codegen) and the runtime it includes
CFLAGS += -I$(GAME_DIR) -Icodegen/lib
CFLAGS += $(GAME_FLAGS) $(EXTRA_CFLAGS)

# Platform-specific flags
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    GC_FLAGS = -Wl,--gc-sections
    RAYLIB_FLAGS = -lGL -lm -lpthread -ldl -lrt -lX11
else ifeq ($(UNAME_S),Darwin) # macOS
    GC_FLAGS = -Wl,-dead_strip
    RAYLIB_FLAGS = -framework OpenGL -framework Cocoa -framework IOKit -framework CoreFoundation -framework QuartzCore -lm -lpthread
endif

OBJECTS = raylib-quickstart/bin/Debug/libraylib.a

SOURCES += $(GAME_DIR)/code.c
SOURCES += $(GAME_DIR)/data.c
SOURCES += codegen/lib/cpu.c
SOURCES += codegen/lib/video.c
SOURCES += codegen/lib/render.c
SOURCES += $(GAME_SOURCES)
SOURCES += codegen/lib/apu.c
SOURCES += codegen/lib/sound.c
SOURCES += codegen/lib/state.c
SOURCES += codegen/lib/debug_chr.c
MAIN = codegen/main.c

.PHONY: codegen header clean gpu-check runtime-wasm runtime-cartridge-wasm apu-wasm game-wasm web replay interpreter

# The game is recompiled from your ROM into codegen/game, or codegen/metroid
# or codegen/smb3 with GAME=metroid or GAME=smb3 (not in the repository).
codegen:
	moon run src/main -- --rom $(ROM) --map $(MAP) $(NATIVE_ROUTINES) --output $(GAME_DIR)

# The map's names for the hand-written code (committed; no ROM needed).
header:
	moon run src/main -- --map maps/smb.map --header codegen/native/smb.h

build: clean
	$(CC) $(CFLAGS) -o $(GAME) ${MAIN} codegen/gpu.c $(SOURCES) codegen/lib/common.c $(GC_FLAGS) $(RAYLIB_FLAGS) $(OBJECTS)

# Replays a recording headlessly (games that drive the PPU registers, see
# codegen/replay.c): ./replay metroid.nes --rec rec/metroid.rec
replay:
	$(CC) $(CFLAGS) -o replay codegen/replay.c $(SOURCES) codegen/lib/rec.c codegen/lib/state_hash.c $(GC_FLAGS) -lm

# The reference interpreter replay is checked against (tests/interpreter.c):
# ./interpreter metroid.nes --waits ... --rec rec/metroid.rec
interpreter:
	$(CC) $(CFLAGS) -o interpreter tests/interpreter.c codegen/lib/cpu.c codegen/lib/video.c codegen/lib/render.c $(GAME_SOURCES) codegen/lib/apu.c codegen/lib/sound.c codegen/lib/prg.c codegen/lib/rec.c codegen/lib/state_hash.c $(GC_FLAGS) -lm

# Compares the GPU renderer with the software renderer over the replay
# (needs OpenGL 3.3; under Xvfb: xvfb-run ./gpu_check --debug-chr, or
# xvfb-run ./gpu_check --every 25 --variations with GAME=metroid).
gpu-check: clean
	$(CC) $(CFLAGS) -o gpu_check codegen/gpu_check.c codegen/gpu.c $(SOURCES) codegen/lib/rec.c codegen/lib/common.c $(GC_FLAGS) $(RAYLIB_FLAGS) $(OBJECTS)

wasm: clean
	$(CC) -O3 -ffunction-sections -fdata-sections --target=wasm32 -nostdlib -Wl,--gc-sections -Wl,--import-memory -Wl,--export-all -Wl,--no-entry -Wl,--allow-undefined $(EXTRA_CFLAGS) -Icodegen/game -Icodegen/lib -o web/smb.wasm $(SOURCES)

# The split WebAssembly build: the C runtime as one module (no game code,
# no ROM data), and the game module recompiled from the ROM, linked at load
# time by web/link.js. Check it with `node tests/web/replay_split.mjs`.
RUNTIME_SOURCES = codegen/lib/cpu.c codegen/lib/video.c codegen/lib/render.c codegen/lib/apu.c codegen/lib/apu_buffers.c codegen/lib/sound.c codegen/lib/state.c codegen/lib/debug_chr.c codegen/lib/prg.c codegen/native/display.c

runtime-wasm:
	$(CC) -O3 -ffunction-sections -fdata-sections --target=wasm32 -nostdlib -Wl,--gc-sections -Wl,--import-memory -Wl,--export-all -Wl,--no-entry -Wl,--allow-undefined $(EXTRA_CFLAGS) -o web/runtime.wasm $(RUNTIME_SOURCES)

# The runtime for cartridges that drive the PPU registers and switch banks
# (Metroid, SMB3): no native routines, the register interface and the
# mappers (MMC1, MMC3) instead.
CARTRIDGE_RUNTIME_SOURCES = codegen/lib/cpu.c codegen/lib/video.c codegen/lib/render.c codegen/lib/apu.c codegen/lib/apu_buffers.c codegen/lib/sound.c codegen/lib/state.c codegen/lib/debug_chr.c codegen/lib/prg.c codegen/lib/ppu_bus.c codegen/lib/mapper.c codegen/lib/state_hash.c

runtime-cartridge-wasm:
	$(CC) -O3 -ffunction-sections -fdata-sections --target=wasm32 -nostdlib -DPPU_BUS -DMAPPER -Wl,--gc-sections -Wl,--import-memory -Wl,--export-all -Wl,--no-entry -Wl,--allow-undefined $(EXTRA_CFLAGS) -o web/runtime-cartridge.wasm $(CARTRIDGE_RUNTIME_SOURCES)

# The synthesizer alone, for the page's AudioWorklet (its own memory).
apu-wasm:
	$(CC) -O3 --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--allow-undefined -Wl,--export=apu_init -Wl,--export=apu_push_frame -Wl,--export=apu_render -Wl,--export=apu_input -Wl,--export=apu_output $(EXTRA_CFLAGS) -o web/apu.wasm codegen/lib/apu.c codegen/lib/apu_buffers.c

# Bakes the runtime's addresses into the game module (as the page does).
game-wasm: runtime-wasm
	node web/layout.mjs web/runtime.wasm > web/layout.txt
	moon run src/main -- --rom $(ROM) --map maps/smb.map --native codegen/native/routines.txt --wasm web/game.wasm --wasm-layout web/layout.txt

# The page: the C runtime, the recompiler compiled to JavaScript, and the
# inputs it recompiles the player's ROM with. Serve web/ over HTTP.
web: runtime-wasm runtime-cartridge-wasm apu-wasm
	moon build --target js --release
	cp _build/js/release/build/web/web.js web/recompiler.js
	cp maps/smb.map web/smb.map
	cp maps/metroid.map web/metroid.map
	cp codegen/native/routines.txt web/routines.txt
	cp codegen/native/display.c web/display.c

hash: clean
	$(CC) $(CFLAGS) -o hash codegen/hash.c $(SOURCES) codegen/lib/rec.c codegen/lib/common.c $(GC_FLAGS)

clean:
	rm -f smb metroid smb3 hash gpu_check replay interpreter smb.wasm audio_queue
