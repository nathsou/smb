CC = clang

CFLAGS = -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-unused-value -Wconversion -Wsign-conversion -Wno-missing-braces
CFLAGS += -I./raylib-quickstart/build/external/raylib-master/src
CFLAGS += -O3 -std=c99

RAYLIB_FLAGS = -lGL -lm -lpthread -ldl -lrt -lX11
OBJECTS = raylib-quickstart/bin/Debug/libraylib.a

SOURCES += out/instructions.c
SOURCES += out/code.c
SOURCES += out/data.c
SOURCES += out/cpu.c
SOURCES += out/ppu.c
MAIN = out/main.c

.PHONY: codegen clean

codegen:
	moon run src/main

build: clean
	$(CC) $(CFLAGS) -o smb ${MAIN} $(SOURCES) $(RAYLIB_FLAGS) $(OBJECTS)

wasm:
	clang -O3 --target=wasm32 -nostdlib -Wl,--import-memory -Wl,--export-all -Wl,--no-entry -Wl,--allow-undefined -o web/smb.wasm $(SOURCES)

clean:
	rm -f smb
