#include "ppu.h"
#include "cpu.h"

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

uint16_t ppu_addr;
uint8_t ppu_data;
uint8_t oam_dma;

uint8_t frame[SCREEN_WIDTH * SCREEN_HEIGHT * 3];

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
                ppu_addr = (ppu_addr & 0xff00) | value;
                ppu_w = 0;
            } else {
                // high byte
                ppu_addr = ((((uint16_t)value) << 8) & 0xff00) | (ppu_addr & 0xff);
                ppu_w = 1;
            }
            break;
        }
        case 0x2007: {
            write_byte(ppu_addr, value);
            uint16_t increment = ppu_ctrl & 0b100 ? 32 : 1;
            ppu_addr += increment;
            break;
        }
    }
}

uint8_t read_ppu(uint16_t addr) {
    return 0;
}

void write_ppu(uint16_t addr, uint8_t value) {

}

void set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    int index = (y * SCREEN_WIDTH + x) * 3;
    frame[index] = r;
    frame[index + 1] = g;
    frame[index + 2] = b;
}

void draw_tile(size_t n, size_t x, size_t y) {
    for (int tile_y = 0; tile_y < 8; tile_y++) {
        uint8_t high = chr_rom[n * 16 + tile_y];
        uint8_t low = chr_rom[n * 16 + tile_y + 8];

        for (int tile_x = 7; tile_x >= 0; tile_x--) {
            uint8_t palette_index = ((high & 1) << 1) | (low & 1);
            low >>= 1;
            high >>= 1;

            uint8_t color = 0; // grayscale
            
            switch (palette_index) {
                case 0: color = 0; break;
                case 1: color = 85; break;
                case 2: color = 170; break;
                case 3: color = 255; break;
            }

            set_pixel(x + tile_x, y + tile_y, color, color, color);
        }
    }
}

void render_ppu(void) {
    // clear screen
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT * 3; i++) {
        frame[i] = 0;
    }
    
    // draw tiles
    for (int x = 0; x < 32; x++) {
        for (int y = 0; y < 15; y++) {
            draw_tile(y * 32 + x, x * 8, y * 8);
        }
    }
}
