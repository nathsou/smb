CC = clang
CFLAGS = -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-unused-value
CFLAGS += -I./raylib-quickstart/build/external/raylib-master/src
RAYLIB_FLAGS = -lGL -lm -lpthread -ldl -lrt -lX11
SOURCES = $(wildcard src/*.c)
SOURCES += $(wildcard out/*.c)
OBJECTS = raylib-quickstart/bin/Debug/libraylib.a

.PHONY: smb codegen clean

build: clean
	$(CC) $(CFLAGS) -o smb $(SOURCES) $(RAYLIB_FLAGS) $(OBJECTS)

codegen:
	moon run src/main

clean:
	rm -f smb
