CC = clang
CFLAGS = -g

.PHONY: smb codegen clean

smb: clean
	$(CC) $(CFLAGS) -o smb out/main.c out/data.c out/code.c out/cpu.c out/instructions.c out/ppu.c

codegen:
	moon run src/main

clean:
	rm -f smb
