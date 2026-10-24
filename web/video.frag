// Draws one 256x240 frame from the Video state (codegen/lib/video.h).
// Shared by the desktop (GLSL 330) and web (GLSL ES 3.00) renderers; the host
// prepends the #version line. Matches the software renderer (render.c)
// pixel for pixel, except that a masked left column shows the backdrop
// where render.c, scrolling horizontally only, leaves the previous frame
// (SMB never masks it).
// The themes (web/themes) prepend `#define NES_LIBRARY` to use its functions
// without its main: select_line(y) first, for the scanline's settings.

uniform sampler2D u_patterns;  // R8, the CHR's colour indices (0-3): a 1 KiB bank (64 tiles) per 8 rows of 512
uniform sampler2D u_nametable; // 32x64 R8: screen 0 rows 0-31, screen 1 rows 32-63
uniform sampler2D u_lines;     // 16x240 R8: each scanline's settings (render.h's video_lines)
uniform int u_palette[32];     // palette RAM
uniform int u_oam[256];        // 64 sprites x (y, tile, attributes, x)
uniform vec3 u_colors[64];     // NES master palette
uniform int u_mirroring;       // MIRROR_* of video.h
uniform int u_flip_y;          // 1 when the target's first row is the bottom

#ifndef NES_LIBRARY
in vec2 fragTexCoord;
out vec4 finalColor;
#endif

int fetch(sampler2D tex, int x, int y) {
    return int(texelFetch(tex, ivec2(x, y), 0).r * 255.0 + 0.5);
}

// The scanline select_line chose: its control and mask registers, its
// position in the 512x480 plane of the four nametables (x of its first
// pixel, and row), whether it is part of a status bar, the sprites'
// priority and left masking (as render.c's path for the frame) and the CHR
// banks.
int line_ctrl;
int line_mask;
int line_x;
int line_y;
bool line_status;
bool line_hardware_priority;
int line_banks[8];

void select_line(int y) {
    line_ctrl = fetch(u_lines, 0, y);
    line_mask = fetch(u_lines, 1, y);
    line_x = fetch(u_lines, 2, y) + fetch(u_lines, 3, y) * 256;
    line_y = fetch(u_lines, 4, y) + fetch(u_lines, 5, y) * 256;
    line_status = fetch(u_lines, 6, y) != 0;
    line_hardware_priority = fetch(u_lines, 7, y) != 0;
    for (int i = 0; i < 8; i++) line_banks[i] = fetch(u_lines, 8 + i, y);
}

// Colour index (0-3) of pixel (col, row) of a tile of pattern table `table`
// (0 at $0000, 1 at $1000); col 0 is the leftmost.
int pattern(int table, int tile, int col, int row) {
    int bank = line_banks[table * 4 + tile / 64];
    return fetch(u_patterns, (tile % 64) * 8 + col, bank * 8 + row);
}

// Background pixel at nametable coordinates of one screen.
// Returns the palette RAM entry in .x, opacity (index != 0) in .y and the
// tile in .z.
ivec3 background(int screen, int nx, int ny) {
    int tile_x = nx / 8;
    int tile_y = ny / 8;
    int tile = fetch(u_nametable, tile_x, screen * 32 + tile_y);
    int table = (line_ctrl & 0x10) != 0 ? 1 : 0;
    int index = pattern(table, tile, nx % 8, ny % 8);

    // attribute byte: one per 4x4 tiles, two bits per 2x2 tiles
    int attribute_offset = 960 + (tile_y / 4) * 8 + (tile_x / 4);
    int attribute_byte = fetch(u_nametable, attribute_offset % 32, screen * 32 + attribute_offset / 32);
    int shift = ((tile_y % 4) / 2) * 4 + ((tile_x % 4) / 2) * 2;
    int palette = ((attribute_byte >> shift) & 3) * 4;

    int entry = index == 0 ? u_palette[0] : u_palette[palette + index];
    return ivec3(entry, index != 0 ? 1 : 0, tile);
}

// The screen (0 or 1) that nametable `table` (0-3) of the address space
// shows, by the mirroring.
int screen_of(int table) {
    if (u_mirroring == 1) return table / 2; // horizontal
    if (u_mirroring == 2) return 0;
    if (u_mirroring == 3) return 1;
    return table % 2; // vertical
}

// The point of the 512x480 plane pixel x of the scanline shows.
ivec2 plane_point(int x) {
    return ivec2((line_x + x) % 512, line_y % 480);
}

// The background pixel of the plane at `point`, as background().
ivec3 plane_background(ivec2 point) {
    int table = (point.y >= 240 ? 2 : 0) + (point.x >= 256 ? 1 : 0);
    return background(screen_of(table), point.x % 256, point.y % 240);
}

vec3 color(int entry) {
    return u_colors[entry & 63];
}

// The sprite pixel at (x, y) of the selected scanline y: its palette RAM
// entry in .x and its OAM index in .y, or -1 in .y when no sprite is drawn
// there.
ivec2 sprite(int x, int y, bool background_opaque) {
    bool tall = (line_ctrl & 0x20) != 0;
    int height = tall ? 16 : 8;
    int table = (line_ctrl & 0x08) != 0 ? 1 : 0;
    bool left = (line_mask & 0x04) != 0;
    // masked in the left column: the hardware's pixels, or (render.c's
    // simple path) the sprites at x 0
    if (!left && x < 8 && line_hardware_priority) return ivec2(0, -1);
    // The lowest-numbered sprite with a visible pixel here wins; a pixel
    // behind an opaque background shows the background, or (render.c's
    // simple path) lets later sprites show through.
    for (int i = 0; i < 64; i++) {
        int sx = u_oam[i * 4 + 3];
        // (render.c's simple path wraps a sprite at y 255 to the top)
        int sy = line_hardware_priority ? u_oam[i * 4] + 1 : (u_oam[i * 4] + 1) & 255;
        if (!left && sx == 0) continue;
        int dx = x - sx;
        int dy = y - sy;
        if (dx < 0 || dx > 7 || dy < 0 || dy >= height) continue;

        int tile = u_oam[i * 4 + 1];
        int attributes = u_oam[i * 4 + 2];
        int col = (attributes & 0x40) != 0 ? 7 - dx : dx;
        int row = (attributes & 0x80) != 0 ? height - 1 - dy : dy;
        int index;
        if (tall) {
            // tiles in pairs, the pair's table in the index's low bit
            index = pattern(tile & 1, (tile & 0xfe) + row / 8, col, row % 8);
        } else {
            index = pattern(table, tile, col, row);
        }
        if (index == 0) continue;
        if ((attributes & 0x20) != 0 && background_opaque) {
            if (line_hardware_priority) return ivec2(0, -1);
            continue;
        }

        return ivec2(u_palette[0x10 + (attributes & 3) * 4 + index], i);
    }
    return ivec2(0, -1);
}

#ifndef NES_LIBRARY
void main() {
    int x = int(gl_FragCoord.x);
    int y = int(gl_FragCoord.y);
    if (u_flip_y == 1) y = 239 - y;
    select_line(y);

    vec3 rgb = vec3(0.0);
    bool background_opaque = false;

    if ((line_mask & 0x08) != 0) {
        ivec2 bg;
        if ((line_mask & 0x02) == 0 && x < 8) {
            bg = ivec2(u_palette[0], 0);
        } else {
            bg = plane_background(plane_point(x)).xy;
        }
        rgb = color(bg.x);
        background_opaque = bg.y != 0;
    }

    if ((line_mask & 0x10) != 0) {
        ivec2 s = sprite(x, y, background_opaque);
        if (s.y >= 0) rgb = color(s.x);
    }

    finalColor = vec4(rgb, 1.0);
}
#endif
