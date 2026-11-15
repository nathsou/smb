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

static uint8_t plane_of(uint8_t material) {
    switch (material) {
    case M_BACKDROP: return PLANE_CLEAR;
    case M_SCENERY: return PLANE_BEHIND;
    case M_HUD: return PLANE_FIXED;
    default: return PLANE_ON;
    }
}

// The settings a scanline is drawn with (render.c's general path): its
// part of the frame, or the whole frame's; and whether it is a status bar,
// fixed on the screen (SMB's and Zelda's at the top, SMB3's at the bottom).
typedef struct {
    uint8_t ctrl, mask;
    const uint8_t *banks;
    int x, y; // in the 512x480 plane of the nametables, at this scanline
    bool status;
} Line;

static Line line_settings(int line, int status_from, int status_to) {
    Line s;
    if (video.split_count == 0) {
        s = (Line){ video.ctrl, video.mask, video.chr_banks,
                    video.scroll_x + ((video.ctrl & 1) ? 256 : 0),
                    video.scroll_y + ((video.ctrl & 2) ? 240 : 0) + line, false };
    } else {
        const VideoSplit *split = &video.splits[0];
        for (int i = 1; i < video.split_count; i++) {
            if (video.splits[i].line <= line) split = &video.splits[i];
        }
        s = (Line){ split->ctrl, split->mask, split->chr_banks, split->x, split->y + (line - split->line), line >= status_from || line < status_to };
    }
    if (video.status_bar && line < 32) {
        // SMB's: the first screen's top, unscrolled
        s.ctrl &= (uint8_t)~3;
        s.x = 0;
        s.y = line;
        s.status = true;
    }
    s.x = ((s.x % 512) + 512) % 512;
    s.y = ((s.y % 480) + 480) % 480;
    return s;
}

bool layers_render(const LayerLevel *level, const LayerTarget *target) {
    uint8_t backdrop = video.palette.u8[0] & 63;
    layer_backdrop = backdrop;
    const uint32_t *columns = target->column_offset;
    size_t nametables[4];
    for (int t = 0; t < 4; t++) nametables[t] = video_nametable_offset((uint16_t)(0x2000 + t * 0x400));
    // a part of the frame that starts low on the screen and runs to its
    // bottom, with settings of its own, is a status bar (SMB3's)
    int status_from = SCREEN_HEIGHT;
    if (video.split_count > 1 && video.splits[video.split_count - 1].line >= 160) {
        status_from = video.splits[video.split_count - 1].line;
    }
    // and a first part that ends high on the screen, before the rest
    // scrolls on its own, is one too (Zelda's, split at sprite 0)
    int status_to = 0;
    if (video.split_count > 1 && video.splits[1].line <= 80) {
        status_to = video.splits[1].line;
    }
    static Line lines[SCREEN_HEIGHT];
    for (int y = 0; y < SCREEN_HEIGHT; y++) lines[y] = line_settings(y, status_from, status_to);

    // The background colours as texels: the backdrop and four palettes of three.
    uint32_t colors[4][4];
    for (size_t p = 0; p < 4; p++) {
        for (size_t i = 0; i < 4; i++) {
            colors[p][i] = target->colors[i == 0 ? backdrop : video.palette.u8[p * 4 + i] & 63];
        }
    }

    // The background, one run of a tile's pixels at a time.
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        const Line *s = &lines[y];
        uint8_t clear_material = s->status ? M_HUD : M_BACKDROP;
        uint32_t clear_texel = colors[0][0] | target->plane_bits[s->status ? PLANE_FIXED : PLANE_CLEAR];
        uint32_t *out = target->background + target->row_offset[y];
        uint8_t *out_material = &layer_background_material[(size_t)y * SCREEN_WIDTH];
        if (!(s->mask & VIDEO_MASK_BACKGROUND)) {
            // black, as render.c draws it, on the screen's plane
            uint32_t black = target->plane_bits[s->status ? PLANE_FIXED : PLANE_ON];
            for (int x = 0; x < SCREEN_WIDTH; x++) out[columns[x]] = black;
            memset(out_material, clear_material, SCREEN_WIDTH);
            continue;
        }
        int table_y = s->y >= 240 ? 2 : 0;
        int ny = s->y % 240;
        size_t tile_row = (size_t)ny / 8;
        int fine_y = ny % 8;
        uint16_t pattern_table = (s->ctrl & VIDEO_CTRL_BACKGROUND_BANK) ? 0x1000 : 0;
        int x = 0;
        if (!(s->mask & VIDEO_MASK_BACKGROUND_LEFT)) {
            for (; x < 8; x++) {
                out[columns[x]] = clear_texel;
                out_material[x] = clear_material;
            }
        }
        while (x < SCREEN_WIDTH) {
            int wx = (s->x + x) % 512;
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
            uint16_t address = (uint16_t)(pattern_table + tile * 16 + fine_y);
            unsigned low = video_pattern(s->banks, address);
            unsigned high = video_pattern(s->banks, (uint16_t)(address + 8));
            uint8_t material;
            if (s->status) {
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
    // background hidden by its opaque pixels. Each row with its scanline's
    // settings (height, pattern table, banks). Only the texels the last
    // frame's sprites wrote need clearing.
    static uint32_t *last_sprites;
    static uint32_t written[64 * 16 * 8];
    static size_t written_count;
    if (target->sprites != last_sprites) {
        memset(target->sprites, 0, target->sprites_bytes);
        last_sprites = target->sprites;
    } else {
        for (size_t i = 0; i < written_count; i++) target->sprites[written[i]] = 0;
    }
    written_count = 0;
    for (int i = 63; i >= 0; i--) {
        const uint8_t *sprite = &video.oam[i * 4];
        int sy = sprite[0] + 1, sx = sprite[3];
        if (sy >= SCREEN_HEIGHT) continue;
        uint8_t tile = sprite[1], attributes = sprite[2];
        bool behind = attributes & 0x20;
        bool flip_x = attributes & 0x40;
        const uint8_t *palette = &video.palette.u8[0x10 + (size_t)(attributes & 3) * 4];
        for (int dy = 0; dy < 16; dy++) {
            int y = sy + dy;
            if (y >= SCREEN_HEIGHT) break;
            const Line *s = &lines[y];
            bool tall = s->ctrl & VIDEO_CTRL_SPRITE_8X16;
            int height = tall ? 16 : 8;
            if (dy >= height) break;
            if (!(s->mask & VIDEO_MASK_SPRITES)) continue;
            int row = (attributes & 0x80) ? height - 1 - dy : dy;
            uint16_t address;
            if (tall) {
                address = (uint16_t)(((tile & 1) ? 0x1000 : 0) + ((tile & 0xfe) + (row >= 8 ? 1 : 0)) * 16 + (row & 7));
            } else {
                address = (uint16_t)(((s->ctrl & VIDEO_CTRL_SPRITE_BANK) ? 0x1000 : 0) + tile * 16 + row);
            }
            unsigned low = video_pattern(s->banks, address);
            unsigned high = video_pattern(s->banks, (uint16_t)(address + 8));
            if ((low | high) == 0) continue;
            bool left = s->mask & VIDEO_MASK_SPRITES_LEFT;
            uint32_t row_offset = target->row_offset[y];
            const uint8_t *materials = &layer_background_material[(size_t)y * SCREEN_WIDTH];
            for (int col = 0; col < 8; col++) {
                int x = sx + col;
                if (x >= SCREEN_WIDTH) break;
                if (!left && x < 8) continue;
                int bit = flip_x ? col : 7 - col;
                unsigned index = ((low >> bit) & 1) | (((high >> bit) & 1) << 1);
                if (index == 0) continue;
                uint32_t at = row_offset + columns[x];
                if (behind && (materials[x] & M_OPAQUE)) {
                    // hidden, and hiding the higher-numbered sprites
                    // drawn before it (the NES's priority)
                    target->sprites[at] = 0;
                    continue;
                }
                target->sprites[at] = target->colors[palette[index] & 63] | target->sprite_bits;
                written[written_count++] = at;
            }
        }
    }
    return true;
}
