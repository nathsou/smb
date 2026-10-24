#ifndef SMB_VIDEO_H
#define SMB_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 240

#define CHR_ROM_SIZE 8192 // the pattern tables, $0000-$1FFF
#define CHR_MAX_SIZE 0x40000 // a cartridge's CHR ROM in 1 KiB banks (MMC3)
#define NAMETABLE_SIZE 2048
#define PALETTE_SIZE 32
#define OAM_SIZE 256

// Display control bits (the game composes these in its PPU mirror bytes).
#define VIDEO_CTRL_NAMETABLE 0x01       // right screen is the base for scroll
#define VIDEO_CTRL_INCREMENT_32 0x04    // VRAM writes advance by a row
#define VIDEO_CTRL_SPRITE_BANK 0x08     // sprites use pattern table $1000
#define VIDEO_CTRL_BACKGROUND_BANK 0x10 // background uses pattern table $1000
#define VIDEO_CTRL_SPRITE_8X16 0x20     // sprites are two tiles high
#define VIDEO_CTRL_NMI 0x80

// How the four nametables of the address space map onto the two screens.
#define MIRROR_VERTICAL 0 // $2000 = $2800, $2400 = $2C00 (screens side by side)
#define MIRROR_HORIZONTAL 1 // $2000 = $2400, $2800 = $2C00 (screens stacked)
#define MIRROR_SINGLE_LOWER 2 // all four are the first screen
#define MIRROR_SINGLE_UPPER 3 // all four are the second screen

#define VIDEO_MASK_BACKGROUND_LEFT 0x02 // draw background in the left 8 pixels
#define VIDEO_MASK_SPRITES_LEFT 0x04    // draw sprites in the left 8 pixels
#define VIDEO_MASK_BACKGROUND 0x08
#define VIDEO_MASK_SPRITES 0x10

// A part of the frame drawn with other settings, from scanline `line` on,
// which an interrupt handler set mid-frame (the MMC3's scanline IRQ).
typedef struct {
    uint8_t line; // the first scanline
    uint8_t ctrl;
    uint8_t mask;
    uint8_t chr_banks[8];
    uint8_t pad;
    uint16_t x; // horizontal scroll in the 512x480 plane of the nametables
    uint16_t y; // the plane's row at `line`
} VideoSplit;

#define VIDEO_MAX_SPLITS 8

// Everything the renderer draws from. Game code writes it directly; no
// register protocol, latches or timing are involved.
typedef struct {
    uint8_t nametable[NAMETABLE_SIZE]; // two 32x30 tile screens + attributes
    union {
        uint8_t u8[PALETTE_SIZE];
        uint32_t u32[PALETTE_SIZE / 4];
    } palette;
    uint8_t oam[OAM_SIZE]; // 64 sprites: y, tile, attributes, x
    uint8_t ctrl;
    uint8_t mask;
    uint8_t scroll_x;
    uint8_t scroll_y;
    bool status_bar; // top 32 pixels are a fixed, unscrolled status bar
    uint8_t mirroring; // MIRROR_*
    uint32_t chr_version; // changes whenever the pattern tables do
    // The 1 KiB banks of `chr_rom` at $0000-$1FFF (0-7 without a mapper).
    uint8_t chr_banks[8];
    // With splits, the frame's first part is `splits[0]`: its settings
    // (ctrl, mask, banks, scroll) replace those above.
    uint8_t split_count;
    VideoSplit splits[VIDEO_MAX_SPLITS];
} Video;

extern Video video;
// The pattern tables (8 KiB), or a cartridge's CHR ROM that `chr_banks`
// map 1 KiB at a time.
extern uint8_t chr_rom[CHR_MAX_SIZE];
extern uint32_t chr_size;
// The pattern tables are RAM (the game writes its tiles through the PPU).
extern bool chr_ram;

// 8 KiB of pattern tables.
void video_init(const uint8_t *chr);
// The CHR already in `chr_rom`, `size` bytes of it: the banks mapped reset.
void video_init_chr(uint32_t size);
// The 1 KiB banks of CHR.
unsigned video_chr_banks(void);

// A byte of the pattern tables ($0000-$1FFF) through the banks mapped.
static inline uint8_t video_pattern(const uint8_t *banks, uint16_t addr) {
    return chr_rom[((uint32_t)banks[(addr >> 10) & 7] << 10) | (addr & 0x3ff)];
}

// The offset in `video.nametable` of a nametable address ($2000-$3EFF).
uint16_t video_nametable_offset(uint16_t addr);

// VRAM address space: pattern tables, nametables (vertical mirroring), palette.
uint8_t video_read(uint16_t addr);
void video_write(uint16_t addr, uint8_t value);

#endif
