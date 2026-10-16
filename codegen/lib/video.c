#include "video.h"
#include "external.h"
#include <stddef.h>

Video video;
uint8_t chr_rom[CHR_ROM_SIZE];

// Byte offsets of the Video fields, for hosts that read it from memory (web).
const uint32_t video_layout[] = {
    offsetof(Video, nametable), offsetof(Video, palette), offsetof(Video, oam),
    offsetof(Video, ctrl), offsetof(Video, mask), offsetof(Video, scroll_x),
    offsetof(Video, status_bar),
};

void video_init(const uint8_t *chr) {
    memcpy(chr_rom, chr, CHR_ROM_SIZE);
}

// $3F10/$3F14/$3F18/$3F1C alias the backdrop entries $3F00/$3F04/$3F08/$3F0C.
static uint8_t *palette_entry(uint16_t addr) {
    if (addr == 0x3f10 || addr == 0x3f14 || addr == 0x3f18 || addr == 0x3f1c) {
        return &video.palette.u8[addr - 0x3f10];
    }
    return &video.palette.u8[(addr - 0x3f00) & 31];
}

uint8_t video_read(uint16_t addr) {
    if (addr < 0x2000) return chr_rom[addr];
    if (addr < 0x3f00) return video.nametable[(addr - 0x2000) & 0x7ff];
    if (addr < 0x4000) return *palette_entry(addr);
    return 0;
}

void video_write(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000 && addr < 0x3f00) {
        video.nametable[(addr - 0x2000) & 0x7ff] = value;
    } else if (addr >= 0x3f00 && addr < 0x4000) {
        *palette_entry(addr) = value;
    }
}
