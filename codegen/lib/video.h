#ifndef SMB_VIDEO_H
#define SMB_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 240

#define CHR_ROM_SIZE 8192
#define NAMETABLE_SIZE 2048
#define PALETTE_SIZE 32
#define OAM_SIZE 256

// Display control bits (the game composes these in its PPU mirror bytes).
#define VIDEO_CTRL_NAMETABLE 0x01       // right screen is the base for scroll
#define VIDEO_CTRL_INCREMENT_32 0x04    // VRAM writes advance by a row
#define VIDEO_CTRL_SPRITE_BANK 0x08     // sprites use pattern table $1000
#define VIDEO_CTRL_BACKGROUND_BANK 0x10 // background uses pattern table $1000
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
} Video;

extern Video video;
extern uint8_t chr_rom[CHR_ROM_SIZE]; // pattern tables (8 KiB)
// The pattern tables are RAM (the game writes its tiles through the PPU).
extern bool chr_ram;

void video_init(const uint8_t *chr);

// The offset in `video.nametable` of a nametable address ($2000-$3EFF).
uint16_t video_nametable_offset(uint16_t addr);

// VRAM address space: pattern tables, nametables (vertical mirroring), palette.
uint8_t video_read(uint16_t addr);
void video_write(uint16_t addr, uint8_t value);

#endif
