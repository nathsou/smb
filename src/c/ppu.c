#include "ppu.h"

uint8_t chr_rom[8192];
uint8_t nametable[2048];
uint8_t palette_table[32];
uint8_t oam[256];

uint16_t ppu_v;
uint16_t ppu_t;
uint8_t ppu_x;
uint8_t ppu_w;
uint8_t ppu_f;

uint8_t ppu_ctrl;
uint8_t ppu_mask;
uint8_t ppu_status;
uint8_t oam_addr;
uint8_t oam_data;
uint8_t ppu_scroll;
uint8_t ppu_addr;
uint8_t ppu_data;
uint8_t oam_dma;

void init_ppu(uint8_t* chr) {
    for (int i = 0; i < 8192; i++) {
        chr_rom[i] = chr[i];
    }
}

uint8_t read_ppu_register(uint16_t addr) {
    switch (addr) {
        case 0x2002: {
            // in SMB, the status register is only used to:
            // 1. clear the write flip flop (ppu_w)
            // 2. detect vblank
            // 3. detect sprite 0 hit
            
            ppu_w = 0;
            // vblank and sprite 0 hit always set
            return 0b11000000;
        }
        case 0x2004: return oam_data;
        case 0x2007: return ppu_data;
        default: return 0;
    }
}

void write_ppu_register(uint16_t addr, uint8_t value) {

}

uint8_t read_ppu(uint16_t addr) {
    return 0;
}

void write_ppu(uint16_t addr, uint8_t value) {

}

