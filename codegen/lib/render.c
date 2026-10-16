#include "render.h"
#include "external.h"

// Software reference renderer: draws one frame from the Video state.

#define SPRITES_PALETTES_OFFSET 0x11
#define BYTES_PER_PALETTE 4
#define TILES_PER_ROW 32
#define TILES_PER_COLUMN 30


uint8_t frame[FRAME_BUFFER_SIZE];
bool opaque_bg_mask[SCREEN_WIDTH * SCREEN_HEIGHT];

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

typedef struct {
    bool is_ready;
    uint8_t pixels[8 * 8 * 3];
    bool opaque_mask[8 * 8];
    uint32_t palette_mask; // mask of palette indices used in this tile
    uint32_t palette; // palette value, masked by palette_mask
    uint8_t universal_color;
} Tile;

Tile bg_cached_tiles[256][4]; // 256 tiles, 4 palettes

void clear_frame(void) {
    memset(frame, 0, SCREEN_WIDTH * SCREEN_HEIGHT * 3);
}

void clear_bg_mask(void) {
    memset(opaque_bg_mask, false, SCREEN_WIDTH * SCREEN_HEIGHT);
}

void render_init(void) {
    clear_frame();

    for (int i = 0; i < 256; i++) {
        for (int j = 0; j < 4; j++) {
            bg_cached_tiles[i][j].is_ready = false;
        }
    }
}

void set_pixel(size_t x, size_t y, uint8_t palette_color) {
    size_t index = (y * SCREEN_WIDTH + x) * 3;

    if (index < SCREEN_WIDTH * SCREEN_HEIGHT * 3) {
        size_t palette_offset = palette_color * 3;
        uint8_t r = COLOR_PALETTE[palette_offset];
        uint8_t g = COLOR_PALETTE[palette_offset + 1];
        uint8_t b = COLOR_PALETTE[palette_offset + 2];

        frame[index] = r;
        frame[index + 1] = g;
        frame[index + 2] = b;
    }
}

size_t get_background_palette_index(size_t tile_col, size_t tile_row, size_t nametable_offset) {
    size_t attr_table_index = (tile_row / 4) * 8 + (tile_col / 4);
    // the attribute table is stored after the nametable (960 bytes)
    size_t attr_table_byte = video.nametable[nametable_offset + 960 + attr_table_index];
    size_t block_x = (tile_col & 3) / 2;
    size_t block_y = (tile_row & 3) / 2;
    size_t shift = block_y * 4 + block_x * 2;

    return ((attr_table_byte >> shift) & 0b11) * BYTES_PER_PALETTE;
}

Tile* get_cached_background_tile(size_t n, size_t bank_offset, size_t palette_offset) {
    size_t palette_idx = palette_offset / 4;
    Tile *tile = &bg_cached_tiles[n][palette_idx];

    if (
        tile->is_ready &&
        tile->universal_color == video.palette.u8[0] &&
        tile->palette == (video.palette.u32[palette_idx] & tile->palette_mask)
    ) {
        return tile;
    }

    uint32_t palette_mask = 0;

    for (size_t tile_y = 0; tile_y < 8; tile_y++) {
        uint8_t plane1 = chr_rom[bank_offset + n * 16 + tile_y];
        uint8_t plane2 = chr_rom[bank_offset + n * 16 + tile_y + 8];

        for (int tile_x = 7; tile_x >= 0; tile_x--) {
            uint8_t bit0 = plane1 & 1;
            uint8_t bit1 = plane2 & 1;
            uint8_t color_index = (uint8_t)((bit1 << 1) | bit0);

            plane1 >>= 1;
            plane2 >>= 1;

            uint8_t palette_idx;
            bool is_universal_bg_color = color_index == 0;
            palette_mask |= (uint32_t)0xff << (color_index * 8);

            if (is_universal_bg_color) {
                palette_idx = video.palette.u8[0];
            } else {
                palette_idx = video.palette.u8[palette_offset + color_index];
            }

            size_t palette_offset = palette_idx * 3;

            uint8_t r = COLOR_PALETTE[palette_offset];
            uint8_t g = COLOR_PALETTE[palette_offset + 1];
            uint8_t b = COLOR_PALETTE[palette_offset + 2];

            size_t tile_offset = tile_y * 8 + (size_t)tile_x;
            size_t tile_offset_times_3 = tile_offset * 3;

            tile->pixels[tile_offset_times_3] = r;
            tile->pixels[tile_offset_times_3 + 1] = g;
            tile->pixels[tile_offset_times_3 + 2] = b;
            tile->opaque_mask[tile_offset] = !is_universal_bg_color;
        }
    }

    tile->palette_mask = palette_mask;
    tile->palette = video.palette.u32[palette_idx] & palette_mask;
    tile->universal_color = video.palette.u8[0];
    tile->is_ready = true;

    return tile;
}

void draw_background_tile(
    size_t n,
    size_t x,
    size_t y,
    size_t bank_offset,
    size_t palette_idx,
    int shift_x,
    int min_x,
    int max_x
) {
    if ((int)(x + 7) < min_x || (int)x >= (max_x + 7)) {
        return;
    }

    Tile *tile = get_cached_background_tile(n, bank_offset, palette_idx);

    // copy the tile, row by row
    for (size_t tile_y = 0; tile_y < 8; tile_y++) {
        int screen_x = (int)x + shift_x;
        size_t screen_y = y + tile_y;
        
        if ((int)x >= min_x && (int)(x + 7) < max_x) {
            size_t tile_offset = tile_y * 8;
            size_t frame_offset = screen_y * SCREEN_WIDTH + (size_t)screen_x;
            
            memcpy(&frame[frame_offset * 3], &tile->pixels[tile_offset * 3], 8 * 3);
            memcpy(&opaque_bg_mask[frame_offset], &tile->opaque_mask[tile_offset], 8);
        } else {
            // clipping
            for (size_t tile_x = 0; tile_x < 8; tile_x++) {
                size_t tile_offset = tile_y * 8 + tile_x;
                int nametable_x = (int)x + (int)tile_x;

                if (nametable_x >= min_x && nametable_x < max_x) {
                    size_t screen_x = (size_t)(shift_x + nametable_x);
                    size_t frame_offset = screen_y * SCREEN_WIDTH + screen_x;

                    memcpy(&frame[frame_offset * 3], &tile->pixels[tile_offset * 3], 3);
                    opaque_bg_mask[frame_offset] = tile->opaque_mask[tile_offset];
                }
            }
        }
    }
}

void render_status_bar(size_t bank_offset) {
    for (size_t tile_y = 0; tile_y < 4; ++tile_y) {
        for (size_t tile_x = 0; tile_x < TILES_PER_ROW; ++tile_x) {
            size_t i = tile_y * TILES_PER_ROW + tile_x;

            uint8_t tile = video.nametable[i]; 
            size_t palette_index = get_background_palette_index(tile_x, tile_y, 0);

            size_t screen_x = tile_x * 8;
            size_t screen_y = tile_y * 8;

            draw_background_tile(tile, screen_x, screen_y, bank_offset, palette_index, 0, 0, SCREEN_WIDTH);
        }
    }
}

void render_nametable(size_t nametable_offset, size_t bank_offset, int shift_x, int min_x, int max_x, size_t min_tile_y) {
    bool draw_leftmost_tile = video.mask & 0b10;

    for (size_t tile_y = min_tile_y; tile_y < TILES_PER_COLUMN; ++tile_y) {
        for (size_t tile_x = 0; tile_x < TILES_PER_ROW; ++tile_x) {
            if (!draw_leftmost_tile && tile_x == 0) {
                continue;
            }

            size_t i = tile_y * TILES_PER_ROW + tile_x;
            uint8_t tile = video.nametable[nametable_offset + i];
            size_t palette_index = get_background_palette_index(tile_x, tile_y, nametable_offset);

            size_t screen_x = tile_x << 3;
            size_t screen_y = tile_y << 3;

            draw_background_tile(tile, screen_x, screen_y, bank_offset, palette_index, shift_x, min_x, max_x);
        }
    }
}

void render_background(void) {
    // https://austinmorlan.com/posts/nes_rendering_overview/
    size_t bank_offset = video.ctrl & 0b10000 ? 0x1000 : 0;
    size_t nametable1_offset, nametable2_offset;
    bool status_bar_visible = video.status_bar;
    size_t min_tile_y = status_bar_visible ? 4 : 0;

    // vertical mirroring
    if (video.ctrl & 0b1) {
        nametable1_offset = 0x400;
        nametable2_offset = 0x000;
    } else {
        nametable1_offset = 0x000;
        nametable2_offset = 0x400;
    }

    render_nametable(nametable1_offset, bank_offset, -((int)video.scroll_x), video.scroll_x, SCREEN_WIDTH, min_tile_y);
    render_nametable(nametable2_offset, bank_offset, SCREEN_WIDTH - (int)video.scroll_x, 0, video.scroll_x, min_tile_y);

    if (status_bar_visible) {
        render_status_bar(bank_offset);
    }
}

void draw_sprite_tile(
    size_t n,
    size_t x,
    size_t y,
    size_t bank_offset,
    size_t palette_idx,
    bool flip_x,
    bool flip_y,
    bool behind_bg
) {
    for (size_t tile_y = 0; tile_y < 8; tile_y++) {
        uint8_t plane1 = chr_rom[bank_offset + n * 16 + tile_y];
        uint8_t plane2 = chr_rom[bank_offset + n * 16 + tile_y + 8];

        for (size_t tile_x = 0; tile_x < 8; tile_x++) {
            uint8_t bit0 = plane1 & 1;
            uint8_t bit1 = plane2 & 1;
            uint8_t color_index = (uint8_t)((bit1 << 1) | bit0);

            plane1 >>= 1;
            plane2 >>= 1;

            if (color_index != 0) {
                uint8_t palette_offset = video.palette.u8[palette_idx + color_index - 1];
                uint8_t flipped_x = (uint8_t)(flip_x ? tile_x : 7 - tile_x);
                uint8_t flipped_y = (uint8_t)(flip_y ? 7 - tile_y : tile_y);
                size_t screen_x = x + flipped_x;
                size_t screen_y = y + flipped_y;

                if (screen_x >= SCREEN_WIDTH || screen_y >= SCREEN_HEIGHT) {
                    continue;
                }

                bool is_hidden = behind_bg && opaque_bg_mask[screen_y * SCREEN_WIDTH + screen_x];

                if (!is_hidden) {
                    set_pixel(screen_x, screen_y, palette_offset);
                }
            }
        }
    }
}

void render_sprites(void) {
    // https://www.nesdev.org/wiki/PPU_OAM
    size_t bank_offset = video.ctrl & 0b1000 ? 0x1000 : 0;
    bool draw_leftmost_tile = video.mask & 0b100;

    // sprites with lower OAM indices are drawn in front
    for (int i = 252; i >= 0; i -= 4) {
        uint8_t x = video.oam[i + 3];
        uint8_t y = video.oam[i] + 1;

        if (!draw_leftmost_tile && x == 0) {
            continue;
        }

        uint8_t tile = video.oam[i + 1];
        uint8_t attr = video.oam[i + 2];

        bool flip_x = attr & 0b01000000;
        bool flip_y = attr & 0b10000000;
        bool behind_bg = attr & 0b00100000;

        uint8_t palette_index = SPRITES_PALETTES_OFFSET + (attr & 0b11) * BYTES_PER_PALETTE;
        draw_sprite_tile(tile, x, y, bank_offset, palette_index, flip_x, flip_y, behind_bg);
    }
}

void render_frame(void) {
    bool render_bg = video.mask & 0b00001000;
    bool render_sp = video.mask & 0b00010000;

    clear_bg_mask();

    if (render_bg) {
        render_background();
    } else {
        clear_frame();
    }

    if (render_sp) {
        render_sprites();
    }
}
