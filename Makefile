CC = clang

CFLAGS = -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-unused-value -Wconversion -Wsign-conversion -Wno-missing-braces
CFLAGS += -I./raylib-quickstart/build/external/raylib-master/src
CFLAGS += -O3 -std=c99
CFLAGS += -ferror-limit=0

# Platform-specific flags
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    RAYLIB_FLAGS = -lGL -lm -lpthread -ldl -lrt -lX11
else ifeq ($(UNAME_S),Darwin) # macOS
    RAYLIB_FLAGS = -framework OpenGL -framework Cocoa -framework IOKit -framework CoreFoundation -lm -lpthread
endif

OBJECTS = raylib-quickstart/bin/Debug/libraylib.a

SOURCES += out/instructions.c
SOURCES += out/code.c
SOURCES += out/data.c
SOURCES += out/cpu.c
SOURCES += out/ppu.c
SOURCES += out/apu.c
SOURCES += out/state.c
MAIN = out/main.c

.PHONY: codegen clean

codegen:
	moon run src/main

build: clean
	$(CC) $(CFLAGS) -o smb ${MAIN} $(SOURCES) out/common.c $(RAYLIB_FLAGS) $(OBJECTS)

wasm: clean
	$(CC) -O3 --target=wasm32 -nostdlib -Wl,--import-memory -Wl,--export-all -Wl,--no-entry -Wl,--allow-undefined -o web/smb.wasm $(SOURCES)

hash: clean
	$(CC) $(CFLAGS) -o hash out/hash.c $(SOURCES) out/rec.c out/common.c

clean:
	rm -f smb hash smb.wasm
