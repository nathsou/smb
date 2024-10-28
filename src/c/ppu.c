#include "ppu.h"
#include "cpu.h"
#include <stdio.h>

uint8_t chr_rom[8192];
uint8_t nametable[2048];
uint8_t palette_table[32];
uint8_t oam[256];

uint16_t ppu_v;
uint8_t ppu_w;
uint8_t ppu_f;

uint8_t ppu_ctrl;
uint8_t ppu_mask;
uint8_t ppu_status;
uint8_t oam_addr;
uint8_t oam_data;
uint8_t ppu_scroll_x;
uint8_t ppu_scroll_y;

uint16_t vram_addr;
uint8_t vram_internal_buffer;
uint8_t oam_dma;

uint8_t frame[SCREEN_WIDTH * SCREEN_HEIGHT * 3];

// 64 RGB colors
uint8_t COLOR_PALETTE[] = {
   0x80, 0x80, 0x80, 0x00, 0x3D, 0xA6, 0x00, 0x12, 0xB0, 0x44, 0x00, 0x96, 0xA1, 0x00, 0x5E,
   0xC7, 0x00, 0x28, 0xBA, 0x06, 0x00, 0x8C, 0x17, 0x00, 0x5C, 0x2F, 0x00, 0x10, 0x45, 0x00,
   0x05, 0x4A, 0x00, 0x00, 0x47, 0x2E, 0x00, 0x41, 0x66, 0x00, 0x00, 0x00, 0x05, 0x05, 0x05,
   0x05, 0x05, 0x05, 0xC7, 0xC7, 0xC7, 0x00, 0x77, 0xFF, 0x21, 0x55, 0xFF, 0x82, 0x37, 0xFA,
   0xEB, 0x2F, 0xB5, 0xFF, 0x29, 0x50, 0xFF, 0x22, 0x00, 0xD6, 0x32, 0x00, 0xC4, 0x62, 0x00,
   0x35, 0x80, 0x00, 0x05, 0x8F, 0x00, 0x00, 0x8A, 0x55, 0x00, 0x99, 0xCC, 0x21, 0x21, 0x21,
   0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0xFF, 0xFF, 0xFF, 0x0F, 0xD7, 0xFF, 0x69, 0xA2, 0xFF,
   0xD4, 0x80, 0xFF, 0xFF, 0x45, 0xF3, 0xFF, 0x61, 0x8B, 0xFF, 0x88, 0x33, 0xFF, 0x9C, 0x12,
   0xFA, 0xBC, 0x20, 0x9F, 0xE3, 0x0E, 0x2B, 0xF0, 0x35, 0x0C, 0xF0, 0xA4, 0x05, 0xFB, 0xFF,
   0x5E, 0x5E, 0x5E, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0xFF, 0xFF, 0xFF, 0xA6, 0xFC, 0xFF,
   0xB3, 0xEC, 0xFF, 0xDA, 0xAB, 0xEB, 0xFF, 0xA8, 0xF9, 0xFF, 0xAB, 0xB3, 0xFF, 0xD2, 0xB0,
   0xFF, 0xEF, 0xA6, 0xFF, 0xF7, 0x9C, 0xD7, 0xE8, 0x95, 0xA6, 0xED, 0xAF, 0xA2, 0xF2, 0xDA,
   0x99, 0xFF, 0xFC, 0xDD, 0xDD, 0xDD, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
};

void init_ppu(uint8_t* chr) {
    for (int i = 0; i < 8192; i++) {
        chr_rom[i] = chr[i];
    }
}

bool status_clear = false;

uint8_t read_ppu_register(uint16_t addr) {
    switch (addr) {
        case 0x2002: {
            // in SMB, the status register is only used to:
            // 1. clear the write flip flop (ppu_w)
            // 2. detect vblank
            // 3. detect sprite 0 hit

            // items 2 and 3 are used for timing purposes
            // since we're rendering the entire frame at once, we can ignore them
            // however, we can't always return the same value
            // as some loops wait for a flag to be set or cleared

            status_clear = !status_clear;
            
            ppu_w = 0;
            // vblank and sprite 0 hit always set
            return status_clear ? 0 : 0b11000000;
        }
        case 0x2004: return oam_data;
        case 0x2007: {
            uint8_t value = vram_internal_buffer;
            vram_internal_buffer = read_ppu(vram_addr);
            uint16_t increment = ppu_ctrl & 0b100 ? 32 : 1;
            vram_addr += increment;
            return value;
        }
        default: return 0;
    }
}

void write_ppu_register(uint16_t addr, uint8_t value) {
    switch (addr) {
        case 0x2000: {
            ppu_ctrl = value;
            break;
        }
        case 0x2001: {
            ppu_mask = value;
            break;
        }
        case 0x2003: {
            oam_addr = value;
            break;
        }
        case 0x2004: {
            oam_addr++;
            oam_data = value;
            break;
        }
        case 0x2005: {
            if (!ppu_w) {
                ppu_scroll_x = value;
                ppu_w = 1;
            } else {
                ppu_scroll_y = value;
                ppu_w = 0;
            }
            break;
        }
        case 0x2006: {
            if (ppu_w) {
                // low byte
                vram_addr = (vram_addr & 0xff00) | value;
                ppu_w = 0;
            } else {
                // high byte
                vram_addr = ((((uint16_t)value) << 8) & 0xff00) | (vram_addr & 0xff);
                ppu_w = 1;
            }
            break;
        }
        case 0x2007: {
            write_ppu(vram_addr, value);
            uint16_t increment = ppu_ctrl & 0b100 ? 32 : 1;
            vram_addr += increment;
            break;
        }
    }
}

// https://www.nesdev.org/wiki/PPU_memory_map
uint8_t read_ppu(uint16_t addr) {
    if (addr < 0x2000) {
        return chr_rom[addr];
    }
    
    if (addr < 0x3f00) {
        return nametable[addr - 0x2000];
    }

    if (addr == 0x3f10 || addr == 0x3f14 || addr == 0x3f18 || addr == 0x3f1c) {
        return palette_table[addr - 0x3f10];
    }
    
    if (addr < 0x4000) {
        return palette_table[(addr - 0x3f00) & 31];
    }

    return 0;
}

void write_ppu(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000 && addr < 0x3f00) {
        nametable[addr - 0x2000] = value;
    } else if (addr == 0x3f10 || addr == 0x3f14 || addr == 0x3f18 || addr == 0x3f1c) {
        palette_table[addr - 0x3f10] = value;
    } else if (addr < 0x4000) {
        palette_table[(addr - 0x3f00) & 31] = value;
    }
}

void set_pixel(size_t x, size_t y, uint8_t r, uint8_t g, uint8_t b) {
    size_t index = (y * SCREEN_WIDTH + x) * 3;
    frame[index] = r;
    frame[index + 1] = g;
    frame[index + 2] = b;
}

size_t get_background_palette_index(size_t tile_col, size_t tile_row, size_t nametable_offset) {
    size_t attr_table_index = (tile_row / 4) * 8 + (tile_col / 4);
    // the attribute table is stored after the nametable (960 bytes)
    size_t attr_table_byte = nametable[nametable_offset + 960 + attr_table_index];
    size_t block_x = (tile_col % 4) / 2;
    size_t block_y = (tile_row % 4) / 2;
    size_t shift = block_y * 4 + block_x * 2;

    return (attr_table_byte >> shift) & 0b11;
}

void draw_tile(size_t n, size_t x, size_t y, size_t bank_offset, size_t palette_idx) {
    for (size_t tile_y = 0; tile_y < 8; tile_y++) {
        uint8_t plane1 = chr_rom[bank_offset + n * 16 + tile_y];
        uint8_t plane2 = chr_rom[bank_offset + n * 16 + tile_y + 8];

        for (size_t tile_x = 0; tile_x < 8; tile_x++) {
            uint8_t bit0 = plane1 & 1;
            uint8_t bit1 = plane2 & 1;
            uint8_t color_index = (uint8_t)((bit1 << 1) | bit0);
            
            plane1 >>= 1;
            plane2 >>= 1;

            uint8_t palette_offset;

            if (color_index == 0) {
                // Universal background color
                palette_offset = palette_table[0];
            } else {
                palette_offset = palette_table[palette_idx * 4 + color_index];
            }

            uint8_t r = COLOR_PALETTE[palette_offset * 3];
            uint8_t g = COLOR_PALETTE[palette_offset * 3 + 1];
            uint8_t b = COLOR_PALETTE[palette_offset * 3 + 2];

            set_pixel(x + (7 - tile_x), y + tile_y, r, g, b);
        }
    }
}

void render_background(void) {
    // see https://austinmorlan.com/posts/nes_rendering_overview/
    size_t bank_offset = ppu_ctrl & 0b10000 ? 0x1000 : 0;
    size_t nametable_offset;

    switch (ppu_ctrl & 0b11) {
        case 0: nametable_offset = 0x000; break;
        case 1: nametable_offset = 0x400; break;
        case 2: nametable_offset = 0x800; break;
        case 3: nametable_offset = 0xc00; break;
    }

    for (size_t i = 0; i < 32 * 30; i++) {
        uint8_t tile = nametable[nametable_offset + i];
        uint8_t tile_x = i % 32;
        uint8_t tile_y = (uint8_t)(i / 32);
        size_t palette_index = get_background_palette_index(tile_x, tile_y, nametable_offset);
        draw_tile(tile, tile_x * 8, tile_y * 8, bank_offset, palette_index);
    }
}

void render_ppu(void) {
    // clear screen
    for (size_t i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT * 3; i++) {
        frame[i] = 0;
    }
    
    render_background();
}
