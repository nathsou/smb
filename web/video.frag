// Draws one 256x240 frame from the Video state (codegen/lib/video.h).
// Shared by the desktop (GLSL 330) and web (GLSL ES 3.00) renderers; the host
// prepends the #version line. Matches the software renderer (render.c)
// pixel for pixel, except that a masked left column shows the backdrop
// (render.c leaves the previous frame there; SMB never masks it).

uniform sampler2D u_patterns;  // 128x256 R8: 2 banks x 256 tiles, colour index 0-3
uniform sampler2D u_nametable; // 32x64 R8: screen 0 rows 0-31, screen 1 rows 32-63
uniform int u_palette[32];     // palette RAM
uniform int u_oam[256];        // 64 sprites x (y, tile, attributes, x)
uniform vec3 u_colors[64];     // NES master palette
uniform int u_ctrl;
uniform int u_mask;
uniform int u_scroll_x;
uniform int u_status_bar;
uniform int u_flip_y;          // 1 when the target's first row is the bottom

in vec2 fragTexCoord;
out vec4 finalColor;

int fetch(sampler2D tex, int x, int y) {
    return int(texelFetch(tex, ivec2(x, y), 0).r * 255.0 + 0.5);
}

// Colour index (0-3) of pixel (col, row) of a tile; col 0 is the leftmost.
int pattern(int bank, int tile, int col, int row) {
    int index = bank * 256 + tile;
    return fetch(u_patterns, (index % 16) * 8 + col, (index / 16) * 8 + row);
}

// Background pixel at nametable coordinates of one screen.
// Returns the palette RAM entry in .x and opacity (index != 0) in .y.
ivec2 background(int screen, int nx, int ny) {
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
    return ivec2(entry, index != 0 ? 1 : 0);
}

vec3 color(int entry) {
    return u_colors[entry & 63];
}

void main() {
    int x = int(gl_FragCoord.x);
    int y = int(gl_FragCoord.y);
    if (u_flip_y == 1) y = 239 - y;

    vec3 rgb = vec3(0.0);
    bool background_opaque = false;

    if ((u_mask & 0x08) != 0) {
        ivec2 bg;
        if (u_status_bar != 0 && y < 32) {
            bg = background(0, x, y); // fixed status bar from the first screen
        } else {
            int base = (u_ctrl & 0x01) != 0 ? 1 : 0;
            int nx = x + u_scroll_x;
            int screen = nx < 256 ? base : 1 - base;
            nx = nx % 256;
            if ((u_mask & 0x02) == 0 && nx < 8) {
                bg = ivec2(u_palette[0], 0);
            } else {
                bg = background(screen, nx, y);
            }
        }
        rgb = color(bg.x);
        background_opaque = bg.y != 0;
    }

    if ((u_mask & 0x10) != 0) {
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

            rgb = color(u_palette[0x10 + (attributes & 3) * 4 + index]);
            break;
        }
    }

    finalColor = vec4(rgb, 1.0);
}
