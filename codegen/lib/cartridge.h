#ifndef SMB_CARTRIDGE_H
#define SMB_CARTRIDGE_H

// The pattern tables from an iNES file: its CHR ROM, or, for boards with
// CHR RAM (no CHR ROM), cleared RAM that the game fills itself. The PRG ROM
// is the recompiled program's own (data.c). Returns 0 on success.
int load_cartridge(const char *rom_path);

#endif
