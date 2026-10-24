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
// The PPU's temporary address (the scroll and nametable the next frame, or
// a $2006 write mid-frame, starts from) and fine X scroll, as the hardware
// keeps them: the splits of a frame (an interrupt handler scrolling
// mid-frame) are read from them. `ppu_address_written`: a second $2006
// write since it was cleared, which set the address the frame continues
// from (`ppu_address_set`; later $2005 writes only change the next frame's).
extern uint16_t ppu_temp_addr;
extern uint8_t ppu_fine_x;
extern bool ppu_address_written;
extern uint16_t ppu_address_set;

uint8_t ppu_bus_read(uint16_t addr);
void ppu_bus_write(uint16_t addr, uint8_t value);
void ppu_bus_oam_dma(uint8_t page);

#endif
