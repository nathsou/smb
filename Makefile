CC = clang

smb:
	$(CC) -o smb out/main.c out/data.c out/code.c out/cpu.c out/instructions.c

codegen:
	moon run src/main
