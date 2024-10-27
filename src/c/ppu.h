#ifndef SMB_PPU_H
#define SMB_PPU_H

#include <stdint.h>

#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 240

extern uint8_t chr_rom[8192]; // 1 page of CHR ROM (8KB)
extern uint8_t nametable[2048]; // 2KB of nametable RAM
extern uint8_t palette_table[32]; // 32 bytes of palette RAM
extern uint8_t oam[256]; // 256 bytes of OAM RAM

extern uint16_t ppu_v; // current VRAM address
extern uint8_t ppu_w; // write toggle (1 bit)
extern uint8_t ppu_f; // even/odd frame flag (1 bit)

// PPU registers
extern uint8_t ppu_ctrl; // Control register: $2000
extern uint8_t ppu_mask; // Mask register: $2001
extern uint8_t ppu_status; // Status register: $2002
extern uint8_t oam_addr; // OAM address: $2003
extern uint8_t oam_data; // OAM data: $2004
extern uint8_t ppu_scroll_x;
extern uint8_t ppu_scroll_y;

extern uint16_t vram_addr;
extern uint8_t vram_internal_buffer; // VRAM read/write buffer
extern uint8_t oam_dma; // OAM DMA: $4014

// screen
extern uint8_t frame[SCREEN_WIDTH * SCREEN_HEIGHT * 3]; // 3 bytes per pixel (RGB)

void init_ppu(uint8_t* chr);

uint8_t read_ppu_register(uint16_t addr);
void write_ppu_register(uint16_t addr, uint8_t value);

uint8_t read_ppu(uint16_t addr);
void write_ppu(uint16_t addr, uint8_t value);
void render_ppu(void);

#endif
