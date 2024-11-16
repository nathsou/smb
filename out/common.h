#ifndef SMB_COMMON_H
#define SMB_COMMON_H

#include "code.h"
#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "external.h"

#define ROM_PATH "Super Mario Bros.nes"
#define CHR_ROM_SIZE 8192 // 8KB
#define NES_HEADER_SIZE 16
#define PRG_ROM_SIZE 32768  // 32KB
#define WINDOW_SCALE 3
#define WINDOW_WIDTH SCREEN_WIDTH * WINDOW_SCALE
#define WINDOW_HEIGHT SCREEN_HEIGHT * WINDOW_SCALE

int read_chr_rom(void);

#endif
