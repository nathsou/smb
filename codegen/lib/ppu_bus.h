#ifndef SMB_PPU_BUS_H
#define SMB_PPU_BUS_H

#include <stdint.h>
#include "video.h"

// NES PPU register interface ($2000-$2007, OAM DMA at $4014) on top of the
// Video state, for programs that talk to the hardware registers (Metroid,
// the test fixtures; builds define PPU_BUS). SMB's build does not link it:
// its display code writes Video directly, so any register access left in
// its generated code is a link error.

extern uint8_t oam_addr;
// The latches between register accesses (saved with the state).
extern uint8_t ppu_write_toggle; // shared $2005/$2006 first/second write latch
extern uint16_t ppu_vram_addr;
extern uint8_t ppu_read_buffer;
extern bool ppu_status_clear;

uint8_t ppu_bus_read(uint16_t addr);
void ppu_bus_write(uint16_t addr, uint8_t value);
void ppu_bus_oam_dma(uint8_t page);

#endif
