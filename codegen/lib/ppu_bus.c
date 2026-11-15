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

// Splits at sprite 0 (Zelda's status bar). The map names the routine that
// waits for sprite 0's hit and then writes the scroll for the rest of the
// frame (`sprite0`), and the generated code calls ppu_sprite0 as it starts:
// the frame's top keeps the scroll set so far, and the routine's writes
// make the second part, from the scanline its delay loops end on (the
// runtime has no beam to time them). Within a frame only: not saved.
static uint8_t split_line; // where the second part starts, or 0

void ppu_frame_start(void) {
    video.split_count = 0;
    split_line = 0;
}

// The scanline after sprite 0's first opaque row (8x8 sprites).
static int sprite0_line(void) {
    const uint8_t *sprite = video.oam;
    uint16_t table = (video.ctrl & VIDEO_CTRL_SPRITE_BANK) ? 0x1000 : 0;
    int first = 0;
    for (int row = 0; row < 8; row++) {
        int src = (sprite[2] & 0x80) ? 7 - row : row;
        uint16_t address = (uint16_t)(table + sprite[1] * 16 + src);
        if (video_pattern(video.chr_banks, address) | video_pattern(video.chr_banks, (uint16_t)(address + 8))) {
            first = row;
            break;
        }
    }
    return sprite[0] + 1 + first + 1;
}

void ppu_sprite0(uint8_t lines) {
    // (not when the frame already has parts: the scanline IRQ's, MMC3)
    if (video.split_count != 0 || video.oam[0] >= 0xEF) return;
    if (!(video.mask & (VIDEO_MASK_BACKGROUND | VIDEO_MASK_SPRITES))) return; // no hit
    int line = sprite0_line() + lines;
    if (line >= SCREEN_HEIGHT) return;
    split_line = (uint8_t)line;
    // the frame's top, as the scroll was set
    video_split(0, (uint16_t)(video.scroll_x + ((video.ctrl & 1) ? 256 : 0)),
                (uint16_t)(video.scroll_y + ((video.ctrl & 2) ? 240 : 0)));
    ppu_address_written = false;
}

// After the hit: the second part, from the temporary address (and the
// address $2006 set, if it did).
static void sprite0_scroll(void) {
    if (split_line == 0) return;
    uint16_t t = ppu_temp_addr, v = ppu_address_set;
    uint16_t x = (uint16_t)(((t >> 10) & 1) * 256 + (t & 0x1f) * 8 + ppu_fine_x);
    const VideoSplit *top = &video.splits[0];
    uint16_t y = ppu_address_written
        ? (uint16_t)(((v >> 11) & 1) * 240 + ((v >> 5) & 0x1f) * 8 + ((v >> 12) & 7))
        : (uint16_t)(top->y + split_line);
    video.split_count = 1;
    video_split(split_line, x, y);
}

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
            sprite0_scroll();
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
            sprite0_scroll();
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
            sprite0_scroll();
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
