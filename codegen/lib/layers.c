#include "layers.h"
#include "cpu.h"
#include "render.h"
#include "external.h"

uint8_t layer_background_material[LAYER_PIXELS];
uint8_t layer_backdrop;

static uint8_t level_blocks[2 * 208];

LayerLevel layers_smb_level(void) {
    LayerLevel level;
    for (uint16_t i = 0; i < 2 * 208; i++) {
        level_blocks[i] = read_byte((uint16_t)(0x0500 + i)); // block_buffer1, block_buffer2
    }
    level.blocks = level_blocks;
    level.area_kind = read_byte(0x074e); // area_kind
    for (uint16_t i = 0; i < 4; i++) {
        level.climb_limits[i] = read_byte((uint16_t)(0xdf96 + i)); // climb_metatile_limits
    }
    return level;
}

static uint8_t background_material(const LayerLevel *level, uint8_t metatile, uint8_t tile) {
    if (metatile == 0) {
        // lava is water drawn with the castle palette
        if (level->area_kind == 3 && (tile == 0x41 || tile == 0x26)) return M_LAVA;
        return M_SCENERY;
    }
    if (metatile == 0xc2 || metatile == 0xc3) return M_COIN;
    if (metatile == 0xc0 || metatile == 0xc1) return M_QUESTION;
    if (metatile == 0x26 || metatile == 0x5f || metatile == 0x60) return M_SCENERY;
    if (metatile >= level->climb_limits[metatile >> 6]) return M_CLIMB;
    return M_SOLID;
}

// Which block buffer a nametable's metatiles are in.
static int screen_of(int table) {
    if (video.mirroring == MIRROR_HORIZONTAL) return table / 2;
    if (video.mirroring == MIRROR_VERTICAL) return table % 2;
    return 0;
}

static bool needs_scanlines(void) {
    if (video.split_count != 0 || (video.ctrl & VIDEO_CTRL_SPRITE_8X16)) return true;
    for (int i = 0; i < 8; i++) {
        if (video.chr_banks[i] != i) return true;
    }
    return false;
}

static uint8_t plane_of(uint8_t material) {
    switch (material) {
    case M_BACKDROP: return PLANE_CLEAR;
    case M_SCENERY: return PLANE_BEHIND;
    case M_HUD: return PLANE_FIXED;
    default: return PLANE_ON;
    }
}

bool layers_render(const LayerLevel *level, const LayerTarget *target) {
    uint8_t backdrop = video.palette.u8[0] & 63;
    layer_backdrop = backdrop;
    if (needs_scanlines()) return false;
    bool show_background = video.mask & VIDEO_MASK_BACKGROUND;
    bool show_sprites = video.mask & VIDEO_MASK_SPRITES;
    bool left_background = video.mask & VIDEO_MASK_BACKGROUND_LEFT;
    bool left_sprites = video.mask & VIDEO_MASK_SPRITES_LEFT;
    size_t bg_bank = (video.ctrl & 0x10) ? 0x1000 : 0;
    size_t sprite_bank = (video.ctrl & 0x08) ? 0x1000 : 0;
    int base_x = video.scroll_x + ((video.ctrl & 0x01) ? 256 : 0);
    int base_y = video.scroll_y + ((video.ctrl & 0x02) ? 240 : 0);
    const uint32_t *columns = target->column_offset;
    size_t nametables[4];
    for (int t = 0; t < 4; t++) nametables[t] = video_nametable_offset((uint16_t)(0x2000 + t * 0x400));

    // The background colours as texels: the backdrop and four palettes of three.
    uint32_t colors[4][4];
    for (size_t p = 0; p < 4; p++) {
        for (size_t i = 0; i < 4; i++) {
            colors[p][i] = target->colors[i == 0 ? backdrop : video.palette.u8[p * 4 + i] & 63];
        }
    }

    // The background, one run of a tile's pixels at a time.
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        bool status = video.status_bar && y < 32;
        uint8_t clear_material = status ? M_HUD : M_BACKDROP;
        uint32_t clear_texel = colors[0][0] | target->plane_bits[status ? PLANE_FIXED : PLANE_CLEAR];
        uint32_t *out = target->background + target->row_offset[y];
        uint8_t *out_material = &layer_background_material[(size_t)y * SCREEN_WIDTH];
        if (!show_background) {
            for (int x = 0; x < SCREEN_WIDTH; x++) out[columns[x]] = clear_texel;
            memset(out_material, clear_material, SCREEN_WIDTH);
            continue;
        }
        // The row of the 512x480 plane of the nametables; the status bar is
        // the first nametable's top, unscrolled.
        int wy = status ? y : (base_y + y) % 480;
        int table_y = wy >= 240 ? 2 : 0;
        int ny = wy % 240;
        size_t tile_row = (size_t)ny / 8;
        int fine_y = ny % 8;
        int x = 0;
        if (!left_background) {
            for (; x < 8; x++) {
                out[columns[x]] = clear_texel;
                out_material[x] = clear_material;
            }
        }
        while (x < SCREEN_WIDTH) {
            int wx = status ? x : (base_x + x) % 512;
            int table = table_y + (wx >= 256 ? 1 : 0);
            int nx = wx % 256;
            int fine_x = nx % 8;
            int run = 8 - fine_x;
            if (run > SCREEN_WIDTH - x) run = SCREEN_WIDTH - x;
            size_t nametable = nametables[table];
            size_t col = (size_t)nx / 8;
            uint8_t tile = video.nametable[nametable + tile_row * 32 + col];
            uint8_t attributes = video.nametable[nametable + 960 + (tile_row / 4) * 8 + col / 4];
            unsigned shift = (unsigned)(((tile_row & 3) / 2) * 4 + ((col & 3) / 2) * 2);
            const uint32_t *palette = colors[(attributes >> shift) & 3];
            unsigned low = chr_rom[bg_bank + (size_t)tile * 16 + (size_t)fine_y];
            unsigned high = chr_rom[bg_bank + (size_t)tile * 16 + (size_t)fine_y + 8];
            uint8_t material;
            if (status) {
                material = M_HUD;
            } else if (level->blocks != NULL && y >= 32) {
                int screen = screen_of(table);
                uint8_t metatile = level->blocks[screen * 208 + ((y - 32) / 16) * 16 + nx / 16];
                material = background_material(level, metatile, tile);
            } else {
                material = M_SOLID;
            }
            uint32_t plane = target->plane_bits[plane_of(material)];
            uint8_t opaque_material = material | M_OPAQUE;
            // The run's pixels, leftmost in the patterns' top bits.
            low <<= fine_x;
            high <<= fine_x;
            for (int k = 0; k < run; k++) {
                unsigned index = ((low >> 7) & 1) | ((high >> 6) & 2);
                low <<= 1;
                high <<= 1;
                if (index == 0) {
                    out[columns[x + k]] = clear_texel;
                    out_material[x + k] = clear_material;
                } else {
                    out[columns[x + k]] = palette[index] | plane;
                    out_material[x + k] = opaque_material;
                }
            }
            x += run;
        }
    }

    // The sprites: the lowest-numbered sprite in front, those behind the
    // background hidden by its opaque pixels. Only the texels the last
    // frame's sprites wrote need clearing.
    static uint32_t *last_sprites;
    static uint32_t written[64 * 64];
    static size_t written_count;
    if (target->sprites != last_sprites) {
        memset(target->sprites, 0, target->sprites_bytes);
        last_sprites = target->sprites;
    } else {
        for (size_t i = 0; i < written_count; i++) target->sprites[written[i]] = 0;
    }
    written_count = 0;
    if (show_sprites) {
        for (int i = 63; i >= 0; i--) {
            const uint8_t *sprite = &video.oam[i * 4];
            int sy = sprite[0] + 1, sx = sprite[3];
            if (sy >= SCREEN_HEIGHT) continue;
            uint8_t tile = sprite[1], attributes = sprite[2];
            bool behind = attributes & 0x20;
            bool flip_x = attributes & 0x40;
            const uint8_t *palette = &video.palette.u8[0x10 + (size_t)(attributes & 3) * 4];
            for (int row = 0; row < 8; row++) {
                int y = sy + row;
                if (y >= SCREEN_HEIGHT) break;
                int src_row = (attributes & 0x80) ? 7 - row : row;
                unsigned low = chr_rom[sprite_bank + (size_t)tile * 16 + (size_t)src_row];
                unsigned high = chr_rom[sprite_bank + (size_t)tile * 16 + (size_t)src_row + 8];
                if ((low | high) == 0) continue;
                uint32_t row = target->row_offset[y];
                const uint8_t *materials = &layer_background_material[(size_t)y * SCREEN_WIDTH];
                for (int col = 0; col < 8; col++) {
                    int x = sx + col;
                    if (x >= SCREEN_WIDTH) break;
                    if (!left_sprites && x < 8) continue;
                    int bit = flip_x ? col : 7 - col;
                    unsigned index = ((low >> bit) & 1) | (((high >> bit) & 1) << 1);
                    if (index == 0) continue;
                    uint32_t at = row + columns[x];
                    if (behind && (materials[x] & M_OPAQUE)) {
                        // hidden, and hiding the higher-numbered sprites
                        // drawn before it (the NES's priority quirk)
                        target->sprites[at] = 0;
                        continue;
                    }
                    target->sprites[at] = target->colors[palette[index] & 63] | target->sprite_bits;
                    written[written_count++] = at;
                }
            }
        }
    }
    return true;
}
