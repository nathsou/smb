#include "ppu_bus.h"
#include "cpu.h"
#include "external.h"

uint8_t oam_addr;
uint8_t ppu_write_toggle;
uint16_t ppu_vram_addr;
uint8_t ppu_read_buffer;
bool ppu_status_clear;
uint16_t ppu_temp_addr;
uint8_t ppu_fine_x;
bool ppu_address_written;
uint16_t ppu_address_set;
#define write_toggle ppu_write_toggle
#define vram_addr ppu_vram_addr
#define read_buffer ppu_read_buffer
#define status_clear ppu_status_clear

static uint16_t vram_increment(void) {
    return video.ctrl & VIDEO_CTRL_INCREMENT_32 ? 32 : 1;
}

uint8_t ppu_bus_read(uint16_t addr) {
    switch (0x2000 + (addr & 7)) {
        case 0x2002:
            // Frame-based model: there is no beam, so alternate the vblank
            // and sprite 0 bits to let polling loops make progress.
            status_clear = !status_clear;
            write_toggle = 0;
            return status_clear ? 0 : 0b11000000;
        case 0x2004:
            return video.oam[oam_addr];
        case 0x2007: {
            uint8_t value = read_buffer;
            read_buffer = video_read(vram_addr);
            vram_addr += vram_increment();
            return value;
        }
        default:
            return 0;
    }
}

void ppu_bus_write(uint16_t addr, uint8_t value) {
    switch (addr & 7) {
        case 0:
            video.ctrl = value;
            ppu_temp_addr = (uint16_t)((ppu_temp_addr & ~0x0c00) | ((value & 3) << 10));
            break;
        case 1: video.mask = value; break;
        case 3: oam_addr = value; break;
        case 4: video.oam[oam_addr++] = value; break;
        case 5:
            if (!write_toggle) {
                video.scroll_x = value;
                ppu_temp_addr = (uint16_t)((ppu_temp_addr & ~0x001f) | (value >> 3));
                ppu_fine_x = value & 7;
            } else {
                video.scroll_y = value;
                ppu_temp_addr = (uint16_t)((ppu_temp_addr & ~0x73e0) | ((value & 7) << 12) | ((value & 0xf8) << 2));
            }
            write_toggle ^= 1;
            break;
        case 6:
            if (write_toggle) {
                vram_addr = (uint16_t)((vram_addr & 0xff00) | value);
                ppu_temp_addr = (uint16_t)((ppu_temp_addr & 0xff00) | value);
                ppu_address_written = true;
                ppu_address_set = ppu_temp_addr;
            } else {
                vram_addr = (uint16_t)((value << 8) | (vram_addr & 0xff));
                ppu_temp_addr = (uint16_t)((ppu_temp_addr & 0x00ff) | ((value & 0x3f) << 8));
            }
            write_toggle ^= 1;
            break;
        case 7:
            video_write(vram_addr, value);
            vram_addr += vram_increment();
            break;
        default: break; // PPUSTATUS is read-only
    }
}

void ppu_bus_oam_dma(uint8_t page) {
    memcpy(video.oam, ram + ((page << 8) & 0x7ff), OAM_SIZE); // RAM mirrors every 2 KiB
}
