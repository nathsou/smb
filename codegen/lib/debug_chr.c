#include "debug_chr.h"

// Made-up pattern tables for running without a ROM. Background tiles $00-$23
// follow SMB's font layout (digits, then A-Z) and $24 is SMB's blank tile, so
// the status bar and menus stay readable. Every other tile shows its index in
// hex: high nibble in color 1, low nibble in color 2, an L-shaped color 3
// corner marking orientation (flips), and transparent pixels in between so
// sprite/background priority remains visible.
//
// SMB also stores its title screen as a VRAM update list inside CHR at
// TitleScreenDataOffset ($1EC0) and reads it through PPUDATA. The debug bank
// replaces it with a short list that prints "DEBUG TILES".

#define DEBUG_TITLE_DATA_OFFSET 0x1ec0
#define DEBUG_TITLE_DATA_SIZE 0x13a
#define DEBUG_BLANK_TILE 0x24

// 3x5 glyphs, one row per entry, bit 2 = leftmost pixel.
static const uint8_t GLYPHS[36][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 3, 1, 7}, // 0-3
    {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 2, 2}, // 4-7
    {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}, {2, 5, 7, 5, 5}, {6, 5, 6, 5, 6}, // 8-B
    {3, 4, 4, 4, 3}, {6, 5, 5, 5, 6}, {7, 4, 6, 4, 7}, {7, 4, 6, 4, 4}, // C-F
    {3, 4, 5, 5, 3}, {5, 5, 7, 5, 5}, {7, 2, 2, 2, 7}, {1, 1, 1, 5, 2}, // G-J
    {5, 5, 6, 5, 5}, {4, 4, 4, 4, 7}, {5, 7, 7, 5, 5}, {6, 5, 5, 5, 5}, // K-N
    {2, 5, 5, 5, 2}, {6, 5, 6, 4, 4}, {2, 5, 5, 6, 3}, {6, 5, 6, 5, 5}, // O-R
    {3, 4, 2, 1, 6}, {7, 2, 2, 2, 2}, {5, 5, 5, 5, 7}, {5, 5, 5, 5, 2}, // S-V
    {5, 5, 7, 7, 5}, {5, 5, 2, 5, 5}, {5, 5, 2, 2, 2}, {7, 1, 2, 4, 7}, // W-Z
};

static void set_pixel(uint8_t *tile, int x, int y, uint8_t color) {
    uint8_t bit = (uint8_t)(0x80 >> x);
    tile[y] = (uint8_t)((tile[y] & ~bit) | ((color & 1) ? bit : 0));
    tile[y + 8] = (uint8_t)((tile[y + 8] & ~bit) | ((color & 2) ? bit : 0));
}

static void draw_glyph(uint8_t *tile, int glyph, int left, int top, uint8_t color) {
    for (int row = 0; row < 5; row++) {
        for (int column = 0; column < 3; column++) {
            if (GLYPHS[glyph][row] & (4 >> column)) {
                set_pixel(tile, left + column, top + row, color);
            }
        }
    }
}

static void draw_hex_tile(uint8_t *tile, int index) {
    draw_glyph(tile, index >> 4, 0, 1, 1);
    draw_glyph(tile, index & 15, 4, 1, 2);
    set_pixel(tile, 0, 7, 3);
    set_pixel(tile, 1, 7, 3);
    set_pixel(tile, 0, 6, 3);
}

static uint8_t font_tile(char c) {
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c - 'A' + 10);
    return DEBUG_BLANK_TILE;
}

static void write_title_data(uint8_t *chr) {
    static const char text[] = "DEBUG TILES";
    uint8_t *data = &chr[DEBUG_TITLE_DATA_OFFSET];
    size_t length = sizeof(text) - 1;
    for (size_t i = 0; i < DEBUG_TITLE_DATA_SIZE; i++) data[i] = 0;
    data[0] = 0x21; // PPU address $218a: centered on row 12
    data[1] = 0x8a;
    data[2] = (uint8_t)length;
    for (size_t i = 0; i < length; i++) data[3 + i] = font_tile(text[i]);
    // data[3 + length] stays 0: end of the update list
}

void debug_chr_generate(uint8_t chr[DEBUG_CHR_SIZE]) {
    for (size_t i = 0; i < DEBUG_CHR_SIZE; i++) chr[i] = 0;

    for (int bank = 0; bank < 2; bank++) {
        for (int index = 0; index < 256; index++) {
            uint8_t *tile = &chr[bank * 0x1000 + index * 16];
            bool background = bank == 1;
            if (background && index < 36) {
                draw_glyph(tile, index, 2, 1, 1);
            } else if (!(background && index == DEBUG_BLANK_TILE)) {
                draw_hex_tile(tile, index);
            }
        }
    }

    write_title_data(chr);
}
