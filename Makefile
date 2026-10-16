CC = clang

CFLAGS = -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-unused-value -Wconversion -Wsign-conversion -Wno-missing-braces
CFLAGS += -I./raylib-quickstart/build/external/raylib-master/src
CFLAGS += -O3 -std=c99
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -ferror-limit=0
CFLAGS += $(EXTRA_CFLAGS) # e.g. EXTRA_CFLAGS=-DNATIVE_CALLS

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

SOURCES += codegen/lib/instructions.c
SOURCES += codegen/lib/code.c
SOURCES += codegen/lib/data.c
SOURCES += codegen/lib/cpu.c
SOURCES += codegen/lib/video.c
SOURCES += codegen/lib/render.c
SOURCES += codegen/native/display.c
SOURCES += codegen/lib/apu.c
SOURCES += codegen/lib/state.c
SOURCES += codegen/lib/debug_chr.c
MAIN = codegen/main.c

.PHONY: codegen clean gpu-check

codegen:
	moon run src/main -- --native codegen/native/routines.txt

build: clean
	$(CC) $(CFLAGS) -o smb ${MAIN} codegen/gpu.c $(SOURCES) codegen/lib/common.c $(GC_FLAGS) $(RAYLIB_FLAGS) $(OBJECTS)

# Compares the GPU renderer with the software renderer over the replay
# (needs OpenGL 3.3; under Xvfb: xvfb-run ./gpu_check --debug-chr).
gpu-check: clean
	$(CC) $(CFLAGS) -o gpu_check codegen/gpu_check.c codegen/gpu.c $(SOURCES) codegen/lib/rec.c codegen/lib/common.c $(GC_FLAGS) $(RAYLIB_FLAGS) $(OBJECTS)

wasm: clean
	$(CC) -O3 -ffunction-sections -fdata-sections --target=wasm32 -nostdlib -Wl,--gc-sections -Wl,--import-memory -Wl,--export-all -Wl,--no-entry -Wl,--allow-undefined $(EXTRA_CFLAGS) -o web/smb.wasm $(SOURCES)

hash: clean
	$(CC) $(CFLAGS) -o hash codegen/hash.c $(SOURCES) codegen/lib/rec.c codegen/lib/common.c $(GC_FLAGS)

clean:
	rm -f smb hash gpu_check smb.wasm
