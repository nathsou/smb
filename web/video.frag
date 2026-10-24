// Draws one 256x240 frame from the Video state (codegen/lib/video.h).
// Shared by the desktop (GLSL 330) and web (GLSL ES 3.00) renderers; the host
// prepends the #version line. Matches the software renderer (render.c)
// pixel for pixel, except that a masked left column shows the backdrop
// where render.c, scrolling horizontally only, leaves the previous frame
// (SMB never masks it).
// The themes (web/themes) prepend `#define NES_LIBRARY` to use its functions
// without its main.

uniform sampler2D u_patterns;  // 128x256 R8: 2 banks x 256 tiles, colour index 0-3
uniform sampler2D u_nametable; // 32x64 R8: screen 0 rows 0-31, screen 1 rows 32-63
uniform int u_palette[32];     // palette RAM
uniform int u_oam[256];        // 64 sprites x (y, tile, attributes, x)
uniform vec3 u_colors[64];     // NES master palette
uniform int u_ctrl;
uniform int u_mask;
uniform int u_scroll_x;
uniform int u_scroll_y;
uniform int u_mirroring;       // MIRROR_* of video.h
uniform int u_status_bar;
uniform int u_flip_y;          // 1 when the target's first row is the bottom

#ifndef NES_LIBRARY
in vec2 fragTexCoord;
out vec4 finalColor;
#endif

int fetch(sampler2D tex, int x, int y) {
    return int(texelFetch(tex, ivec2(x, y), 0).r * 255.0 + 0.5);
}

// Colour index (0-3) of pixel (col, row) of a tile; col 0 is the leftmost.
int pattern(int bank, int tile, int col, int row) {
    int index = bank * 256 + tile;
    return fetch(u_patterns, (index % 16) * 8 + col, (index / 16) * 8 + row);
}

// Background pixel at nametable coordinates of one screen.
// Returns the palette RAM entry in .x, opacity (index != 0) in .y and the
// tile in .z.
ivec3 background(int screen, int nx, int ny) {
    int tile_x = nx / 8;
    int tile_y = ny / 8;
    int tile = fetch(u_nametable, tile_x, screen * 32 + tile_y);
    int bank = (u_ctrl & 0x10) != 0 ? 1 : 0;
    int index = pattern(bank, tile, nx % 8, ny % 8);

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

vec3 color(int entry) {
    return u_colors[entry & 63];
}

// The sprite pixel at (x, y): its palette RAM entry in .x and its OAM index
// in .y, or -1 in .y when no sprite is drawn there.
ivec2 sprite(int x, int y, bool background_opaque) {
    int bank = (u_ctrl & 0x08) != 0 ? 1 : 0;
    bool left = (u_mask & 0x04) != 0;
    // The lowest-numbered sprite with a visible pixel here wins; a pixel
    // behind an opaque background lets later sprites show through.
    for (int i = 0; i < 64; i++) {
        int sx = u_oam[i * 4 + 3];
        int sy = (u_oam[i * 4] + 1) & 255;
        if (!left && sx == 0) continue;
        int dx = x - sx;
        int dy = y - sy;
        if (dx < 0 || dx > 7 || dy < 0 || dy > 7) continue;

        int tile = u_oam[i * 4 + 1];
        int attributes = u_oam[i * 4 + 2];
        int col = (attributes & 0x40) != 0 ? 7 - dx : dx;
        int row = (attributes & 0x80) != 0 ? 7 - dy : dy;
        int index = pattern(bank, tile, col, row);
        if (index == 0) continue;
        if ((attributes & 0x20) != 0 && background_opaque) continue;

        return ivec2(u_palette[0x10 + (attributes & 3) * 4 + index], i);
    }
    return ivec2(0, -1);
}

#ifndef NES_LIBRARY
void main() {
    int x = int(gl_FragCoord.x);
    int y = int(gl_FragCoord.y);
    if (u_flip_y == 1) y = 239 - y;

    vec3 rgb = vec3(0.0);
    bool background_opaque = false;

    if ((u_mask & 0x08) != 0) {
        ivec2 bg;
        if (u_status_bar != 0 && y < 32) {
            bg = background(0, x, y).xy; // fixed status bar from the first screen
        } else if ((u_mask & 0x02) == 0 && x < 8) {
            bg = ivec2(u_palette[0], 0);
        } else {
            // the 512x480 plane of the four nametables
            int wx = (x + u_scroll_x + ((u_ctrl & 0x01) != 0 ? 256 : 0)) % 512;
            int wy = (y + u_scroll_y + ((u_ctrl & 0x02) != 0 ? 240 : 0)) % 480;
            int table = (wy >= 240 ? 2 : 0) + (wx >= 256 ? 1 : 0);
            bg = background(screen_of(table), wx % 256, wy % 240).xy;
        }
        rgb = color(bg.x);
        background_opaque = bg.y != 0;
    }

    if ((u_mask & 0x10) != 0) {
        ivec2 s = sprite(x, y, background_opaque);
        if (s.y >= 0) rgb = color(s.x);
    }

    finalColor = vec4(rgb, 1.0);
}
#endif
