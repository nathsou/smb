#ifndef SMB_CARTRIDGE_H
#define SMB_CARTRIDGE_H

#include <stddef.h>
#include <stdint.h>

// The pattern tables from an iNES file: its CHR ROM, or, for boards with
// CHR RAM (no CHR ROM), cleared RAM that the game fills itself; and its
// mapper (`cartridge_mapper`, for mapper_init). The PRG ROM is the
// recompiled program's own (data.c). Returns 0 on success.
extern uint8_t cartridge_mapper;
// The board's mirroring, for NROM (mapper_mirroring): vertical or not.
extern uint8_t cartridge_vertical;
int load_cartridge(const char *rom_path);
// The same from an iNES image in memory (the PRG banks may be left out,
// with the header's PRG count zero). Needs no C library (the 3DS).
int load_cartridge_image(const uint8_t *rom, size_t size);

#endif
